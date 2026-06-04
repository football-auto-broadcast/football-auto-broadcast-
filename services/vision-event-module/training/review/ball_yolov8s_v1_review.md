# ball_yolov8s_v1 训练复盘

## 1. 本次训练目标

本次训练的目标是先完成足球检测模型的第一版闭环，验证以下事项：

1. Roboflow 网络数据能否被整理成项目统一的单类 YOLO 数据集。
2. `yolov8s.pt` 能否在本机 `yolo_football` 环境中完成训练。
3. 是否能导出可供 C++ 视觉模块后续接入的 ONNX 模型。
4. 得到一个可用于第一版集成测试的 `ball_detector` 模型检查点。

本次训练不是最终模型训练，主要定位是“网络数据预训练 + 工程链路打通”。

## 2. 数据集情况

原始数据集来自 Roboflow：

```text
raw_data/roboflow/football.v1i.yolov8
```

原始类别：

```text
0 football
1 others
```

项目运行时只需要检测足球，因此训练目标统一为单类：

```text
0 ball
```

为避免 `others` 类污染单类检测器，本次使用脚本进行了类别清洗：

```bash
python scripts/prepare_roboflow_single_class.py --source raw_data/roboflow/football.v1i.yolov8 --output datasets/football/yolo_web_single_class
```

清洗后的数据规模：

```text
train: 118 张图片，216 个 football 框，过滤 63 个 others 框
val:    34 张图片， 49 个 football 框，过滤 13 个 others 框
test:   20 张图片， 37 个 football 框，过滤  8 个 others 框
```

这批数据量偏小，而且全部来自网络数据，不包含当前项目真实部署场景中的工业相机画面。

## 3. 训练配置

训练环境：

```text
Conda env: yolo_football
Ultralytics: 8.4.46
Python: 3.12.13
PyTorch: 2.6.0+cu124
GPU: NVIDIA GeForce RTX 4080 Laptop GPU
```

模型：

```text
YOLOv8s
预训练权重: weights/pretrained/yolov8s.pt
输入尺寸: 1280
batch: 8
epochs: 120
patience: 30
```

训练命令：

```bash
yolo detect train model=weights/pretrained/yolov8s.pt data=configs/yolo_dataset.yaml imgsz=1280 epochs=120 batch=8 workers=4 device=0 optimizer=auto patience=30 cache=False project=runs/football name=ball_yolov8s_v1 exist_ok=True seed=2026
```

训练在第 77 个 epoch 触发 EarlyStopping，最佳模型来自第 47 个 epoch。

## 4. 训练结果

验证集结果：

```text
images: 34
instances: 49
precision: 0.997
recall: 0.857
mAP50: 0.954
mAP50-95: 0.674
```

测试集结果：

```text
images: 20
instances: 37
precision: 0.968
recall: 0.824
mAP50: 0.957
mAP50-95: 0.641
```

模型产物：

```text
weights/trained/ball_detector_yolov8s.pt
weights/trained/ball_detector_yolov8s.onnx
```

训练输出目录：

```text
runs/detect/runs/football/ball_yolov8s_v1
```

## 5. 结果解读

本次模型的 Precision 较高，说明模型在当前测试集上误检不算严重；但 Recall 不足，测试集 Recall 为 `0.824`，低于项目预期的 `0.95`。

这意味着当前模型用于集成测试是可以的，但还不适合作为最终比赛检测模型。实际比赛中足球漏检会直接影响：

1. 主画面关注区域生成。
2. 门前活动判断。
3. 射门和进球候选事件召回。
4. 精彩片段生成的完整性。

对于本项目，漏检比误检更严重。误检还可以通过运动区域、时序平滑和置信度过滤削弱；漏检会导致关键事件完全没有视觉信号。

## 6. 本次训练中的问题

### 6.1 数据量太小

总数据只有 172 张图片，训练集只有 118 张。这对足球这种小目标检测任务明显不足。

### 6.2 数据域不匹配

当前数据是 Roboflow 网络数据，不等价于本项目真实场景：

```text
海康 GigE 工业相机
固定高位广角 cam_01
底线/门前 cam_02
1920x1080@25fps
校园球场
远景小球
运动模糊
球门网和白线干扰
```

这些真实部署域中的难例，目前没有被充分覆盖。

### 6.3 类别清洗是必要的

原始数据集中存在 `others` 类。本项目只训练单类 `ball`，因此已将 `football` 映射为 `ball`，并过滤 `others`。

这个处理是正确的，否则运行时 C++ 侧的 `BallDetector` 需要处理额外类别，会破坏当前模块的简单接口。

### 6.4 Recall 未达标

目标 Recall 是 `0.95`，当前 test recall 是 `0.824`。下一轮必须优先提升召回率，而不是继续追求 precision。

## 7. 当前模型可用范围

当前模型适合：

1. 验证 ONNX 推理链路。
2. 接入 C++ `BallDetector` 做第一版集成。
3. 作为网络数据预训练 baseline。
4. 用于发现真实视频上的漏检和误检样本。

当前模型不适合：

1. 直接作为最终比赛检测器。
2. 独立支撑进球候选检测。
3. 独立支撑主画面裁切。
4. 评估真实部署效果。

## 8. 下一轮数据采集建议

按照“30% 网络数据 + 70% 自采数据”的策略，当前网络数据约 172 张。如果严格按比例，至少需要自采约 400 张有效标注图片。

但考虑到当前 Recall 不足，以及真实场景域差异，建议第一轮自采目标提高到：

```text
800 - 1200 张标注图片
```

推荐分布：

```text
cam_01 高位广角: 400 - 600 张
cam_02 门前/底线: 300 - 500 张
无球负样本: 100 - 200 张
```

重点采集：

1. 远景小球，尤其是 `cam_01` 中低于 15 像素的球。
2. 门前混乱，人群遮挡，球门线附近。
3. 高速射门造成的运动模糊。
4. 白色球鞋、白线、球门网、角旗、反光物等负样本。
5. 阴天、强光、夜间灯光等不同光照。
6. 球在画面边缘或被裁切一部分的情况。
7. 无球画面，避免模型在空场景中乱报。

## 9. 下一轮训练建议

下一轮建议命名：

```text
ball_yolov8s_v2
```

训练策略：

1. 继续使用 `yolov8s.pt` 作为预训练权重。
2. 合并 Roboflow 网络数据和自采数据。
3. 保持单类 `ball`。
4. 提高 hard negative 比例。
5. 保持 `imgsz=1280`，因为足球在广角画面中是小目标。
6. 评估指标优先看 Recall，其次看 mAP50。

建议验收线：

```text
val recall >= 0.93
test recall >= 0.90
真实自采视频抽样 recall >= 0.90
误检能被时序规则过滤
```

最终上线目标仍应保持：

```text
recall >= 0.95
precision >= 0.70
```

## 10. 结论

本次训练成功打通了从 Roboflow 数据清洗、YOLOv8s 训练、测试集验证到 ONNX 导出的完整流程。模型产物已经可以用于 C++ 推理接入和第一版系统集成。

但由于数据量小且缺少自采工业相机场景，当前模型只能作为 baseline。下一阶段最重要的工作不是调参，而是补充自采数据，尤其是 `cam_01` 远景小球和 `cam_02` 门前难例。
