# ball_yolov8s_v1

## Environment

- Conda env: `yolo_football`
- Ultralytics: `8.4.46`
- Python: `3.12.13`
- PyTorch: `2.6.0+cu124`
- GPU: NVIDIA GeForce RTX 4080 Laptop GPU

## Dataset

Source:

```text
raw_data/roboflow/football.v1i.yolov8
```

Roboflow source classes:

```text
0 football
1 others
```

Runtime detector target:

```text
0 ball
```

Preparation:

```bash
python scripts/prepare_roboflow_single_class.py --source raw_data/roboflow/football.v1i.yolov8 --output datasets/football/yolo_web_single_class
```

Prepared dataset:

```text
train: 118 images, 216 kept football boxes, 63 dropped others boxes
val:    34 images,  49 kept football boxes, 13 dropped others boxes
test:   20 images,  37 kept football boxes,  8 dropped others boxes
```

## Training Command

```bash
yolo detect train model=weights/pretrained/yolov8s.pt data=configs/yolo_dataset.yaml imgsz=1280 epochs=120 batch=8 workers=4 device=0 optimizer=auto patience=30 cache=False project=runs/football name=ball_yolov8s_v1 exist_ok=True seed=2026
```

Training stopped early at epoch 77. Best checkpoint was epoch 47.

## Validation Metrics

Validation split:

```text
images: 34
instances: 49
precision: 0.997
recall: 0.857
mAP50: 0.954
mAP50-95: 0.674
```

Test split:

```text
images: 20
instances: 37
precision: 0.968
recall: 0.824
mAP50: 0.957
mAP50-95: 0.641
```

## Artifacts

Training run:

```text
runs/detect/runs/football/ball_yolov8s_v1
```

Runtime artifacts:

```text
weights/trained/ball_detector_yolov8s.pt
weights/trained/ball_detector_yolov8s.onnx
```

## Notes

- Current dataset is only the Roboflow web-data portion.
- The target recall is 0.95, but test recall is 0.824. This model is usable as a first integration checkpoint, not as the final match detector.
- Next improvement should add the 70% self-collected cam_01/cam_02 data, especially tiny-ball wide-view frames and goal-area hard negatives.
