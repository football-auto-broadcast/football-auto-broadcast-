# Football Ball Detector Training

This directory contains the first-stage training pipeline for the vision module.
The immediate target is a single-class football detector used by
`vision-event-module` to improve focus region generation and event candidates.

## Directory Layout

```text
training/
├── configs/yolo_dataset.yaml
├── configs/yolo_train.yaml
├── datasets/football/
│   ├── videos/cam_01/
│   ├── videos/cam_02/
│   ├── frames_unlabeled/
│   ├── images/{train,val,test}/
│   └── labels/{train,val,test}/
├── raw_data/
├── runs/football/
├── scripts/
└── weights/{pretrained,trained}/
```

## Workflow

1. Put source videos in:

```text
datasets/football/videos/cam_01/
datasets/football/videos/cam_02/
```

2. Extract frames:

```bash
python scripts/extract_frames.py --input datasets/football/videos --output datasets/football/frames_unlabeled --fps 2
```

3. Label football boxes with LabelMe, CVAT, or any tool that can export COCO.
Use only one class name: `ball`.

4. Convert annotations to YOLO labels:

```bash
python scripts/convert_format.py --format labelme --images datasets/football/frames_unlabeled --annotations raw_data/labelme --output-labels raw_data/yolo_labels
```

or:

```bash
python scripts/convert_format.py --format coco --images datasets/football/frames_unlabeled --annotations raw_data/annotations.json --output-labels raw_data/yolo_labels
```

5. Split dataset:

```bash
python scripts/split_dataset.py --images datasets/football/frames_unlabeled --labels raw_data/yolo_labels --output datasets/football
```

6. Visual QA:

```bash
python scripts/visualize_labels.py --images datasets/football/images/train --labels datasets/football/labels/train --output raw_data/qa_train --sample 200
```

7. Train:

```bash
yolo detect train model=../../yolov8s.pt data=configs/yolo_dataset.yaml imgsz=1280 epochs=120 batch=8 project=runs/football name=ball_yolov8s_v1
```

8. Export ONNX:

```bash
yolo export model=runs/football/ball_yolov8s_v1/weights/best.pt format=onnx opset=12 simplify=True imgsz=1280
```

Copy the exported model to:

```text
weights/trained/ball_detector_yolov8s.onnx
```

## Labeling Rules

- Label the visible football only.
- Use a tight rectangle around the ball.
- If the ball is heavily occluded but still visually identifiable, label the visible extent.
- If the ball is not visible, do not create a box; keep an empty YOLO label file.
- Include hard negatives: players' shoes, white sideline marks, goal net knots, corner flags, bright reflections.
- Keep both camera roles balanced. `cam_01` provides tiny ball samples; `cam_02` provides goal-area close-up samples.

## First Acceptance Targets

- Recall for `ball` >= 0.95 on validation clips.
- Precision for `ball` >= 0.70.
- Runtime confidence threshold starts at 0.25.
- Runtime NMS IoU starts at 0.45.
- False negatives near the penalty area are more costly than false positives.
