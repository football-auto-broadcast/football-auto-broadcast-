/**
 * ball_detector.cpp - 足球检测器实现
 *
 * 加载 ONNX 模型并执行推理，检测足球位置和置信度。
 */

#include "ball_detector.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>

#include <onnxruntime_cxx_api.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace vision {
namespace inference {

namespace {

constexpr float kDefaultConfidenceThreshold = 0.35f;
constexpr float kDefaultNmsThreshold = 0.45f;
constexpr int kFallbackInputSize = 640;

struct LetterboxInfo {
    float scale = 1.0f;
    int pad_x = 0;
    int pad_y = 0;
};

struct CandidateBox {
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;
    float score = 0.0f;
};

float clamp_float(float value, float low, float high) {
    return std::max(low, std::min(high, value));
}

float box_iou(const CandidateBox& a, const CandidateBox& b) {
    const float inter_x1 = std::max(a.x1, b.x1);
    const float inter_y1 = std::max(a.y1, b.y1);
    const float inter_x2 = std::min(a.x2, b.x2);
    const float inter_y2 = std::min(a.y2, b.y2);
    const float inter_w = std::max(0.0f, inter_x2 - inter_x1);
    const float inter_h = std::max(0.0f, inter_y2 - inter_y1);
    const float inter_area = inter_w * inter_h;
    const float area_a = std::max(0.0f, a.x2 - a.x1) * std::max(0.0f, a.y2 - a.y1);
    const float area_b = std::max(0.0f, b.x2 - b.x1) * std::max(0.0f, b.y2 - b.y1);
    const float union_area = area_a + area_b - inter_area;
    if (union_area <= 0.0f) {
        return 0.0f;
    }
    return inter_area / union_area;
}

std::vector<CandidateBox> nms(std::vector<CandidateBox> boxes, float iou_threshold) {
    // YOLO may produce several boxes around the same ball. NMS keeps the
    // strongest box and suppresses heavily-overlapping duplicates.
    std::sort(boxes.begin(), boxes.end(), [](const CandidateBox& lhs, const CandidateBox& rhs) {
        return lhs.score > rhs.score;
    });

    std::vector<CandidateBox> kept;
    std::vector<bool> removed(boxes.size(), false);
    for (size_t i = 0; i < boxes.size(); ++i) {
        if (removed[i]) continue;
        kept.push_back(boxes[i]);
        for (size_t j = i + 1; j < boxes.size(); ++j) {
            if (!removed[j] && box_iou(boxes[i], boxes[j]) > iou_threshold) {
                removed[j] = true;
            }
        }
    }
    return kept;
}

bool select_highest_confidence_box(const std::vector<CandidateBox>& boxes, CandidateBox& best_box) {
    // MVP only tracks one football. After NMS, keep the single most confident
    // candidate so downstream focus/event logic never sees multiple balls.
    if (boxes.empty()) {
        return false;
    }
    const auto best_it = std::max_element(
        boxes.begin(),
        boxes.end(),
        [](const CandidateBox& lhs, const CandidateBox& rhs) {
            return lhs.score < rhs.score;
        });
    best_box = *best_it;
    return true;
}

#ifdef _WIN32
std::wstring widen_path(const std::string& path) {
    return std::wstring(path.begin(), path.end());
}
#endif

} // namespace

struct BallDetector::Impl {
    std::string model_path;
    bool initialized = false;
    int input_width = kFallbackInputSize;
    int input_height = kFallbackInputSize;
    float confidence_threshold = kDefaultConfidenceThreshold;
    float nms_threshold = kDefaultNmsThreshold;

    std::unique_ptr<Ort::Env> env;
    std::unique_ptr<Ort::SessionOptions> session_options;
    std::unique_ptr<Ort::Session> session;
    std::vector<std::string> input_names;
    std::vector<std::string> output_names;

    static std::vector<float> make_input_tensor(const cv::Mat& bgr,
                                                int target_width,
                                                int target_height,
                                                LetterboxInfo& letterbox) {
        // Match YOLO export preprocessing: resize with unchanged aspect ratio,
        // pad with 114, convert BGR->RGB, normalize, and pack as NCHW.
        const float scale = std::min(
            static_cast<float>(target_width) / std::max(1, bgr.cols),
            static_cast<float>(target_height) / std::max(1, bgr.rows));
        const int resized_width = std::max(1, static_cast<int>(std::round(bgr.cols * scale)));
        const int resized_height = std::max(1, static_cast<int>(std::round(bgr.rows * scale)));
        letterbox.scale = scale;
        letterbox.pad_x = (target_width - resized_width) / 2;
        letterbox.pad_y = (target_height - resized_height) / 2;

        cv::Mat resized;
        cv::resize(bgr, resized, cv::Size(resized_width, resized_height));

        cv::Mat canvas(target_height, target_width, CV_8UC3, cv::Scalar(114, 114, 114));
        resized.copyTo(canvas(cv::Rect(letterbox.pad_x, letterbox.pad_y, resized_width, resized_height)));

        cv::Mat rgb;
        cv::cvtColor(canvas, rgb, cv::COLOR_BGR2RGB);
        rgb.convertTo(rgb, CV_32FC3, 1.0 / 255.0);

        std::vector<cv::Mat> channels(3);
        cv::split(rgb, channels);

        std::vector<float> input(3 * target_width * target_height);
        const size_t plane_size = static_cast<size_t>(target_width) * target_height;
        for (int c = 0; c < 3; ++c) {
            std::memcpy(input.data() + c * plane_size,
                        channels[c].data,
                        plane_size * sizeof(float));
        }
        return input;
    }

    CandidateBox map_box_to_original(float cx,
                                     float cy,
                                     float box_w,
                                     float box_h,
                                     float score,
                                     const LetterboxInfo& letterbox,
                                     int original_width,
                                     int original_height) const {
        // Raw YOLO outputs boxes in the padded model-input coordinate system.
        // Remove letterbox padding and scale back to the source frame.
        float x1 = cx - box_w / 2.0f;
        float y1 = cy - box_h / 2.0f;
        float x2 = cx + box_w / 2.0f;
        float y2 = cy + box_h / 2.0f;

        x1 = (x1 - letterbox.pad_x) / letterbox.scale;
        y1 = (y1 - letterbox.pad_y) / letterbox.scale;
        x2 = (x2 - letterbox.pad_x) / letterbox.scale;
        y2 = (y2 - letterbox.pad_y) / letterbox.scale;

        CandidateBox box;
        box.x1 = clamp_float(x1, 0.0f, static_cast<float>(std::max(0, original_width - 1)));
        box.y1 = clamp_float(y1, 0.0f, static_cast<float>(std::max(0, original_height - 1)));
        box.x2 = clamp_float(x2, 0.0f, static_cast<float>(std::max(0, original_width - 1)));
        box.y2 = clamp_float(y2, 0.0f, static_cast<float>(std::max(0, original_height - 1)));
        box.score = score;
        return box;
    }

    CandidateBox map_xyxy_to_original(float x1,
                                      float y1,
                                      float x2,
                                      float y2,
                                      float score,
                                      const LetterboxInfo& letterbox,
                                      int original_width,
                                      int original_height) const {
        // Some exported YOLO graphs include NMS and output xyxy directly.
        // Those coordinates still need letterbox removal.
        x1 = (x1 - letterbox.pad_x) / letterbox.scale;
        y1 = (y1 - letterbox.pad_y) / letterbox.scale;
        x2 = (x2 - letterbox.pad_x) / letterbox.scale;
        y2 = (y2 - letterbox.pad_y) / letterbox.scale;

        CandidateBox box;
        box.x1 = clamp_float(x1, 0.0f, static_cast<float>(std::max(0, original_width - 1)));
        box.y1 = clamp_float(y1, 0.0f, static_cast<float>(std::max(0, original_height - 1)));
        box.x2 = clamp_float(x2, 0.0f, static_cast<float>(std::max(0, original_width - 1)));
        box.y2 = clamp_float(y2, 0.0f, static_cast<float>(std::max(0, original_height - 1)));
        box.score = score;
        return box;
    }

    std::vector<CandidateBox> parse_yolo_output(const float* output,
                                                const std::vector<int64_t>& shape,
                                                const LetterboxInfo& letterbox,
                                                int original_width,
                                                int original_height) const {
        // Support common YOLO exports:
        // - YOLOv8 raw: [1, attrs, boxes]
        // - YOLOv5/raw-like: [1, boxes, attrs]
        // - end-to-end NMS: [1, boxes, 6] as xyxy + score + class
        std::vector<int64_t> dims;
        for (int64_t dim : shape) {
            if (dim > 1 || dims.empty()) {
                dims.push_back(dim);
            }
        }
        if (dims.size() == 3 && dims[0] == 1) {
            dims.erase(dims.begin());
        }
        if (dims.size() != 2) {
            return {};
        }

        int64_t dim0 = dims[0];
        int64_t dim1 = dims[1];
        if (dim0 <= 0 || dim1 <= 0) {
            return {};
        }

        const bool attrs_first = dim0 < dim1;
        const int64_t attrs = attrs_first ? dim0 : dim1;
        const int64_t count = attrs_first ? dim1 : dim0;
        if (attrs < 5 || count <= 0) {
            return {};
        }

        auto value_at = [&](int64_t row, int64_t attr) -> float {
            return attrs_first ? output[attr * count + row] : output[row * attrs + attr];
        };

        std::vector<CandidateBox> boxes;
        boxes.reserve(static_cast<size_t>(std::min<int64_t>(count, 1024)));
        for (int64_t i = 0; i < count; ++i) {
            const float cx = value_at(i, 0);
            const float cy = value_at(i, 1);
            const float box_w = value_at(i, 2);
            const float box_h = value_at(i, 3);
            const bool nms_xyxy_output = attrs == 6 && count <= 1000 && box_w > cx && box_h > cy;

            float best_score = value_at(i, 4);
            if (nms_xyxy_output) {
                best_score = value_at(i, 4);
            } else if (attrs > 5) {
                // Single-class YOLOv8 exports often place class confidence at
                // attr 4. YOLOv5-like exports use objectness * class score.
                // Taking the max of both interpretations is robust for both.
                float max_class_from_4 = value_at(i, 4);
                for (int64_t attr = 5; attr < attrs; ++attr) {
                    max_class_from_4 = std::max(max_class_from_4, value_at(i, attr));
                }

                float max_class_from_5 = 0.0f;
                for (int64_t attr = 5; attr < attrs; ++attr) {
                    max_class_from_5 = std::max(max_class_from_5, value_at(i, attr));
                }
                const float yolo5_score = value_at(i, 4) * max_class_from_5;
                best_score = std::max(max_class_from_4, yolo5_score);
            }

            if (best_score < confidence_threshold || box_w <= 0.0f || box_h <= 0.0f) {
                continue;
            }

            CandidateBox box = nms_xyxy_output
                ? map_xyxy_to_original(cx, cy, box_w, box_h, best_score,
                                       letterbox, original_width, original_height)
                : map_box_to_original(cx, cy, box_w, box_h, best_score,
                                      letterbox, original_width, original_height);
            if (box.x2 > box.x1 && box.y2 > box.y1) {
                boxes.push_back(box);
            }
        }

        return boxes;
    }
};

BallDetector::BallDetector(const std::string& model_path,
                           float confidence_threshold,
                           float nms_threshold)
    : impl_(std::make_unique<Impl>()) {
    impl_->model_path = model_path;
    impl_->confidence_threshold = confidence_threshold;
    impl_->nms_threshold = nms_threshold;
}

BallDetector::~BallDetector() = default;

bool BallDetector::initialize() {
    std::cout << "[ball_detector] Loading model: " << impl_->model_path << std::endl;

    if (!std::ifstream(impl_->model_path).good()) {
        std::cerr << "[ball_detector] Model file not found: " << impl_->model_path << std::endl;
        return false;
    }

    try {
        impl_->env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "ball_detector");
        impl_->session_options = std::make_unique<Ort::SessionOptions>();
        impl_->session_options->SetIntraOpNumThreads(1);
        impl_->session_options->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

#ifdef _WIN32
        const std::wstring model_path = widen_path(impl_->model_path);
        impl_->session = std::make_unique<Ort::Session>(
            *impl_->env, model_path.c_str(), *impl_->session_options);
#else
        impl_->session = std::make_unique<Ort::Session>(
            *impl_->env, impl_->model_path.c_str(), *impl_->session_options);
#endif

        Ort::AllocatorWithDefaultOptions allocator;
        const size_t input_count = impl_->session->GetInputCount();
        const size_t output_count = impl_->session->GetOutputCount();
        impl_->input_names.clear();
        impl_->output_names.clear();
        for (size_t i = 0; i < input_count; ++i) {
            auto name = impl_->session->GetInputNameAllocated(i, allocator);
            impl_->input_names.emplace_back(name.get());
        }
        for (size_t i = 0; i < output_count; ++i) {
            auto name = impl_->session->GetOutputNameAllocated(i, allocator);
            impl_->output_names.emplace_back(name.get());
        }

        if (impl_->input_names.empty() || impl_->output_names.empty()) {
            std::cerr << "[ball_detector] Model has no valid input/output tensors" << std::endl;
            return false;
        }

        const Ort::TypeInfo input_type_info = impl_->session->GetInputTypeInfo(0);
        const auto tensor_info = input_type_info.GetTensorTypeAndShapeInfo();
        const std::vector<int64_t> input_shape = tensor_info.GetShape();
        if (input_shape.size() == 4) {
            if (input_shape[2] > 0) impl_->input_height = static_cast<int>(input_shape[2]);
            if (input_shape[3] > 0) impl_->input_width = static_cast<int>(input_shape[3]);
        }

        impl_->initialized = true;
        std::cout << "[ball_detector] Model loaded successfully, input="
                  << impl_->input_width << "x" << impl_->input_height << std::endl;
        return true;
    } catch (const Ort::Exception& ex) {
        std::cerr << "[ball_detector] ONNX Runtime error: " << ex.what() << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "[ball_detector] Initialization error: " << ex.what() << std::endl;
    }

    impl_->initialized = false;
    return false;
}

BallDetection BallDetector::detect(void* image_data, int width, int height) {
    BallDetection result;

    if (!impl_->initialized) {
        std::cerr << "[ball_detector] Detector not initialized" << std::endl;
        return result;
    }

    if (!image_data || width <= 0 || height <= 0 || !impl_->session) {
        return result;
    }

    try {
        cv::Mat frame(height, width, CV_8UC3, image_data);
        LetterboxInfo letterbox;
        std::vector<float> input_tensor_values =
            Impl::make_input_tensor(frame, impl_->input_width, impl_->input_height, letterbox);

        std::array<int64_t, 4> input_shape{
            1, 3, impl_->input_height, impl_->input_width
        };
        Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(
            OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info,
            input_tensor_values.data(),
            input_tensor_values.size(),
            input_shape.data(),
            input_shape.size());

        std::vector<const char*> input_name_ptrs;
        std::vector<const char*> output_name_ptrs;
        input_name_ptrs.reserve(impl_->input_names.size());
        output_name_ptrs.reserve(impl_->output_names.size());
        for (const auto& name : impl_->input_names) input_name_ptrs.push_back(name.c_str());
        for (const auto& name : impl_->output_names) output_name_ptrs.push_back(name.c_str());

        std::vector<Ort::Value> outputs = impl_->session->Run(
            Ort::RunOptions{nullptr},
            input_name_ptrs.data(),
            &input_tensor,
            1,
            output_name_ptrs.data(),
            output_name_ptrs.size());

        if (outputs.empty() || !outputs[0].IsTensor()) {
            return result;
        }

        const float* output_data = outputs[0].GetTensorData<float>();
        const auto output_info = outputs[0].GetTensorTypeAndShapeInfo();
        const std::vector<int64_t> output_shape = output_info.GetShape();
        std::vector<CandidateBox> candidates = impl_->parse_yolo_output(
            output_data, output_shape, letterbox, width, height);
        candidates = nms(std::move(candidates), impl_->nms_threshold);

        CandidateBox best;
        if (!select_highest_confidence_box(candidates, best)) {
            return result;
        }

        result.x = (best.x1 + best.x2) / 2.0f;
        result.y = (best.y1 + best.y2) / 2.0f;
        result.width = best.x2 - best.x1;
        result.height = best.y2 - best.y1;
        result.confidence = best.score;
        result.detected = true;
        return result;
    } catch (const Ort::Exception& ex) {
        std::cerr << "[ball_detector] ONNX Runtime inference error: " << ex.what() << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "[ball_detector] Inference error: " << ex.what() << std::endl;
    }

    return result;
}

bool BallDetector::is_initialized() const {
    return impl_->initialized;
}

} // namespace inference
} // namespace vision
