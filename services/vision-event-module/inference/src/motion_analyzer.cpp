/**
 * motion_analyzer.cpp - 运动分析器实现
 *
 * 使用背景减除和轮廓检测分析画面中的运动模式。
 */

#include "motion_analyzer.hpp"

#include <algorithm>
#include <iostream>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace vision {
namespace inference {

struct MotionAnalyzer::Impl {
    bool initialized = false;
    cv::Mat prev_gray;
};

MotionAnalyzer::MotionAnalyzer()
    : impl_(std::make_unique<Impl>()) {}

MotionAnalyzer::~MotionAnalyzer() = default;

bool MotionAnalyzer::initialize() {
    std::cout << "[motion_analyzer] Initializing..." << std::endl;
    // TODO: 创建背景减除器 (MOG2 或 KNN)
    impl_->initialized = true;
    return true;
}

MotionAnalysis MotionAnalyzer::analyze(void* image_data, int width, int height) {
    MotionAnalysis result;

    if (!impl_->initialized) {
        std::cerr << "[motion_analyzer] Analyzer not initialized" << std::endl;
        return result;
    }

    if (!image_data || width <= 0 || height <= 0) {
        return result;
    }

    cv::Mat bgr(height, width, CV_8UC3, image_data);
    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0.0);

    if (impl_->prev_gray.empty()) {
        // First frame seeds the temporal model; motion needs at least two frames.
        impl_->prev_gray = gray.clone();
        return result;
    }

    cv::Mat diff;
    cv::absdiff(gray, impl_->prev_gray, diff);
    impl_->prev_gray = gray.clone();

    cv::Mat mask;
    // Frame differencing is intentionally lightweight for MVP realtime use.
    // Morphology removes sensor noise and expands nearby moving pixels.
    cv::threshold(diff, mask, 28, 255, cv::THRESH_BINARY);
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, cv::Mat(), cv::Point(-1, -1), 1);
    cv::morphologyEx(mask, mask, cv::MORPH_DILATE, cv::Mat(), cv::Point(-1, -1), 2);

    std::vector<cv::Point> points;
    cv::findNonZero(mask, points);
    if (points.empty()) {
        return result;
    }

    const cv::Rect rect = cv::boundingRect(points);
    const double active_pixels = static_cast<double>(cv::countNonZero(mask));
    const double total_pixels = static_cast<double>(std::max(1, width * height));

    result.motion_cluster_rect = Rect{rect.x, rect.y, rect.width, rect.height};
    result.global_activity = std::min(1.0, active_pixels / total_pixels * 8.0);
    result.attack_direction = rect.x + rect.width / 2.0 >= width / 2.0 ? 0.0 : 180.0;
    result.has_significant_motion = result.global_activity >= 0.03 &&
                                    result.motion_cluster_rect.is_valid();

    return result;
}

void MotionAnalyzer::reset() {
    impl_->prev_gray.release();
}

} // namespace inference
} // namespace vision
