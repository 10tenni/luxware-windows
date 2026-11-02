# Windows build verification

The actual x86-64 Windows PE executable was cross-compiled with MinGW-w64 GCC 16.2. It includes an embedded Windows/DPI manifest, app icon and version resource. The same executable was run with its packaged Windows ONNX Runtime DLL under Wine 11 on the Apple Silicon Mac. That is compatibility testing, **not native Windows or live Roblox testing**.

## Controlled checks

`Luxware.exe --self-test REPORT.json` writes a machine-readable report. It checks real monotonic decimal click phases, stall handling, the production INPUT dispatch loop through an injected sink, Hold-mode blocking/release, balanced down/up structures and generated-event tags. It does not send these test events to the operating system or a game.

The test also checks calibrated level-return grace/rate/offset, manual cancellation, edge clipping, model-format rejection, known own-avatar exclusion, appearance-based single-ID recovery, different-body rejection, ambiguity pauses and expiry of current-pixel template continuation. Geometric association observations in these unit checks are explicit test inputs, accompanied by pixel descriptors from actual recorded body crops; they are not presented as new detections.

The packaged **Windows ONNX Runtime CPU DLL** actually evaluates all six recorded body-regression images and a real empty-scene image. Each checked body has a real proposal center inside its manual annotation; the six views also survive filtering with the migrated avatar references. Scores are actual varying object scores rather than constant 100%. The views include the previously reported pillar mistake, jumping/partial-edge bodies and a red hit-flash. They are development/training-source regressions, not independent PvP accuracy evidence. The trained model's small-data scope and earlier match errors remain documented in Models/TRAINING_MODEL_CARD.md.

The exported raw ONNX tensor was compared with PyTorch on 11 correlated checkpoint-validation images, using the same RGB/scaleFill preprocessing. Maximum observed coordinate difference was about 0.00208 pixels in the 640-pixel tensor, and score difference about 0.00000340. This is conversion consistency, not unseen-match accuracy. See ExportParity.json.

The owned Windows interface was rendered offscreen through its actual GDI painting/controls using one real recorded-image inference result. No desktop was captured and no global hooks or input thread were installed in render-test mode. The resulting preview was inspected for the controls, actual model box and displayed values. That visual is explicitly labeled a recorded-image test.

## What remains unverified

Native Windows DXGI/GDI capture, DirectML hardware/driver behavior, real keyboard/mouse-hook delivery, actual SendInput delivery and Roblox's acceptance of it must be tested on a Windows PC. Wine CPU timings are not Windows PC performance estimates. Requested 240 Hz or a successful SendInput call is not evidence of achieved camera tracking. The UI and model test modes deliberately do not inspect the Mac desktop or post mouse/key events.

The port uses a pixel descriptor and current-image correlation, rather than unavailable Apple Vision feature prints. Identical outfits, occlusion, camera changes, tiny bodies and model false positives/misses remain limitations. Level return has no direct knowledge of game pitch. No permanent lock, flawless self exclusion or equivalent-to-reference-video PvP claim is made.

Run `Run-Self-Tests.cmd` on the PC for a local report. The included build-time WindowsTestResults.json records the verified controlled run and host scope. The native Visual Studio/CMake project is supplied; the delivered binary was built with the cross-build script, not Visual Studio.

## 1.1 updater and fixes

The updater test script exercises semantic versions, HTTPS/repository URL restrictions, malformed manifests, hash/size rejection, ZIP extraction/traversal/case collision rejection, replacement and rollback after an injected failure. Temporary fixtures contain no running application. See UpdaterTestResults.json for the host and scope; native CI reports are named NativeUpdaterTestResults.json and NativeCoreTestResults.json when available. Full recorded-image regression fixtures are kept out of the public GitHub release for privacy. The repository CI runs --core-test, which explicitly reports recordedImageChecksRun=false. Full private regression runs use --self-test.

Live Windows installation of a running app, restart behavior, endpoint protection and real gameplay still require a Windows-PC test. The same model weights are retained; this version makes no new accuracy claim.
