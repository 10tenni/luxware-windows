from pathlib import Path
import os
os.environ['PYTORCH_ENABLE_MPS_FALLBACK']='1'
os.environ['WANDB_MODE']='disabled';os.environ['WANDB_DISABLED']='true'
import torch,importlib,json
from ultralytics import YOLO
root=Path(__file__).resolve().parent
checkpoint=root/'BaseCheckpoint.pt'
allowed={'ultralytics.nn.modules.block.C2f','torch.nn.modules.linear.Identity','ultralytics.nn.modules.conv.Concat','torch.nn.modules.container.Sequential','ultralytics.nn.tasks.DetectionModel','ultralytics.nn.modules.conv.Conv','torch.nn.modules.batchnorm.BatchNorm2d','ultralytics.nn.modules.block.SPPF','ultralytics.nn.modules.block.Bottleneck','torch.nn.modules.pooling.MaxPool2d','torch.nn.modules.activation.SiLU','torch.nn.modules.container.ModuleList','ultralytics.nn.modules.head.Detect','torch.nn.modules.upsampling.Upsample','torch.nn.modules.conv.Conv2d','ultralytics.nn.modules.block.DFL'}
unsafe=set(torch.serialization.get_unsafe_globals_in_checkpoint(checkpoint));assert unsafe<=allowed,unsafe-allowed
classes=[]
for name in unsafe:
 mod,key=name.rsplit('.',1);classes.append(getattr(importlib.import_module(mod),key))
with torch.serialization.safe_globals(classes):ckpt=torch.load(checkpoint,weights_only=True,map_location='cpu')
net=ckpt['model'].float()
runner=YOLO('yolov8n.yaml');runner.model=net;runner.task='detect';runner.ckpt={'model':net}
runner.overrides.update({'model':'yolov8n.yaml','task':'detect'})
print('STARTING_FROM',str(checkpoint),'CLASSES',net.names,'MPS',torch.backends.mps.is_available(),flush=True)
# Resolve the portable dataset path for this checkout.
(root/'dataset/data.yaml').write_text(f'path: {root / "dataset"}\ntrain: images/train\nval: images/val\ntest: images/test\nnames:\n  0: avatar\n')
result=runner.train(data=str(root/'dataset/data.yaml'),epochs=140,patience=40,imgsz=640,batch=4,device=0 if torch.cuda.is_available() else 'cpu',workers=0,project=str(root/'runs'),name='retrained-player',exist_ok=False,optimizer='AdamW',lr0=0.001,lrf=0.01,warmup_epochs=3,weight_decay=0.0005,amp=False,mosaic=0.8,close_mosaic=15,mixup=0.1,degrees=25,translate=0.15,scale=0.5,perspective=0.0002,fliplr=0.5,flipud=0.05,hsv_h=0.025,hsv_s=0.5,hsv_v=0.4,seed=1122,plots=False,save=True,save_period=20,verbose=False,cache=False)
print('FINISHED',result.results_dict,flush=True)
