# Luxware for Windows 1.1 (x64)

This is a compiled native Windows application, using the same genuinely trained player weights as the Mac app. It is a C++/Win32 port with actual DXGI Desktop Duplication/GDI screen capture, ONNX Runtime inference and standard Windows SendInput. It is not a renamed Mac bundle, an aim trainer, a video playback system or invented detection boxes. No Python installation, API key, cloud upload or account is needed to run the packaged executable. The updater uses the built-in Windows PowerShell 5.1 and contacts GitHub for releases.

## Start

Extract the entire ZIP on a Windows 10 version 2004+ or Windows 11 x64 PC and run **Luxware.exe**. Keep the DLLs, Models and other folders beside it. Choose Roblox, press **Start scan**, then return to the game. Capture deliberately runs only while the selected game window is foreground; the preview holds its last frame while Luxware is foreground. A second monitor can show the preview while the game remains focused.

Existing Windows settings and saved avatar views remain in your Local AppData folder. Public downloads contain no personal preset or recordings. A private ZIP may include a first-run preset; Mac Command modifiers become Windows Control. On a new PC without a preset, mark your own avatar once before aiming. Update your avatar if the outfit changes: show yourself in third person, scan briefly, return to Luxware, then use **Update my avatar** and drag around your body. This is a saved self-exclusion reference, not a repeatedly selected opponent.

Press the displayed binding in the game, or click **Activate input** and return to the game. **Toggle** is press-on/press-off. **Hold** requires a physically held binding, including when manually armed first. Click **Bind**, then press a key or left/right/middle/Mouse 4/Mouse 5 button inside Luxware to change it. Modifiers are supported; reset, level and emergency chords are reserved. Generated and injected events are ignored by the binding monitor, preventing the auto clicker from toggling itself. Vendor-remapped buttons depend on what the mouse driver delivers.

- **Ctrl–Alt–Esc / STOP ALL:** stop input and capture.
- **Ctrl–Shift–R / Reset:** release the target lock and stop input; reactivate to acquire anew.
- **Ctrl–Shift–L / Set level:** use the current view as the first-person level reference. Look level when setting it. A new activation also takes its initial reference from the current view.
- **First person:** relative mouse correction. **Third person:** move the free desktop cursor onto the detected body.
- **Auto clicker:** genuine left/right down/up input, decimal CPS, 1–99% duty and measured generated CPS. Turn Aim assist off for clicker-only use; that mode captures current pixels without spending inference time and labels the counter Capture FPS.

## Actual vision and tracking

The bundled `Models/LuxwarePlayers.onnx` is exported from the Mac player fine-tune. RGB pixels are resized to 640 × 640 with scaleFill semantics and divided by 255. The single-class YOLO output contains decoded box coordinates and actual object scores. The app applies the selected confidence threshold (initially 0.40) and NMS IoU 0.45. Generic human classifiers and incompatible multi-class/raw model formats are not accepted as substitutes.

Normal model observations appear as red **ML** boxes with measured scores. The chosen body has a yellow outline and a correction line. IDs, velocity estimates, bounded prediction, acquisition FOV, confidence and processing/movement limits are implemented. One entity starts a fresh lock on enable, reserves its match before other tracks, and permits up to two seconds of appearance-checked recovery from actual current detections. Long loss, lookalike ambiguity and fully missing pixels pause the lock; Reset or off/on selects anew. It does not see through cover or behind the camera.

Apple Vision feature prints are unavailable on Windows. This port computes a real pixel descriptor using spatial RGB, HSV histograms and gradients, for saved-self exclusion and body-appearance matching. Its distance is not equivalent to Apple's feature-print distance. It passes the supplied pose/self regression cases, but similar outfits, backgrounds and unseen poses can still cause rejection or identity mistakes. It cannot guarantee ownership or “never me.”

A short optional orange **CV** continuation searches current pixels with normalized template correlation and appearance/self checks. It requires a strong match and expires within 0.20 seconds after model confirmation. Its score is correlation, not neural confidence. It is used only when there are no current neural candidates, so it does not override an ambiguous neural single-entity recovery. No invisible target box is fabricated.

ONNX Runtime requests DirectML GPU execution when enabled. If unavailable it explicitly reports CPU fallback. Requested FPS/Hz are limits; the displayed rates count work/events that actually happened. Model/capture latency and process CPU usage are measured. CPU percentage is normalized across the PC's logical processors.

## Input and level return

The input thread uses a monotonic clock, a high-resolution waitable timer where available, separate mouse-down/up phases, overrun handling without catch-up bursts, and tagged standard SendInput packets. Measured CPS counts successful SendInput down calls over a two-second rolling window; it is not the requested CPS or a count of all hardware clicks. At 23.43 CPS and 40% duty the requested interval/down/up phases are approximately 42.68/17.07/25.61 ms; actual phases depend on scheduling. Input is blocked on wrong focus, stale capture, missing held binding, pointer outside the game client, or an unresponsive UI heartbeat. Current-image targets must be fresh for aim movement.

Level return is a **calibrated mouse-delta estimate, not a measured Roblox pitch angle**. In First person it can reverse accumulated vertical mouse deltas after a 0.35 second target-loss grace period. It is bounded in rate, duration and total reference offset; horizontal aim is left alone. A new target interrupts it and manual movement during the return cancels it. Focus loss invalidates the reference. Game sensitivity, pointer acceleration, camera pitch clamps, scripted movement or ignored input can make inversion inaccurate; reset the reference after changes. It remains disabled in Third person, with Aim assist/strength off, or when input is stopped.

Windows SendInput can be blocked by process privilege differences, and a game can ignore accepted synthetic events. The program does not elevate itself, inject code, install an input driver, bypass anti-cheat or claim anti-ban protection. If events are rejected, keep the app and target at ordinary matching privilege levels. If event counters rise but the game ignores them, that is not proof of a working camera controller. No hidden bypass is included.

## Files, requirements and build

The packaged Windows build is unsigned. It includes pinned Microsoft ONNX Runtime DirectML 1.23.0, DirectML 1.15.4, their licenses/notices, the actual trained model and the original optional Microsoft Visual C++ x64 redistributable. If the model status reports a missing runtime dependency, run `Prerequisites/VC_redist.x64.exe` and reopen the app. The app does not run installers automatically. Windows settings and avatar BMP crops live under `%LOCALAPPDATA%\Luxware`; full screen recordings are not saved.

Source is supplied with CMake, a Visual Studio Developer PowerShell build script and a MinGW cross-build script. On Windows, install the Desktop development with C++ workload/CMake and run `build-windows.ps1`, or open the CMake project in Visual Studio. The 1.1 public executable is built by the included Windows CI workflow using MSVC. `build-cross.sh` can also produce an x64 PE executable on the Mac. There is no .NET/Python runtime requirement for the app. Optional model retraining/export scripts and checkpoints are under Training/Players. Personal recorded training images are excluded from the public repository; ONNX export is `tools/export_onnx.py`.

Model/base weights carry AGPL-3.0; see MODEL-LICENSE.txt. Runtime licenses, notices, official download URLs and archive hashes are included. The legacy Apple Create ML dummy model is not bundled in this Windows port. See VALIDATION.md for exactly what was tested, including the important difference between running the Windows inference DLL under Wine and native Windows gameplay validation.

Primary API documentation: [ONNX Runtime C/C++](https://onnxruntime.ai/docs/get-started/with-c.html), [DirectML provider](https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html), [SendInput and privilege limits](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput).

## Updates (1.1 and later)

The official update location is [10tenni/luxware-windows releases](https://github.com/10tenni/luxware-windows/releases). Starting with 1.1, Luxware checks on launch and downloads a newer release in the background. **Updates → Install update** stops input, closes the app, installs and restarts. It does not restart during a game without your click. Automatic checking/downloading can be disabled in Settings; manual checking stays available. Version 1.0 must be replaced manually once because it has no updater.

The feed is HTTPS, limited to this repository. Download length and SHA-256 must match the feed. ZIP traversal, duplicate-case paths, symlinks and oversized expansion are rejected. These checks trust control of the GitHub repository; they are not Authenticode signatures. Updates stage before replacement and roll back on a failed file replacement. A backup of replaced files is kept beside the app. Your settings and avatar references under LocalAppData are untouched. Read-only/Program Files installs fail without elevation and keep the old app. Install into an ordinary writable folder.

Update status and errors appear in Settings. Networking uses a separate local PowerShell process with time/size limits, no profile and a process-only execution-policy override for the shipped updater script; no system policy is changed. Offline checks fail without blocking capture/input. No gameplay images are uploaded.

1.1 also avoids duplicate running input engines, keeps action/error messages visible for eight seconds, and stages just the client rectangle during DXGI readback rather than copying the full monitor. This reduces copied pixels for smaller windows; native capture performance has not been benchmarked. Detector weights and their accuracy limitations are unchanged.
