from pathlib import Path
from ultralytics import YOLO

base_dir=Path(__file__).resolve().parent
training_dir= base_dir.parent
def main():
    #model_path=training_dir / "weights/trained/ball_detector_yolov8s.pt"

    model_path = training_dir / "runs/football/ball_yolov8s_v2/weights/best.pt"

    video_path=base_dir / "football_game2.mp4"
    image_path = base_dir / "img.png"

    output_dir=training_dir / "runs/predict_video"
    #output_dir = training_dir / "runs/predict_image"

    if not model_path.exists():
        raise FileNotFoundError(f"找不到模型权重文件: {model_path}")

    if not video_path.exists():
        raise FileNotFoundError(f"找不到测试视频文件: {video_path}")

    model=YOLO(str(model_path))

    results = model.predict(
        source=str(video_path),
        #source=str(image_path),
        conf=0.25,#置信度阈值
        iou=0.5,#框去重阈值:两个框重叠度 > 0.5 → 删掉分数低的那个框
        imgsz=1280,#模型处理时把图片缩放到 1280x1280
        device=0,#使用 第 0 号 GPU
        save=True,#保存画好框的结果视频 / 图片会输出带框的 mp4 /jpg
        save_txt=True,#保存每个目标的 坐标信息到 txt 文件
        save_conf=True,#在 txt 里多存一列置信度分数
        project=str(output_dir),#输出的根目录
        name="football_test",#最终输出文件夹名字：output_dir/football_test/
        exist_ok=True,#如果 football_test 文件夹已经存在，直接覆盖写入
        show=True#是否弹出实时播放窗口
    )

    print("视频检测完成")
    print(f"结果保存在: {output_dir / 'football_test'}")


if __name__ == "__main__":
    main()
