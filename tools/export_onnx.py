"""Export the provided, locally generated player checkpoint; not a third-party script."""
from pathlib import Path
import os,shutil,json,hashlib
os.environ['WANDB_MODE']='disabled'
os.environ['WANDB_DISABLED']='true'
from ultralytics import YOLO
root=Path(__file__).resolve().parents[1]
checkpoint=root/'Training/Players/PlayerCheckpoint.pt'
model=YOLO(str(checkpoint))
output=Path(model.export(format='onnx',imgsz=640,opset=17,nms=False,simplify=False,dynamic=False,device='cpu'))
(root/'Models').mkdir(exist_ok=True)
shutil.copy2(output,root/'Models/LuxwarePlayers.onnx')
print('ONNX export complete. Native application uses scaleFill RGB /255, raw object score, confidence .40 and NMS .45.')
