# LuxwarePlayers 1.3 model card

This is a genuine, locally fine-tuned YOLOv8 nano avatar detector, not a generic human model, manually invented boxes or the reference creator's unavailable private model. It starts from `roblox-character-mid.pt` published by [SantiPT007/roblox-vision](https://github.com/SantiPT007/roblox-vision). The base and derived weights carry AGPL-3.0; see Resources/LuxwarePlayers-LICENSE.txt and the source release's Training/Players.

## Training and operating point

One class, `avatar`, covers visible block-character bodies. The prepared corpus has 78 training, 11 validation and 29 separate test images. Training includes earlier reference footage, cyan-avatar crops and the supplied 10.41.12 PM match, with jumping, close-edge and red hit-flash examples. Negative images include scenery, weapons and HUD. Images are resized to 640 × 640; native Vision uses scaleFill.

The final run restarted from the original inspected checkpoint, not the rejected earlier fine-tune. It used MPS on the M5, AdamW, batch 4, augmentations and validation-based early stopping: 119 epochs completed; epoch 79 selected. Training source, parameters, prepared pixels/labels, original/derived checkpoints, per-epoch metrics and provenance are supplied. No cloud upload or paid service was used.

The model is exported as a float16 Core ML package with decoded coordinates, class labels and NMS. Native CPU/Neural Engine inference was compared with PyTorch on all 11 validation images: maximum normalized coordinate difference about 0.00203 and maximum score difference about 0.01585. This checks export/preprocessing consistency, not generalization.

The 0.40 operating threshold was selected on validation frames before the final test evaluation. At that threshold, those correlated validation frames have 13/13 matched bodies at IoU ≥ 0.5, without an unmatched proposal. These frames share source clips with training; **this is not independent PvP accuracy**. Creator metadata `luxware.confidence = observation` is essential: this export's single-class label confidence is always 1, while its object observation contains the real model score. The app uses that object score and applies its threshold.

## Recorded-footage checks

- The earlier whole 10.49.05 PM match is excluded from this run's training and checkpoint/threshold validation. Across 29 reviewed frames with 17 annotated bodies, 13 bodies have a predicted center inside the body; 11 match at IoU ≥ 0.5, with two unmatched proposals. IoU-based precision is about 84.6% and recall 64.7%. This clip had been reviewed and was used in a discarded earlier experiment, so this is not a blind benchmark or proof of arbitrary match reliability. The final run restarts from the original base weights and includes no images from that match in training or selection.
- In 23 scored snapshots from the supplied 10.41.12 PM development clip, 13/15 annotated bodies have a predicted center inside the body; 12 match at IoU ≥ 0.5, with two unmatched proposals. Two very small bodies remain missed, a partial close-up is poorly bounded, and a green resource block is mistaken for an avatar in one death-camera view. The unlabelable inside-own-avatar spawn-transition frame is excluded. **This clip became training/development data after an earlier fine-tune failed, so these are regression results, not held-out accuracy.**
- The pillar-mistake snapshot now has its box on the above-left opponent; the checked distant, jumping, partial-edge and red hit-flash snapshots pass production-pipeline center checks without target seeding. See PlayerTestResults.json. Exact saved-own-crop rejection and updating-avatar release of a prior strict ID also pass; one crop is not proof of ownership recognition.
- The native owned-window capture test processes actual ScreenCaptureKit pixels through this packaged model and saved-avatar filtering. Its measured result is in PlayerCaptureTestResults.json. It does not send test input to Roblox or establish game camera response.

## Limits

The dataset is small and outfit coverage limited. Unseen characters/maps, tiny bodies, motion blur, strong rotation, close-ups and occlusion can fail. Green resources can resemble green armor. Appearance similarity can reject an opponent or fail to reject the local avatar. Neither this detector nor screen capture proves identity/team membership or sees through solid cover/behind the camera. Geometric IDs can switch at crossings and break after loss. A confidence score is not a calibrated probability or a correctness guarantee.

The app's short Vision continuation tracks current image pixels for at most 0.22 seconds after a neural confirmation; it does not invent hidden targets. Requested FPS/event limits are not achieved-speed claims. Live Roblox acceptance of synthetic input, physical global shortcut delivery and sustained PvP performance remain unverified end to end. The legacy dummy model is separate and documented in BUNDLED_MODEL.md.
