# Luxware for Windows 1.2.2 (x64)

This is a compiled native Windows application, using the same genuinely trained player weights as the Mac app. It is a C++/Win32 port with actual DXGI Desktop Duplication/GDI screen capture, ONNX Runtime inference and standard Windows SendInput. It is not a renamed Mac bundle, an aim trainer, a video playback system or invented detection boxes. No Python installation, API key, cloud upload or account is needed to run the packaged executable. The updater uses the built-in Windows PowerShell 5.1 and contacts GitHub for releases.

## Start

Extract the entire ZIP on a Windows 10 version 2004+ or Windows 11 x64 PC and run **Luxware.exe**. Keep the DLLs, Models and other folders beside it. Choose Roblox, press **Start scan**, then return to the game. Capture deliberately runs only while the selected game window is foreground; the preview holds its last frame while Luxware is foreground. A second monitor can show the preview while the game remains focused.

Existing Windows settings and saved avatar views remain in your Local AppData folder. Public downloads contain no personal preset or recordings. A private ZIP may include a first-run preset; Mac Command modifiers become Windows Control. On a new PC without a preset, mark your own avatar once before aiming. Update your avatar if the outfit changes: show yourself in third person, scan briefly, return to Luxware, then use **Update my avatar** and drag around your body. This is a saved self-exclusion reference, not a repeatedly selected opponent.

Press the displayed binding in the game, or click **Activate input** and return to the game. **Toggle** is press-on/press-off. **Hold** requires a physically held binding, including when manually armed first. Click **Bind**, then press a key or left/right/middle/Mouse 4/Mouse 5 button inside Luxware to change it. Modifiers are supported; reset and emergency chords are reserved. Generated and injected events are ignored by the binding monitor, preventing the auto clicker from toggling itself. Vendor-remapped buttons depend on what the mouse driver delivers.

- **Ctrl–Alt–Esc / STOP ALL:** stop input and capture.
- **Ctrl–Shift–R / Reset:** release the target lock and stop input; reactivate to acquire anew.
- **First person:** relative mouse correction. **Third person:** move the free desktop cursor onto the detected body.
- **Auto clicker:** genuine left/right down/up input, decimal CPS, 1–99% duty and measured generated CPS. Turn Aim assist off for clicker-only use; that mode captures current pixels without spending inference time and labels the counter Capture FPS.

## Actual vision and tracking

The bundled `Models/LuxwarePlayers.onnx` is exported from the Mac player fine-tune. RGB pixels are resized to 640 × 640 with scaleFill semantics and divided by 255. The single-class YOLO output contains decoded box coordinates and actual object scores. The app applies the selected confidence threshold (initially 0.40) and NMS IoU 0.45. Generic human classifiers and incompatible multi-class/raw model formats are not accepted as substitutes.

Normal model observations appear as red **ML** boxes with measured scores. The chosen body has a yellow outline and a correction line. IDs, velocity estimates, bounded prediction, acquisition FOV, confidence and processing/movement limits are implemented. One entity starts a fresh lock on enable, reserves its match before other tracks, and permits up to two seconds of appearance-checked recovery from actual current detections. Long loss, lookalike ambiguity and fully missing pixels pause the lock; Reset or off/on selects anew. It does not see through cover or behind the camera.

Apple Vision feature prints are unavailable on Windows. This port computes a real pixel descriptor using spatial RGB, HSV histograms and gradients, for saved-self exclusion and body-appearance matching. Its distance is not equivalent to Apple's feature-print distance. It passes the supplied pose/self regression cases, but similar outfits, backgrounds and unseen poses can still cause rejection or identity mistakes. It cannot guarantee ownership or “never me.”

In 1.1.2, template-only continuation no longer drives the mouse. A current, sufficiently confident neural observation is required for aim movement. Tracking can remember a target ID for reacquisition, but it does not search by rotating the camera when that target is missing.

ONNX Runtime requests DirectML GPU execution when enabled. If unavailable it explicitly reports CPU fallback. Requested FPS/Hz are limits; the displayed rates count work/events that actually happened. Model/capture latency and process CPU usage are measured. CPU percentage is normalized across the PC's logical processors.

## Input and no-player behavior

The input thread uses a monotonic clock, a high-resolution waitable timer where available, separate mouse-down/up phases, overrun handling without catch-up bursts, and tagged standard SendInput packets. Measured CPS counts successful SendInput down calls over a two-second rolling window; it is not the requested CPS or a count of all hardware clicks. At 23.43 CPS and 40% duty the requested interval/down/up phases are approximately 42.68/17.07/25.61 ms; actual phases depend on scheduling. Input is blocked on wrong focus, stale capture, missing held binding, pointer outside the game client, or an unresponsive UI heartbeat. Current-image targets must be fresh for aim movement.

When no eligible current player is detected, the app clears pending aim correction and emits no mouse-movement events. This applies in both camera and free-cursor modes. Automatic level return is removed, including for old saved settings. The activated auto clicker keeps its normal decimal CPS/down-up timing without needing a target. Hold/Toggle, foreground-window, fresh-capture and emergency-stop rules still apply. A missing avatar reference or unavailable detector does not block clicker activation when Auto clicker is enabled; aiming waits until its requirements are satisfied.

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

## 1.1.1 performance changes

Game-friendly processing is enabled by default, including for existing settings files. It caps analysis at the lower of your requested rate and 60 FPS on DirectML or 30 FPS on CPU, and reserves an idle gap of at least 25% of each analysis duration. It never queues old frames. Turn it off in Settings to use your original analysis-rate limit. Actual achieved detection FPS is still measured. Click CPS and the movement-rate setting are independent of this analysis budget.

Bilinear RGB preprocessing reuses its 4.7 MiB tensor and caches resize coordinates. Regression checks compare it with the original scalar preprocessing, including single-pixel, odd-sized and larger images. The model weights, 640-pixel input, confidence and detector format are unchanged. ONNX Runtime thread-pool spinning is explicitly disabled to reduce idle CPU competition; see the [official threading documentation](https://onnxruntime.ai/docs/performance/tune-performance/threading.html).

The UI reuses its drawing buffer. Background preview updates are limited to 5 FPS, while foreground controls/preview update up to 20 FPS; this preview limit is separate from real detection FPS. Minimized redraws are skipped. The 20 Hz input-safety heartbeat remains independent. Idle input polling is reduced to 100 Hz and the active timer is one-shot rather than continuously periodic.

Native test reports include a generated-image preprocessing microbenchmark, maximum pixel error, and resource-reuse checks. Those measurements are not Roblox gameplay FPS or a guarantee of performance on a particular PC.

## 1.1.2 no-player change

No player detected means ordinary auto clicking with a free mouse. Level return and template-only mouse correction are disabled, and obsolete level-return controls are removed. Production-dispatch regressions verify movement for a current observation, stopping after loss even with the old level setting, continued balanced click events, and no movement from stale, weak or template-only observations. Tests use an injected event sink and explicit controlled observations; they do not post events to a game.

## 1.2.0 reference-style controls and confirmation

The visible panel around 5:00–5:07 in the supplied video is recreated as a compact black/red Luxware control panel: First/Third Person tabs, hotkey/Change card, Hold/Toggle buttons, sensitivity and tracking-frequency sliders. The active perspective changes the real input mode; the two tabs currently share the saved binding and tuning values. Original branding, hidden Advanced options and unprovided internals are not claimed to be reproduced. Auto clicker CPS/duty remain visible. Scroll down for one-entity/reset, your avatar reference, capture-window selection and detector controls. Preview expands a real pixel/detection preview beside the compact panel.

A target must now have two matching consecutive model observations before aiming. A missing observation immediately leaves the mouse free while activated clicks continue. A remembered ID alone never produces a box or movement. Target or perspective changes reset accumulated smoothing and correction so a previous target's direction does not carry over. This confirmation reduces single-frame false locks at the cost of waiting for the next analysis frame; it cannot reject every persistent model false positive.

The trained model and saved-self pixel descriptor format are unchanged. Self exclusion now also carries a known own-avatar pixel match across overlapping current proposals for up to 150 ms through small appearance changes. This exclusion-only memory never creates an aiming target; it expires and is cleared on resets/source changes. Ambiguous overlapping lookalikes may be excluded too, and perfect ownership recognition remains impossible from these descriptors alone. The creator's weights/source are not available from the video, and identical PvP accuracy or perfect ownership recognition cannot be promised. Similar outfits, unseen avatars and occlusion remain limitations. No screen images or personal avatar references are uploaded by updates.

Native UI tests create a hidden owned window and exercise real controls/production handlers for mode tabs, activation modes, sliders, decimal fields, capture-state start/stop, preview expansion, scrolling and mouse-button binding storage. They install no hooks, run no input thread and do not write user settings or control a game.

1.2.1 keeps the sensitivity slider scoped to First Person. Third Person shows a free-cursor explanation instead; the camera-only slider is hidden there. Native tab tests verify this visibility together with the actual engine mode.

## 1.2.2 delayed-frame correction

Camera aiming now keeps timestamped history of accepted relative mouse input across detection-frame changes. Before calculating a correction it subtracts movement sent after the current pixels were captured, using the configured sensitivity conversion. Previously, per-frame accounting restarted at each result, which could apply an already-sent correction again when inference delivered delayed pixels. History is bounded to half a second and cleared on focus/input pause, source/client-size changes or leaving camera mode. No-player frames still emit no movement; history never creates a target. Free-cursor mode is unchanged.

This compensates capture-to-result delay under the calibrated linear mouse-to-screen approximation. It is not acknowledgement that the game processed an input packet, automatic game-sensitivity calibration or visibility behind cover. Physical game input acceptance, non-linear camera response and manual camera motion can still affect results. Detection weights and the creator-equivalence limitation remain unchanged.

Tests include an explicitly idealized linear-camera scene with delayed observations, comparing the previous per-frame accounting with timestamped compensation. The JSON pixel-error values describe that controlled model only, not Roblox accuracy or FPS. Static targets must settle with 16.7, 50 and 100 ms observation delays; a moving-target case checks reduced error against the old accounting. Real recorded-image model and no-player dispatch regressions remain separate.

Optional Target lead is now corrected for this app's accepted camera motion when estimating image-space player velocity. In Settings, **Target lead OFF (0 ms)** keeps prediction disabled. A positive value enables measured frame-age compensation plus the chosen extra lead, capped at 100 ms and at most 60 pixels (further limited by current body size). It resets for a new/missing target, view/source/size changes, stale frames or discontinuous jumps. It adjusts only the mouse correction for a current neural observation, not the detected box or confidence. Free-cursor mode uses current screen motion without camera-input subtraction. Old prediction values remain saved, but the earlier uncorrected screen-velocity box shift is replaced by this input-side calculation.

The serial linear-camera moving-target test compares the previous no-lead controller with timestamped compensation and optional lead enabled. It uses a 50 ms inference delay, production processing-budget cadence and smoothed correction. Reported errors are controlled-model results, not live PvP accuracy or evidence that the default lead-off setting tracks moving bodies faster. Camera-history compensation alone targets repeated corrections/oscillation.
