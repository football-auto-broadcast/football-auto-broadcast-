/**
 * box_activity_analyzer.cpp - 禁区活跃度分析器实现
 *
 * 检测禁区内的危险进攻信号和小禁区精彩瞬间。
 */

#include "box_activity_analyzer.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

#include <opencv2/core.hpp>

namespace vision {
namespace inference {

struct BoxActivityAnalyzer::Impl {
    bool initialized = false;
    // TODO: 禁区 ROI 配置
    // Rect penalty_box_roi;
    // Rect six_yard_box_roi;
};

BoxActivityAnalyzer::BoxActivityAnalyzer()
    : impl_(std::make_unique<Impl>()) {}

BoxActivityAnalyzer::~BoxActivityAnalyzer() = default;

bool BoxActivityAnalyzer::initialize() {
    std::cout << "[box_activity_analyzer] Initializing..." << std::endl;
    impl_->initialized = true;
    return true;
}

BoxActivity BoxActivityAnalyzer::analyze(
    void* image_data, int width, int height,
    float ball_x, float ball_y) {
    BoxActivity result;

    if (!impl_->initialized) {
        std::cerr << "[box_activity_analyzer] Analyzer not initialized" << std::endl;
        return result;
    }

    if (!image_data || width <= 0 || height <= 0) {
        return result;
    }

    const bool aux_like_goal_view = ball_x >= width * 0.45f;
    // MVP uses a camera-local goal-side ROI. Calibration can replace these
    // ratios later without changing the frozen output JSON contract.
    const int roi_x = aux_like_goal_view ? width / 2 : static_cast<int>(width * 0.62);
    const int roi_y = static_cast<int>(height * 0.20);
    const int roi_w = std::max(1, width - roi_x);
    const int roi_h = static_cast<int>(height * 0.62);

    cv::Mat image(height, width, CV_8UC3, image_data);
    cv::Rect roi(roi_x, roi_y, std::min(roi_w, width - roi_x), std::min(roi_h, height - roi_y));
    if (roi.width <= 0 || roi.height <= 0) {
        return result;
    }

    const cv::Mat patch = image(roi);
    int active_samples = 0;
    int dense_samples = 0;
    int total_samples = 0;
    const int step_x = std::max(1, roi.width / 64);
    const int step_y = std::max(1, roi.height / 36);
    for (int y = 0; y < patch.rows; y += step_y) {
        for (int x = 0; x < patch.cols; x += step_x) {
            // Bright/chromatic samples roughly approximate crowd/ball activity
            // in the goal-side crop without running an extra detector.
            const cv::Vec3b pixel = patch.at<cv::Vec3b>(y, x);
            const double b = pixel[0];
            const double g = pixel[1];
            const double r = pixel[2];
            const double brightness = (r + g + b) / (3.0 * 255.0);
            const double chroma = std::max({std::abs(r - g), std::abs(g - b), std::abs(r - b)}) / 255.0;
            if (brightness > 0.30 || chroma > 0.18) {
                ++active_samples;
            }
            if (brightness > 0.42 || chroma > 0.28) {
                ++dense_samples;
            }
            ++total_samples;
        }
    }

    result.intensity = std::min(1.0, static_cast<double>(active_samples) /
                                     std::max(1, total_samples) * 3.0);
    result.crowd_density = std::min(1.0, static_cast<double>(dense_samples) /
                                         std::max(1, total_samples) * 4.0);
    const bool ball_in_goal_roi =
        ball_x >= roi.x && ball_x <= roi.x + roi.width &&
        ball_y >= roi.y && ball_y <= roi.y + roi.height;
    result.high_intensity = result.intensity >= 0.55;
    result.is_six_yard_box_highlight =
        ball_in_goal_roi && result.high_intensity && result.crowd_density >= 0.30;

    return result;
}

} // namespace inference
} // namespace vision
