from ultralytics import YOLO

model = YOLO('best.pt')

model.export(format='onnx', opset=12, imgsz=640, dynamic=False, simplify=False)