# Windows build verification

The 1.1 public release is built with MSVC/CMake on a GitHub Windows Server 2022 runner. Thirteen native core checks pass, including actual bundled ONNX Runtime inference on generated neutral pixels and the production input loop through an injected sink. Nineteen updater checks pass under Windows PowerShell 5.1, including checksum rejection, ZIP traversal/case collisions, settings/unrelated-file preservation, and rollback after a partial replacement. Reports are included in the native release ZIP. These do not send mouse events to a game.

The earlier 1.0 executable and 1.1 cross-build were also built with MinGW and exercised under Wine on Apple Silicon. Full recorded-body regression checks use real images kept locally for privacy. Native Windows build/tests and Wine recorded-image inference are separate verification scopes; neither is a live Roblox test.

## Controlled checks

`Luxware.exe --self-test REPORT.json` writes a machine-readable report. It checks real monotonic decimal click phases, stall handling, the production INPUT dispatch loop through an injected sink, Hold-mode blocking/release, balanced down/up structures and generated-event tags. It does not send these test events to the operating system or a game.

The test also checks calibrated level-return grace/rate/offset, manual cancellation, edge clipping, model-format rejection, known own-avatar exclusion, appearance-based single-ID recovery, different-body rejection, ambiguity pauses and expiry of current-pixel template continuation. Geometric association observations in these unit checks are explicit test inputs, accompanied by pixel descriptors from actual recorded body crops; they are not presented as new detections.

The packaged **Windows ONNX Runtime CPU DLL** actually evaluates all six recorded body-regression images and a real empty-scene image. Each checked body has a real proposal center inside its manual annotation; the six views also survive filtering with the migrated avatar references. Scores are actual varying object scores rather than constant 100%. The views include the previously reported pillar mistake, jumping/partial-edge bodies and a red hit-flash. They are development/training-source regressions, not independent PvP accuracy evidence. The trained model's small-data scope and earlier match errors remain documented in Models/TRAINING_MODEL_CARD.md.

The exported raw ONNX tensor was compared with PyTorch on 11 correlated checkpoint-validation images, using the same RGB/scaleFill preprocessing. Maximum observed coordinate difference was about 0.00208 pixels in the 640-pixel tensor, and score difference about 0.00000340. This is conversion consistency, not unseen-match accuracy. See ExportParity.json.

The owned Windows interface was rendered offscreen through its actual GDI painting/controls using one real recorded-image inference result. No desktop was captured and no global hooks or input thread were installed in render-test mode. The resulting preview was inspected for the controls, actual model box and displayed values. That visual is explicitly labeled a recorded-image test.

## What remains unverified

Native Windows DXGI/GDI capture, DirectML hardware/driver behavior, real keyboard/mouse-hook delivery, actual SendInput delivery and Roblox's acceptance of it must be tested on a Windows PC. Wine CPU timings are not Windows PC performance estimates. Requested 240 Hz or a successful SendInput call is not evidence of achieved camera tracking. The UI and model test modes deliberately do not inspect the Mac desktop or post mouse/key events.

The port uses a pixel descriptor and current-image correlation, rather than unavailable Apple Vision feature prints. Identical outfits, occlusion, camera changes, tiny bodies and model false positives/misses remain limitations. Level return has no direct knowledge of game pitch. No permanent lock, flawless self exclusion or equivalent-to-reference-video PvP claim is made.

Run `Run-Self-Tests.cmd` on the PC for a local report. The included build-time WindowsTestResults.json records the verified controlled run and host scope. The supplied CMake project is used by the native Windows release workflow; a cross-build script is also provided.

## 1.1 updater and fixes

The updater test script exercises semantic versions, HTTPS/repository URL restrictions, malformed manifests, hash/size rejection, ZIP extraction/traversal/case collision rejection, replacement and rollback after an injected failure. Temporary fixtures contain no running application. See UpdaterTestResults.json for the host and scope; native CI reports are named NativeUpdaterTestResults.json and NativeCoreTestResults.json when available. Full recorded-image regression fixtures are kept out of the public GitHub release for privacy. The repository CI runs --core-test, which explicitly reports recordedImageChecksRun=false. Full private regression runs use --self-test.

Replacing/restarting a running installation, endpoint protection, live capture and real gameplay still require a Windows-PC test. The same model weights are retained; this version makes no new accuracy claim.

## 1.1.1 performance checks

Additional checks compare optimized preprocessing to the scalar RGB bilinear reference across four image sizes, ensure tensor storage remains stable, verify the processing budget, and exercise repeated GDI buffer resizing/release. Reported preprocessing times are repeated generated-image microbenchmarks on the stated host, not game benchmarks. Recorded-body model regressions are rerun locally after the change. Published native reports are generated by the tagged Windows build.

The 1.1.1 pre-release MSVC Windows runner passed 17 core/performance checks and 19 updater checks. In its generated 1600x900 preprocessing microbenchmark (eight repetitions), the scalar reference averaged 13.704 ms and the reusable implementation 4.05797 ms, with maximum tested float error 0. This measures preprocessing only; it is not an end-to-end game FPS result. The local full recorded-image run passed all 27 checks using the Windows inference DLL under Wine.

## 1.1.2 target-loss dispatch

Eight additional regressions drive the production input loop through an injected sink. They cover initial no-player clicking, movement for an explicit current observation, zero movement after loss beyond the former level-return grace period, continued clicks during loss, rejected template/low-confidence/stale observations, and balanced click pairs. Synthetic observations are explicit test inputs, separate from recorded-model checks. Earlier level-return utility regressions remain historical compatibility tests; the production loop no longer calls that utility. Native reports and release workflow identify the actual host and passing counts.

## 1.2.0 observed reference and new tests

The supplied local video was inspected at full-resolution frames 297–310 seconds and gameplay frames through 390 seconds. It visibly shows a black/red compact panel, First/Third Person tabs, hotkey Change control, Hold/Toggle, sensitivity and a 240 Hz movement-frequency setting. Screenshots demonstrate appearance/behavior, not the creator's implementation or learned weights. Luxware uses its own existing detector and retains its branding. The exact hidden Advanced panel and performance equivalence are not asserted.

New regressions verify rejecting an isolated proposal, confirming consecutive current observations, no remembered-box output during loss, and clearing correction on target/perspective changes. Owned hidden-window UI tests exercise actual controls without installing hooks, starting input/capture or writing saved settings. Physical mouse/key delivery and live-game aim quality still require a PC gameplay test. Private recorded regressions continue locally, not as an independent accuracy benchmark.

Self-exclusion continuation has three additional controlled tests: small gradual feature changes at an overlapping current box remain excluded; a different distant proposal remains eligible; and exclusion memory expires. These use explicit synthetic descriptors/boxes to test the guard, not invented neural results or proof of real-world self-recognition accuracy. Real saved-avatar/recorded-body regressions are separate.

## 1.2.2 timestamped camera-input history

New tests cover excluding pre-capture input from compensation, expiring/clearing history, static settling at several capture-to-result delays and moving-target error in an idealized linear-camera simulation. The simulation is explicit controlled test data, not fabricated detector output, game footage or evidence of matching the reference program. Native production dispatch, no-target click behavior, actual ONNX inference and hidden UI controls are checked separately by the existing suites.

The final delay model runs serial capture/inference rather than queueing parallel detections. Its moving case explicitly enables optional lead; static cases leave it off. Further tests check separation of scene motion from accepted camera input, disabled prediction at zero, free-cursor motion without camera subtraction, and resets for new identities/discontinuous jumps. These are controlled control-loop tests, not invented neural detections or live-game accuracy measurements.

## 1.2.3 Snap and reappearance

Production input-loop tests with an injected sink verify a direct 350/200-unit correction despite a configured smoothing value, exactly one movement per producer frame, a new correction for the next frame, and stopping at detection loss. These use explicit controlled observations and never send events to the OS. Tracker tests verify confirmed reacquisition at the opposite viewport edge, pause on equal lookalikes and no silent takeover after identity expiry. Hidden UI tests verify Snap default, disable and response-preset controls. These are not live PvP validation or evidence of 1 ms vision latency.

Migration checks use a dedicated temporary INI file (not user settings) to verify the Snap response migration, unchanged CPS/duty and explicit smooth-mode opt-out persistence. An additional injected-dispatch check decodes the free-cursor absolute event and verifies the expected target point directly, without moving the real cursor.

## 1.3.0 managed profile

The normal application and hidden UI mode enforce one profile; standalone core fixtures remain configurable only inside tests. New checks verify old raw tuning is ignored, camera type/avatar-file rotation remain valid setup state, and the production input loop generates the managed click behavior even when raw settings request hold mode/disabled clicks. No OS events are sent. UI checks now verify tuners/binding/model import controls are absent, old commands cannot alter effective settings, both input types/capture/reset/preview/start-stop remain functional, and the Updates dialog has no tuning controls. Historical tests for configurable modes remain regression coverage for the lower-level library rather than editable shipping UI.

The preset is selected from the existing validated defaults and bounded-lead control tests. It is not a benchmark-proven universal optimum for unknown game sensitivity/hardware or proof of live PvP accuracy. Numeric INI edits are overridden; only necessary perspective and avatar storage metadata are carried forward.
