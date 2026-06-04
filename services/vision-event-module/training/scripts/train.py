from pathlib import Path
import argparse
import os

def existing_path(path: Path, description: str) -> Path:
    if not path.exists():
        raise FileNotFoundError(f"找不到{description}: {path}")
    return path

def add_epoch_progress_callbacks(model, total_epochs: int):
    try:
        from tqdm.auto import tqdm
    except ModuleNotFoundError:
        def print_epoch_progress(trainer):
            epoch = int(getattr(trainer, "epoch", 0)) + 1
            metrics = getattr(trainer, "metrics", {}) or {}
            metric_text = format_progress_metrics(metrics)
            suffix = f" | {metric_text}" if metric_text else ""
            print(f"[epoch {epoch}/{total_epochs}]{suffix}")

        model.add_callback("on_fit_epoch_end", print_epoch_progress)
        return

    progress = tqdm(total=total_epochs, desc="Training epochs", unit="epoch")

    def update_epoch_progress(trainer):
        epoch = int(getattr(trainer, "epoch", progress.n)) + 1
        if epoch > progress.n:
            progress.update(epoch - progress.n)
        metrics = getattr(trainer, "metrics", {}) or {}
        postfix = compact_progress_metrics(metrics)
        if postfix:
            progress.set_postfix(postfix)

    def close_epoch_progress(trainer):
        if progress.n < total_epochs:
            progress.update(total_epochs - progress.n)
        progress.close()

    model.add_callback("on_fit_epoch_end", update_epoch_progress)
    model.add_callback("on_train_end", close_epoch_progress)

def compact_progress_metrics(metrics: dict) -> dict:
    interesting_keys = {
        "metrics/precision(B)": "P",
        "metrics/recall(B)": "R",
        "metrics/mAP50(B)": "mAP50",
        "metrics/mAP50-95(B)": "mAP50-95",
        "val/box_loss": "box",
        "train/box_loss": "train_box",
    }
    compact = {}
    for key, label in interesting_keys.items():
        value = metrics.get(key)
        if isinstance(value, (int, float)):
            compact[label] = f"{value:.4f}"
    return compact

def format_progress_metrics(metrics: dict) -> str:
    compact = compact_progress_metrics(metrics)
    return " ".join(f"{key}={value}" for key, value in compact.items())

def main():
    parser = argparse.ArgumentParser(description="Run second-stage YOLO football detector training.")
    parser.add_argument("--data", type=Path, default=None, help="YOLO dataset yaml. Defaults to the merged ball-only dataset.")
    parser.add_argument("--model", type=Path, default=None, help="Initial model weights. Defaults to the first-stage trained weights.")
    parser.add_argument("--epochs", type=int, default=80, help="Training epochs.")
    parser.add_argument("--imgsz", type=int, default=1280, help="Image size.")
    parser.add_argument("--batch", type=int, default=4, help="Batch size.")
    parser.add_argument("--device", default="0")
    parser.add_argument("--workers", type=int, default=4, help="Dataloader workers.")
    parser.add_argument("--fraction", type=float, default=1.0, help="Fraction of training data for smoke tests.")
    parser.add_argument("--name", default="ball_yolov8s_v2", help="Run name under runs/football.")
    parser.add_argument("--no-progress", action="store_true", help="Disable the extra epoch progress bar.")
    args = parser.parse_args()

    module_root = Path(__file__).resolve().parents[1]
    yolo_config_dir = module_root / "runs"
    yolo_config_dir.mkdir(parents=True, exist_ok=True)
    os.environ.setdefault("YOLO_CONFIG_DIR", str(yolo_config_dir))
    os.chdir(module_root)

    data_yaml = args.data or module_root / "raw_data" / "roboflow" / "football_merged_ball_only.v1i.yolov8" / "data.yaml"
    model_path = args.model or module_root / "weights" / "trained" / "ball_detector_yolov8s.pt"
    runs_dir = module_root / "runs" / "football"

    data_yaml = existing_path(data_yaml, "数据配置文件")
    model_path = existing_path(model_path, "模型权重")

    try:
        from ultralytics import YOLO
    except ModuleNotFoundError as exc:
        raise SystemExit("缺少依赖 ultralytics，请先运行: python -m pip install ultralytics") from exc

    model = YOLO(str(model_path))
    if not args.no_progress:
        add_epoch_progress_callbacks(model, args.epochs)

    print("====第二次训练配置====")
    print(f"data: {data_yaml}")
    print(f"model: {model_path}")
    print(f"epochs: {args.epochs}")
    print(f"imgsz: {args.imgsz}")
    print(f"batch: {args.batch}")
    print(f"device: {args.device}")
    print(f"workers: {args.workers}")
    print(f"fraction: {args.fraction}")
    print(f"output: {runs_dir / args.name}")

    model.train(
        data=str(data_yaml),
        imgsz=args.imgsz,
        epochs=args.epochs,
        batch=args.batch,
        device=args.device,
        workers=args.workers,
        fraction=args.fraction,
        project=str(runs_dir),
        name=args.name,
        exist_ok=True,
    )

    print("=====================训练完成============================")

if __name__ == "__main__":
    main()
