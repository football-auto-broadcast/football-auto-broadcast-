from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path

import numpy as np

# 尝试导入 opencv 和 ultralytics，如果环境未安装则给出明确提示。
try:
    import cv2
    from ultralytics import YOLO
except ModuleNotFoundError as exc:
    missing = exc.name
    raise SystemExit(
        f"Missing Python dependency: {missing}. "
        "Install opencv-python and ultralytics in the environment used to run this script."
    ) from exc


@dataclass
class Detection:
    """单帧检测结果，保存边界框和置信度信息。"""

    x: float = 0.0          # 边界框中心 x 坐标（像素）
    y: float = 0.0          # 边界框中心 y 坐标（像素）
    width: float = 0.0      # 边界框宽度（像素）
    height: float = 0.0     # 边界框高度（像素）
    confidence: float = 0.0 # 检测置信度 [0, 1]
    detected: bool = False  # 该帧是否检测到目标
    accepted: bool = False  # 该检测是否被滤波器接受用于更新
    predicted: bool = False # 是否为无 YOLO 检测时的短时运动预测结果


class BallKalmanFilter:
    """恒加速度 2D 卡尔曼滤波器，用于足球高速/弧线运动预测。

    状态向量为 [x, y, vx, vy, ax, ay]，即位置、速度和加速度。
    默认输出当前稳定估计；需要提前量时可通过 CLI 开启短时间前瞻。
    """

    def __init__(
        self,
        min_update_conf: float = 0.35,
        gate_base_px: float = 180.0,
        gate_conf_scale_px: float = 450.0,
        hard_reset_conf: float = 0.72,
        max_predict_frames: int = 6,
        reset_confirm_frames: int = 2,
        min_missing_before_reset: int = 3,
        velocity_blend: float = 0.25,
        max_speed_px_s: float = 1800.0,
        acceleration_blend: float = 0.08,
        max_accel_px_s2: float = 3500.0,
        prediction_lead_s: float = 0.0,
        max_extra_lead_s: float = 0.0,
        gate_min_px: float = 45.0,
        gate_max_px: float = 260.0,
        gate_speed_scale: float = 2.2,
    ) -> None:
        # 滤波器是否已用第一个测量值初始化
        self.initialized = False
        # 上一次处理的时间戳（毫秒），用于计算帧间 dt
        self.last_timestamp_ms = 0.0
        # 状态向量 [x, y, vx, vy, ax, ay]，初始为零
        self.x = np.zeros((6, 1), dtype=np.float64)
        # 协方差矩阵，初始不确定度设为 80 px^2，与 C++ 服务一致。
        # 该值足够大，使滤波器在初始阶段更信任测量值。
        self.p = np.eye(6, dtype=np.float64) * 80.0
        # 低于该置信度的 YOLO 框只显示 raw，不更新 Kalman，减少明显误识别拖偏。
        self.min_update_conf = min_update_conf
        # 位置门控：检测点离预测点太远时，认为是误检或目标切换。
        self.gate_base_px = gate_base_px
        # 高置信框允许更大位移，低置信框只能小范围修正。
        self.gate_conf_scale_px = gate_conf_scale_px
        # 置信度足够高的大跳变直接重置，避免真实快速位移时 Kalman 滞后。
        self.hard_reset_conf = hard_reset_conf
        # YOLO 短时丢检时允许输出若干帧预测框，超过后停止显示，避免长时间漂移。
        self.max_predict_frames = max_predict_frames
        # 高置信大跳变需要连续确认，避免单帧误检直接把轨迹重置到远处。
        self.reset_confirm_frames = reset_confirm_frames
        # 刚刚还有稳定轨迹时，不立即接受远距离重置；只有连续丢失若干帧后
        # 才允许跨屏重捕，避免短时误检造成绿色框突然跳屏。
        self.min_missing_before_reset = min_missing_before_reset
        self.missing_frames = 0
        self.last_width = 0.0
        self.last_height = 0.0
        self.pending_reset: Detection | None = None
        self.pending_reset_count = 0
        # 用连续接受的 YOLO 测量估计速度，增强丢检时的运动预测能力。
        self.prev_measurement: tuple[float, float, float] | None = None
        self.prev_measured_velocity: tuple[float, float] | None = None
        self.velocity_blend = velocity_blend
        # 限制预测速度，避免误检更新后速度过大导致预测框飞出画面。
        self.max_speed_px_s = max_speed_px_s
        # 用连续测量点估计加速度，支持射门和抛物线/弧线轨迹的提前预测。
        self.acceleration_blend = acceleration_blend
        self.max_accel_px_s2 = max_accel_px_s2
        # 输出框前瞻时间默认关闭。速度越快可额外增加少量前瞻，但有上限防止过冲。
        self.prediction_lead_s = prediction_lead_s
        self.max_extra_lead_s = max_extra_lead_s
        # 自适应门控：参考 SORT/DeepSORT 的 tracking-by-detection 思路，
        # 根据当前运动速度放宽门控，而不是单纯因为 YOLO 置信度高就接受大跳点。
        self.gate_min_px = gate_min_px
        self.gate_max_px = gate_max_px
        self.gate_speed_scale = gate_speed_scale

    def reset(self, measured_x: float, measured_y: float, timestamp_ms: float) -> None:
        """用第一个测量值重新初始化滤波器。

        位置设为测量值，速度设为零，协方差恢复到初始值。
        """
        self.x = np.array([[measured_x], [measured_y], [0.0], [0.0], [0.0], [0.0]], dtype=np.float64)
        # 初始位置不确定度约 80 px^2，与 C++ 服务保持一致。
        self.p = np.eye(6, dtype=np.float64) * 80.0
        self.last_timestamp_ms = timestamp_ms
        self.initialized = True
        self.missing_frames = 0
        self.pending_reset = None
        self.pending_reset_count = 0
        self.prev_measurement = (measured_x, measured_y, timestamp_ms)
        self.prev_measured_velocity = None

    def clamp_velocity(self) -> None:
        speed = float(np.hypot(self.x[2, 0], self.x[3, 0]))
        if speed > self.max_speed_px_s:
            scale = self.max_speed_px_s / max(speed, 1e-6)
            self.x[2, 0] *= scale
            self.x[3, 0] *= scale

    def clamp_acceleration(self) -> None:
        accel = float(np.hypot(self.x[4, 0], self.x[5, 0]))
        if accel > self.max_accel_px_s2:
            scale = self.max_accel_px_s2 / max(accel, 1e-6)
            self.x[4, 0] *= scale
            self.x[5, 0] *= scale

    def lead_time(self) -> float:
        speed_ratio = min(1.0, float(np.hypot(self.x[2, 0], self.x[3, 0])) / max(self.max_speed_px_s, 1.0))
        return max(0.0, self.prediction_lead_s + self.max_extra_lead_s * speed_ratio)

    def forecast_position(self, lead_s: float | None = None) -> tuple[float, float]:
        t = self.lead_time() if lead_s is None else max(0.0, lead_s)
        x = self.x[0, 0] + self.x[2, 0] * t + 0.5 * self.x[4, 0] * t * t
        y = self.x[1, 0] + self.x[3, 0] * t + 0.5 * self.x[5, 0] * t * t
        return float(x), float(y)

    def adaptive_gate_px(self, dt: float, confidence: float) -> float:
        speed = float(np.hypot(self.x[2, 0], self.x[3, 0]))
        accel = float(np.hypot(self.x[4, 0], self.x[5, 0]))
        expected_motion = speed * dt + 0.5 * accel * dt * dt
        confidence_margin = 35.0 * max(0.0, min(1.0, confidence))
        gate_px = self.gate_min_px + expected_motion * self.gate_speed_scale + confidence_margin
        return float(max(self.gate_min_px, min(self.gate_max_px, gate_px)))

    def update_velocity_from_measurement(self, detection: Detection, timestamp_ms: float) -> None:
        if self.prev_measurement is None:
            self.prev_measurement = (detection.x, detection.y, timestamp_ms)
            return

        prev_x, prev_y, prev_t = self.prev_measurement
        dt = max(0.001, (timestamp_ms - prev_t) / 1000.0)
        measured_vx = (detection.x - prev_x) / dt
        measured_vy = (detection.y - prev_y) / dt
        if self.prev_measured_velocity is None:
            measured_ax = (measured_vx - self.x[2, 0]) / dt
            measured_ay = (measured_vy - self.x[3, 0]) / dt
        else:
            prev_vx, prev_vy = self.prev_measured_velocity
            measured_ax = (measured_vx - prev_vx) / dt
            measured_ay = (measured_vy - prev_vy) / dt
        self.x[2, 0] = self.x[2, 0] * (1.0 - self.velocity_blend) + measured_vx * self.velocity_blend
        self.x[3, 0] = self.x[3, 0] * (1.0 - self.velocity_blend) + measured_vy * self.velocity_blend
        self.x[4, 0] = self.x[4, 0] * (1.0 - self.acceleration_blend) + measured_ax * self.acceleration_blend
        self.x[5, 0] = self.x[5, 0] * (1.0 - self.acceleration_blend) + measured_ay * self.acceleration_blend
        self.clamp_velocity()
        self.clamp_acceleration()
        self.prev_measurement = (detection.x, detection.y, timestamp_ms)
        self.prev_measured_velocity = (measured_vx, measured_vy)

    def prediction_detection(self, frame_width: int, frame_height: int) -> Detection:
        if not self.initialized:
            return Detection()
        if self.missing_frames > self.max_predict_frames or self.last_width <= 0 or self.last_height <= 0:
            return Detection()
        confidence = max(0.05, 0.40 * (1.0 - self.missing_frames / (self.max_predict_frames + 1)))
        forecast_x, forecast_y = self.forecast_position()
        return Detection(
            x=float(np.clip(forecast_x, 0, max(1, frame_width))),
            y=float(np.clip(forecast_y, 0, max(1, frame_height))),
            width=self.last_width,
            height=self.last_height,
            confidence=confidence,
            detected=True,
            accepted=False,
            predicted=True,
        )

    def update_pending_reset(self, detection: Detection) -> bool:
        if self.missing_frames < self.min_missing_before_reset:
            self.pending_reset = None
            self.pending_reset_count = 0
            return False

        if detection.confidence < self.hard_reset_conf:
            self.pending_reset = None
            self.pending_reset_count = 0
            return False

        if self.pending_reset is None:
            self.pending_reset = detection
            self.pending_reset_count = 1
            return self.pending_reset_count >= self.reset_confirm_frames

        distance = float(np.hypot(detection.x - self.pending_reset.x, detection.y - self.pending_reset.y))
        if distance <= max(80.0, min(220.0, self.gate_base_px)):
            self.pending_reset = detection
            self.pending_reset_count += 1
        else:
            self.pending_reset = detection
            self.pending_reset_count = 1
        return self.pending_reset_count >= self.reset_confirm_frames

    def predict(self, dt: float) -> None:
        """卡尔曼预测步骤：根据时间 dt 将状态向前推进。

        使用恒加速度运动模型：
        x_new = x + vx*dt + 0.5*ax*dt^2, vx_new = vx + ax*dt。
        """
        dt2 = dt * dt
        # 状态转移矩阵：恒加速度模型，适合足球射门、弹跳和弧线轨迹。
        f = np.array(
            [
                [1.0, 0.0, dt, 0.0, 0.5 * dt2, 0.0],
                [0.0, 1.0, 0.0, dt, 0.0, 0.5 * dt2],
                [0.0, 0.0, 1.0, 0.0, dt, 0.0],
                [0.0, 0.0, 0.0, 1.0, 0.0, dt],
                [0.0, 0.0, 0.0, 0.0, 1.0, 0.0],
                [0.0, 0.0, 0.0, 0.0, 0.0, 1.0],
            ],
            dtype=np.float64,
        )

        # 预测状态：x = F * x
        self.clamp_velocity()
        self.clamp_acceleration()
        self.x = f @ self.x
        # 预测协方差：P = F * P * F^T
        self.p = f @ self.p @ f.T

        # 过程噪声保持保守，避免单帧误检把速度/加速度状态迅速放大。
        self.p[0, 0] += 25.0 * dt2 + 1.0
        self.p[1, 1] += 25.0 * dt2 + 1.0
        self.p[2, 2] += 90.0 * dt + 1.0
        self.p[3, 3] += 90.0 * dt + 1.0
        self.p[4, 4] += 260.0 * dt + 1.0
        self.p[5, 5] += 260.0 * dt + 1.0
        self.clamp_velocity()
        self.clamp_acceleration()

    def update(self, measured_x: float, measured_y: float, confidence: float) -> None:
        """卡尔曼更新步骤：用 YOLO 检测结果修正状态估计。

        confidence 用于动态调整测量噪声——置信度越低，噪声越大，
        滤波器就越不信任该测量值。
        """
        # 钳制置信度到 [0, 1]
        confidence = max(0.0, min(1.0, confidence))

        # 测量噪声根据置信度动态计算：
        # - 高置信度（1.0）→ 噪声 ~12 → 滤波器更信任测量
        # - 低置信度（0.0）→ 噪声 ~132 → 滤波器更依赖预测
        # 8.0 的噪声下限防止滤波器过度锁定到单个噪声检测结果上。
        measurement_noise = max(18.0, 170.0 * (1.0 - confidence) + 18.0)

        # 观测矩阵：只观测位置 (x, y)，速度和加速度由时间序列推断。
        h = np.array(
            [
                [1.0, 0.0, 0.0, 0.0, 0.0, 0.0],
                [0.0, 1.0, 0.0, 0.0, 0.0, 0.0],
            ],
            dtype=np.float64,
        )

        # 测量噪声协方差矩阵（对角线，假设 x/y 独立同分布）
        r = np.eye(2, dtype=np.float64) * measurement_noise

        # 测量向量
        z = np.array([[measured_x], [measured_y]], dtype=np.float64)

        # 标准卡尔曼更新公式：
        # 新息 = 测量值 - 预测测量值
        innovation = z - h @ self.x
        # 新息协方差 S = H * P * H^T + R
        s = h @ self.p @ h.T + r
        # 卡尔曼增益 K = P * H^T * S^{-1}
        k = self.p @ h.T @ np.linalg.inv(s)
        # 更新状态：x = x + K * innovation
        self.x = self.x + k @ innovation
        # 更新协方差：P = (I - K * H) * P
        self.p = (np.eye(6, dtype=np.float64) - k @ h) @ self.p
        self.clamp_velocity()
        self.clamp_acceleration()

    def smooth(self, detection: Detection, timestamp_ms: float, frame_width: int, frame_height: int) -> Detection:
        """对单帧检测结果进行卡尔曼平滑。

        返回一个新的 Detection，其位置经过卡尔曼滤波平滑。
        - 如果当前帧未检测到目标：仅执行预测步骤，返回原 detection（detected=False）
        - 如果当前帧检测到目标且滤波器未初始化：用该检测值初始化
        - 如果当前帧检测到目标且滤波器已初始化：先预测再更新，返回平滑后的位置
        """
        if not detection.detected:
            # 没有检测到目标时，仅推进预测，继续沿最后已知轨迹估计位置。
            if self.initialized:
                # 钳制 dt 到 [0.001, 0.25] 秒，避免帧间隔过大（如视频卡顿）
                # 或过小（如同帧重复）导致预测退化。
                dt = max(0.001, min(0.25, (timestamp_ms - self.last_timestamp_ms) / 1000.0))
                self.predict(dt)
                self.last_timestamp_ms = timestamp_ms
                self.missing_frames += 1
                return self.prediction_detection(frame_width, frame_height)
            return detection

        if detection.confidence < self.min_update_conf:
            # 低置信框最容易来自误识别。拒绝更新，但继续输出短时运动预测，
            # 让绿色/黄色框保持连续，又不被低置信 raw 框拖偏。
            if self.initialized:
                dt = max(0.001, min(0.25, (timestamp_ms - self.last_timestamp_ms) / 1000.0))
                self.predict(dt)
                self.last_timestamp_ms = timestamp_ms
                self.missing_frames += 1
                return self.prediction_detection(frame_width, frame_height)
            return Detection()

        if not self.initialized:
            # 首次检测到目标，直接初始化滤波器。
            self.reset(detection.x, detection.y, timestamp_ms)
            self.last_width = detection.width
            self.last_height = detection.height
            detection.accepted = True
        else:
            # 正常流程：先预测再更新。
            # 钳制 dt 避免帧间隔过大或过小导致预测退化。
            dt = max(0.001, min(0.25, (timestamp_ms - self.last_timestamp_ms) / 1000.0))
            self.predict(dt)

            predicted_x = float(self.x[0, 0])
            predicted_y = float(self.x[1, 0])
            distance = float(np.hypot(detection.x - predicted_x, detection.y - predicted_y))
            legacy_gate_px = self.gate_base_px + self.gate_conf_scale_px * detection.confidence
            gate_px = min(legacy_gate_px, self.adaptive_gate_px(dt, detection.confidence))
            if distance > gate_px:
                if self.update_pending_reset(detection):
                    # 高置信大跳变连续出现，认为是重新捕获真球，再重置。
                    self.reset(detection.x, detection.y, timestamp_ms)
                    self.last_width = detection.width
                    self.last_height = detection.height
                    detection.accepted = True
                else:
                    # 大跳变未连续确认，不更新滤波器，避免绿色框被单帧误检拖走。
                    self.missing_frames += 1
                    self.last_timestamp_ms = timestamp_ms
                    return self.prediction_detection(frame_width, frame_height)
            else:
                self.missing_frames = 0
                self.pending_reset = None
                self.pending_reset_count = 0
                detection.accepted = True
                self.update(detection.x, detection.y, detection.confidence)
                self.update_velocity_from_measurement(detection, timestamp_ms)
                self.last_width = detection.width
                self.last_height = detection.height
                self.last_timestamp_ms = timestamp_ms

        # 构建平滑后的检测结果：
        # - 位置使用卡尔曼估计值的短时间前瞻点，并钳制到画面范围内
        # - 宽高沿用原始检测值（卡尔曼滤波只平滑位置）
        # - 置信度按 0.85/0.15 比例向 1.0 混合，体现卡尔曼先验的贡献
        forecast_x, forecast_y = self.forecast_position()
        smoothed = Detection(
            x=float(np.clip(forecast_x, 0, max(1, frame_width))),
            y=float(np.clip(forecast_y, 0, max(1, frame_height))),
            width=detection.width,
            height=detection.height,
            # 将原始置信度向 1.0 混合（0.85/0.15 比例），
            # 使平滑后的输出体现卡尔曼先验而非直接回显原始置信度。
            confidence=float(max(0.0, min(1.0, detection.confidence * 0.85 + 0.15))),
            detected=True,
            accepted=detection.accepted,
            predicted=False,
        )
        return smoothed


def best_ball_detection(result) -> Detection:
    """从 YOLO 推理结果中取出置信度最高的足球检测。

    result: ultralytics 单帧推理结果（Results 对象）。
    返回: Detection 数据类，如果无检测框则 detected=False。
    """
    boxes = result.boxes
    # 如果当前帧没有任何检测框，返回空检测
    if boxes is None or len(boxes) == 0:
        return Detection()

    # 取置信度最高的那个框作为足球检测结果
    confidences = boxes.conf.detach().cpu().numpy()
    best_index = int(np.argmax(confidences))
    xyxy = boxes.xyxy[best_index].detach().cpu().numpy()
    x1, y1, x2, y2 = [float(v) for v in xyxy]

    # 转换为 Detection 格式：中心点 + 宽高
    return Detection(
        x=(x1 + x2) / 2.0,           # 中心 x
        y=(y1 + y2) / 2.0,           # 中心 y
        width=max(0.0, x2 - x1),     # 宽度（保证非负）
        height=max(0.0, y2 - y1),    # 高度（保证非负）
        confidence=float(confidences[best_index]),
        detected=True,
        accepted=False,
    )


def draw_detection(frame, detection: Detection, color: tuple[int, int, int], label: str) -> None:
    """在帧上绘制检测框和标签。

    - frame: OpenCV 图像（原地修改）
    - detection: 检测结果
    - color: BGR 颜色元组
    - label: 标签文字（如 "raw" 或 "kalman"）
    """
    if not detection.detected:
        return

    # 由中心坐标和宽高还原左上/右下角坐标
    x1 = int(round(detection.x - detection.width / 2.0))
    y1 = int(round(detection.y - detection.height / 2.0))
    x2 = int(round(detection.x + detection.width / 2.0))
    y2 = int(round(detection.y + detection.height / 2.0))

    # 绘制边界框
    thickness = 1 if detection.predicted else 2
    cv2.rectangle(frame, (x1, y1), (x2, y2), color, thickness)
    # 绘制中心点
    cv2.circle(frame, (int(round(detection.x)), int(round(detection.y))), 4, color, -1)
    # 绘制标签和置信度（放在框上方，带边缘保护防止出界）
    cv2.putText(
        frame,
        f"{label} {detection.confidence:.2f}",
        (max(0, x1), max(20, y1 - 8)),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.55,
        color,
        2,
        cv2.LINE_AA,
    )


def draw_dashed_line(
    frame,
    p1: tuple[int, int],
    p2: tuple[int, int],
    color: tuple[int, int, int],
    dash_length: int = 10,
    gap_length: int = 7,
) -> None:
    """绘制虚线，用于连接 YOLO 原始点和平滑/预测点。"""
    x1, y1 = p1
    x2, y2 = p2
    dx = x2 - x1
    dy = y2 - y1
    distance = float(np.hypot(dx, dy))
    if distance < 1.0:
        return

    step = dash_length + gap_length
    steps = max(1, int(distance // step) + 1)
    ux = dx / distance
    uy = dy / distance
    for i in range(steps):
        start = i * step
        end = min(start + dash_length, distance)
        if start >= distance:
            break
        sx = int(round(x1 + ux * start))
        sy = int(round(y1 + uy * start))
        ex = int(round(x1 + ux * end))
        ey = int(round(y1 + uy * end))
        cv2.line(frame, (sx, sy), (ex, ey), color, 1, cv2.LINE_AA)


def draw_raw_to_kalman_link(frame, raw: Detection, smoothed: Detection) -> None:
    """用虚线展示 YOLO 观测点和平滑/预测点之间的偏移。"""
    if not smoothed.detected:
        return
    if smoothed.predicted:
        # 无检测或当前 raw 被门控拒绝时，用短十字标明这是纯运动预测点。
        # 不画 raw->predict 的长虚线，否则误检会制造横跨全屏的视觉噪声。
        cx = int(round(smoothed.x))
        cy = int(round(smoothed.y))
        cv2.drawMarker(frame, (cx, cy), (0, 255, 255), cv2.MARKER_CROSS, 18, 2, cv2.LINE_AA)
    elif raw.detected and smoothed.accepted:
        p1 = (int(round(raw.x)), int(round(raw.y)))
        p2 = (int(round(smoothed.x)), int(round(smoothed.y)))
        draw_dashed_line(frame, p1, p2, (255, 255, 0))


def format_detection(detection: Detection) -> str:
    """格式化检测结果，方便在终端逐帧观察坐标和置信度。"""
    if not detection.detected:
        return "none"
    return (
        f"x={detection.x:.1f}, y={detection.y:.1f}, "
        f"w={detection.width:.1f}, h={detection.height:.1f}, "
        f"conf={detection.confidence:.3f}, "
        f"accepted={int(detection.accepted)}, predicted={int(detection.predicted)}"
    )


def resolve_default_model(training_dir: Path) -> Path:
    """按优先级查找默认模型文件。

    先查训练输出目录，再查统一权重目录，都不存在时返回最后一个候选路径
    （让后续流程自行报 FileNotFoundError）。
    """
    candidates = [
        training_dir / "runs/football/ball_yolov8s_v2/weights/best.pt",
        training_dir / "weights/trained/ball_detector_yolov8s.pt",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    # 所有候选都不存在时返回最后一个，让调用方处理文件不存在的错误
    return candidates[-1]


def parse_args() -> argparse.Namespace:
    """解析命令行参数。

    默认视频文件、模型路径和输出目录均相对于本脚本所在目录推断。
    """
    base_dir = Path(__file__).resolve().parent      # detect_test/
    training_dir = base_dir.parent                   # training/

    parser = argparse.ArgumentParser(description="Test YOLO ball detection with Kalman smoothing.")
    # 输入视频，默认为脚本同目录下的 football_game3.mp4
    parser.add_argument("--video", type=Path, default=base_dir / "football_game3.mp4")
    # YOLO 模型权重路径，自动查找默认模型
    parser.add_argument("--model", type=Path, default=resolve_default_model(training_dir))
    # 输出目录，保存带标注的视频和轨迹 CSV
    parser.add_argument("--output-dir", type=Path, default=training_dir / "runs/kalman_test")
    # YOLO 推理参数
    parser.add_argument("--conf", type=float, default=0.25, help="YOLO confidence threshold.")
    parser.add_argument("--iou", type=float, default=0.5, help="YOLO IoU threshold for NMS.")
    parser.add_argument("--imgsz", type=int, default=1280, help="YOLO inference image size.")
    parser.add_argument("--device", default="0", help="GPU device index (0 = first GPU, 'cpu' for CPU).")
    # 实时显示窗口默认开启；如果只想导出文件，可传 --no-show。
    parser.add_argument("--show", dest="show", action="store_true", default=True, help="Show real-time visualization window.")
    parser.add_argument("--no-show", dest="show", action="store_false", help="Disable real-time visualization window.")
    # 限制处理帧数，0 表示处理整个视频
    parser.add_argument("--max-frames", type=int, default=0, help="Maximum frames to process (0 = whole video).")
    parser.add_argument("--print-every", type=int, default=1, help="Print coordinates every N frames (0 = disabled).")
    parser.add_argument("--kalman-min-conf", type=float, default=0.35, help="Minimum YOLO confidence used to update Kalman.")
    parser.add_argument("--kalman-gate-base", type=float, default=180.0, help="Base distance gate in pixels.")
    parser.add_argument("--kalman-gate-scale", type=float, default=450.0, help="Extra gate pixels multiplied by confidence.")
    parser.add_argument("--kalman-reset-conf", type=float, default=0.72, help="High-confidence jump threshold for filter reset.")
    parser.add_argument("--kalman-predict-frames", type=int, default=6, help="How many missing-detection frames can show motion prediction.")
    parser.add_argument("--kalman-reset-confirm-frames", type=int, default=2, help="Consecutive high-confidence jump frames required before reset.")
    parser.add_argument("--kalman-reset-min-missing", type=int, default=3, help="Missing/rejected frames required before far-distance reset.")
    parser.add_argument("--kalman-velocity-blend", type=float, default=0.25, help="Blend ratio for measurement-derived velocity.")
    parser.add_argument("--kalman-max-speed", type=float, default=1800.0, help="Maximum predicted speed in pixels/second.")
    parser.add_argument("--kalman-acceleration-blend", type=float, default=0.08, help="Blend ratio for measurement-derived acceleration.")
    parser.add_argument("--kalman-max-accel", type=float, default=3500.0, help="Maximum predicted acceleration in pixels/second^2.")
    parser.add_argument("--kalman-lead-time", type=float, default=0.0, help="Base seconds to forecast Kalman output ahead of YOLO.")
    parser.add_argument("--kalman-max-extra-lead", type=float, default=0.0, help="Additional speed-based forecast seconds at max speed.")
    parser.add_argument("--kalman-gate-min", type=float, default=45.0, help="Minimum adaptive gate in pixels.")
    parser.add_argument("--kalman-gate-max", type=float, default=260.0, help="Maximum adaptive gate in pixels.")
    parser.add_argument("--kalman-gate-speed-scale", type=float, default=2.2, help="Adaptive gate multiplier for expected frame motion.")
    return parser.parse_args()


def main() -> None:
    """主流程：逐帧 YOLO 推理 + 卡尔曼平滑 + 可视化 + 导出 CSV 轨迹。"""
    args = parse_args()

    # 验证输入文件存在
    if not args.model.exists():
        raise FileNotFoundError(f"Model not found: {args.model}")
    if not args.video.exists():
        raise FileNotFoundError(f"Video not found: {args.video}")

    # 创建输出目录和输出文件路径
    args.output_dir.mkdir(parents=True, exist_ok=True)
    output_video = args.output_dir / f"{args.video.stem}_yolo_kalman.mp4"
    output_csv = args.output_dir / f"{args.video.stem}_trajectory.csv"

    # 加载 YOLO 模型
    model = YOLO(str(args.model))

    # 打开输入视频
    capture = cv2.VideoCapture(str(args.video))
    if not capture.isOpened():
        raise RuntimeError(f"Failed to open video: {args.video}")

    # 获取视频元信息，fps 读取失败时回退到 25
    fps = capture.get(cv2.CAP_PROP_FPS) or 25.0
    width = int(capture.get(cv2.CAP_PROP_FRAME_WIDTH))
    height = int(capture.get(cv2.CAP_PROP_FRAME_HEIGHT))

    # 初始化视频写入器，编码格式 mp4v
    writer = cv2.VideoWriter(
        str(output_video),
        cv2.VideoWriter_fourcc(*"mp4v"),
        fps,
        (width, height),
    )
    if not writer.isOpened():
        raise RuntimeError(f"Failed to create output video: {output_video}")

    if args.show:
        cv2.namedWindow("YOLO + Kalman test", cv2.WINDOW_NORMAL)
        cv2.resizeWindow("YOLO + Kalman test", min(width, 1280), min(height, 720))

    # 卡尔曼滤波器实例（跨帧保持状态）
    kalman = BallKalmanFilter(
        min_update_conf=args.kalman_min_conf,
        gate_base_px=args.kalman_gate_base,
        gate_conf_scale_px=args.kalman_gate_scale,
        hard_reset_conf=args.kalman_reset_conf,
        max_predict_frames=args.kalman_predict_frames,
        reset_confirm_frames=args.kalman_reset_confirm_frames,
        min_missing_before_reset=args.kalman_reset_min_missing,
        velocity_blend=args.kalman_velocity_blend,
        max_speed_px_s=args.kalman_max_speed,
        acceleration_blend=args.kalman_acceleration_blend,
        max_accel_px_s2=args.kalman_max_accel,
        prediction_lead_s=args.kalman_lead_time,
        max_extra_lead_s=args.kalman_max_extra_lead,
        gate_min_px=args.kalman_gate_min,
        gate_max_px=args.kalman_gate_max,
        gate_speed_scale=args.kalman_gate_speed_scale,
    )
    frame_index = 0

    # 记录每帧的轨迹点，用于后续分析
    raw_points: list[tuple[float, float]] = []     # YOLO 原始检测位置
    smooth_points: list[tuple[float, float]] = []  # 卡尔曼平滑后位置
    kalman_accepted_count = 0
    kalman_rejected_count = 0
    kalman_predicted_count = 0

    # 写入 CSV 文件：记录每帧原始检测和平滑结果
    with output_csv.open("w", newline="", encoding="utf-8") as csv_file:
        fieldnames = [
            "frame_index",
            "timestamp_ms",
            "raw_detected",
            "raw_confidence",
            "raw_x",
            "raw_y",
            "raw_width",
            "raw_height",
            "kalman_detected",
            "kalman_confidence",
            "kalman_x",
            "kalman_y",
            "kalman_width",
            "kalman_height",
            "kalman_accepted",
            "kalman_predicted",
        ]
        csv_writer = csv.DictWriter(csv_file, fieldnames=fieldnames)
        csv_writer.writeheader()

        # 逐帧处理循环
        while True:
            # 读取一帧，失败则视频结束
            ok, frame = capture.read()
            if not ok:
                break
            # 达到最大帧数限制则停止
            if args.max_frames and frame_index >= args.max_frames:
                break

            # 计算当前帧时间戳（毫秒）
            timestamp_ms = frame_index * 1000.0 / fps

            # YOLO 推理，verbose=False 抑制终端输出
            result = model.predict(
                source=frame,
                conf=args.conf,
                iou=args.iou,
                imgsz=args.imgsz,
                device=args.device,
                verbose=False,
            )[0]

            # 提取置信度最高的足球检测 + 卡尔曼平滑
            raw = best_ball_detection(result)
            smoothed = kalman.smooth(raw, timestamp_ms, width, height)

            # 记录轨迹点
            if raw.detected:
                raw_points.append((raw.x, raw.y))
            if smoothed.detected:
                smooth_points.append((smoothed.x, smoothed.y))
            if raw.detected and smoothed.accepted:
                kalman_accepted_count += 1
            elif raw.detected and not smoothed.detected:
                kalman_rejected_count += 1
            if smoothed.predicted:
                kalman_predicted_count += 1

            # 在帧上绘制检测结果：红色=原始 YOLO，绿色=卡尔曼平滑，黄色=短时预测
            draw_detection(frame, raw, (0, 0, 255), "raw")
            kalman_color = (0, 255, 255) if smoothed.predicted else (0, 255, 0)
            kalman_label = "predict" if smoothed.predicted else "kalman"
            draw_detection(frame, smoothed, kalman_color, kalman_label)
            draw_raw_to_kalman_link(frame, raw, smoothed)
            # 图例
            cv2.putText(
                frame,
                "red=YOLO, green=Kalman, yellow=motion prediction, cyan dashed=accepted offset",
                (20, 32),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.75,
                (255, 255, 255),
                2,
                cv2.LINE_AA,
            )

            # 写入输出视频
            writer.write(frame)

            # 实时显示窗口（按 q 退出）
            if args.show:
                cv2.imshow("YOLO + Kalman test", frame)
                if cv2.waitKey(1) & 0xFF == ord("q"):
                    break

            # 写入 CSV 行：原始和平滑结果并列，便于对比
            csv_writer.writerow(
                {
                    "frame_index": frame_index,
                    "timestamp_ms": round(timestamp_ms, 2),
                    "raw_detected": int(raw.detected),
                    "raw_confidence": round(raw.confidence, 6),
                    "raw_x": round(raw.x, 3),
                    "raw_y": round(raw.y, 3),
                    "raw_width": round(raw.width, 3),
                    "raw_height": round(raw.height, 3),
                    "kalman_detected": int(smoothed.detected),
                    "kalman_confidence": round(smoothed.confidence, 6),
                    "kalman_x": round(smoothed.x, 3),
                    "kalman_y": round(smoothed.y, 3),
                    "kalman_width": round(smoothed.width, 3),
                    "kalman_height": round(smoothed.height, 3),
                    "kalman_accepted": int(smoothed.accepted),
                    "kalman_predicted": int(smoothed.predicted),
                }
            )

            if args.print_every > 0 and frame_index % args.print_every == 0:
                print(
                    f"frame={frame_index:06d} "
                    f"t={timestamp_ms:9.2f}ms "
                    f"raw=[{format_detection(raw)}] "
                    f"kalman=[{format_detection(smoothed)}]"
                )

            frame_index += 1
            # 每 100 帧打印进度
            if frame_index % 100 == 0:
                print(f"Processed {frame_index} frames...")

    # 释放资源
    capture.release()
    writer.release()
    if args.show:
        cv2.destroyAllWindows()

    # 打印摘要统计
    print("YOLO + Kalman test complete")
    print(f"Frames processed: {frame_index}")
    print(f"Raw detections: {len(raw_points)}")
    print(f"Kalman detections: {len(smooth_points)}")
    print(f"Kalman accepted updates: {kalman_accepted_count}")
    print(f"Kalman rejected raw boxes: {kalman_rejected_count}")
    print(f"Kalman motion predictions: {kalman_predicted_count}")
    print(f"Output video: {output_video}")
    print(f"Trajectory CSV: {output_csv}")


if __name__ == "__main__":
    main()
