from ultralytics import YOLO
from pathlib import Path



def main():
    project_root=Path(__file__).resolve().parents[2]

    data_yaml=project_root / "training" / "configs" / "yolo_train.yaml"
    runs_dir =project_root / "training" / "runs" / "football"

    if not data_yaml.exists():
        raise FileNotFoundError(f"找不到配置文件：{data_yaml}")
    
    model=YOLO("yolov8s.pt")

    model.train(
        data=str(data_yaml),
        imgsz=1280,
        epochs=3,#训练轮数
        batch=4,#一次喂给模型 4 张图片
        device=0,
        project=str(runs_dir),
        name="test_run",
    )

    print("测试版训练完成")

if __name__ == "__main__":
    main()