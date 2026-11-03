# Luxware for Windows 1.4.0 — fixed aim preset + auto crossbow

Extract the complete Windows ZIP and run **Luxware.exe**. Keep the model and DLLs together. No Python installation, cloud account or API key is needed to run the app.

## Use

1. Open Roblox and choose its window under **Capture & your avatar**.
2. Choose **First Person** for relative camera input, or **Third Person** for a free cursor. Both use the same fixed preset; these are input types, not tuning profiles.
3. Click **Start scan**, then focus the selected game. Capture pauses while another app is in front.
4. Press **F6** to toggle aim and auto clicking. F6 also works with movement modifiers held. Press F6 again to stop input.
5. **Ctrl+Alt+Esc / STOP ALL** stops input and capture. Start scanning again before the next activation.
6. **Ctrl+Shift+R / Reset target** clears the one-entity lock and stops input. Reactivate with F6 to acquire a new player.

Your saved avatar images remain in Local AppData. On a new PC or after an outfit change: show your own body in third person, scan briefly, return to Luxware and click **Mark myself / Update my avatar**. Draw a tight box around your body in the frozen preview. This is a saved self-exclusion reference, not a repeated opponent selection. A missing reference leaves ordinary activated clicking available but prevents aiming. The status message explains this.

**Preview** expands actual captured pixels and detections beside the compact panel. It never substitutes an aim trainer or internal game coordinates for image analysis.

## Auto crossbow

Open **Auto crossbow…**. Record the single letter/number that equips your crossbow, then a different letter/number that returns to your normal weapon. These are individual key presses, not a typed sequence. Choose **Recorded toggle key** or **Automatic: active input + visible player**. Crossbow defaults to Off and requires both inventory keys before any switching. Setup stops input; close the panel and reactivate with F6 afterward.

In toggle mode, record any supported keyboard key or Mouse 3/4/5 (default F7), focus the selected game with F6 input active, then press that trigger to toggle cycling. F6, Esc, modifier-only keys and R are reserved for controls; left/right mouse are excluded as triggers to avoid fighting normal gameplay/clicking. Avoid inventory keys that conflict with your game's movement/other controls. In automatic mode, a fresh current eligible model detection starts the timer; a missing/stale target cancels it and resumes ordinary activated clicking. It cannot distinguish actual combat from merely seeing an eligible player.

Timing is fixed: wait **1.10 seconds**, equip, allow a 20 ms settle, press/release left mouse for 17 ms, return, then wait another 1.10 seconds. Regular clicking continues between cycles and pauses briefly during equip/fire/return to prevent unwanted shots. The scheduler never catches up with a burst after a stall. Rejected key-up events are retried; generated keyboard events carry the same injected-event tag as the clicker. Crossbow switching pauses while Ctrl/Alt/Windows keys are held to avoid sending inventory keys as system shortcuts. Switching stops when capture/input stops, UI heartbeat expires, capture becomes stale, the pointer leaves the client or the selected game loses focus. It never types a return key into another foreground app. A stop/focus loss can therefore leave the equipped inventory slot unchanged.

This is **timer-based switching, not cooldown detection**. No game state is read. The 1.10-second delay is your requested interval, not a verified crossbow cooldown. The actual shot/slot acceptance, reload behavior, latency and any game-specific equip requirements need testing in the game. No immediate equip/shot guarantee is made.

## Fixed profile

There are no sliders, editable CPS/duty fields, aim-activation-binding recorder, aim-tuning menu, model importer or manual GPU switch. The Updates panel is read-only apart from checking/installing updates. The application enforces the profile during normal loading, configuration access and input dispatch; older tuning values and edits to numeric INI fields do not override it.

The profile uses full-strength Snap, zero smoothing, one-entity tracking, a fixed F6 toggle, left clicking at 23.43 CPS with 40% duty, the bundled avatar detector, 0.40 detection confidence, a 0.50 acquisition FOV radius, sensitivity conversion 1.0 and bounded optional target lead of 1 ms plus measured frame age. DirectML is requested automatically with explicit CPU fallback. Analysis requests up to 120 FPS with the game-friendly 60 FPS GPU / 30 FPS CPU caps and idle budget; input checks request a 1000 Hz limit. Perspective, crossbow slot/trigger/mode setup and internal avatar-image rotation are preserved as setup data. Custom model paths are ignored.

This is one tested baseline, not a proven optimum for every PC or in-game sensitivity. Locking controls cannot make the model recognize every appearance or make arbitrary game mouse settings equivalent. Actual live PvP accuracy and input acceptance still need testing on the PC.

## Actual vision and control

The bundled trained single-class avatar model receives RGB scaleFill pixels at 640×640, normalized by 255. ONNX Runtime returns actual object scores and decoded boxes; confidence filtering and 0.45-IoU NMS operate on them. Two matching consecutive model observations are required before acquisition. Persistent IDs, pixel appearance, velocity estimates and own-avatar exclusion are based on actual image content. The trained weights and their limited-data provenance are documented in Models/TRAINING_MODEL_CARD.md; this is not a generic human detector.

Snap bypasses easing and sends at most one accepted bounded correction per fresh confirmed observation. Accepted camera input is timestamped and accounted for across delayed frames. Lead estimates compensate for this app's camera input, reset for missing/new/stale/context-changing observations and reject discontinuous jumps. Lead is bounded by age, body size and 60 pixels. It changes the input correction, not the measured detection box or confidence. The linear sensitivity conversion is an approximation, not acknowledgement that the game applied an event.

One-entity appearance memory lasts up to six seconds. A strong unambiguous current proposal can be matched across the viewport after confirmation. Lookalikes pause the lock; expiry requires Reset rather than silently switching people. Saved-self exclusion can continue briefly across overlapping, compatible current proposals. Similar outfits and occlusion remain limitations; perfect identity or ownership recognition is not guaranteed.

**No eligible detected player means no automatic mouse/camera movement. Activated auto clicking continues.** There is no blind camera search or automatic level return. Screen capture cannot see behind the camera or through cover.

Requested 1000 Hz is a **1 ms input-check target**, not 1 ms vision latency or guaranteed scheduling. Actual detection FPS, processing latency, posted CPS/movement events and process CPU usage are measured. CPS counts successful posted downs over a rolling two-second window, not requested CPS or hits registered by the game. Down/up phases use a monotonic clock and do not generate catch-up bursts after stalls. Hold/toggle tuning is removed; the shipped activation behavior is Toggle.

Input is gated by activation, selected-window focus, fresh capture, a responsive UI and the pointer being inside the game client. Generated events are tagged to avoid activation feedback. Standard SendInput can be rejected by privilege differences or ignored by a game; accepted API calls do not prove game camera movement. No elevation, injection, driver or anti-cheat bypass is included.

## Updates, files and build

**Updates → Check for updates → Install update** installs a published release from [10tenni/luxware-windows](https://github.com/10tenni/luxware-windows/releases). Launch checks are automatic. The updater validates repository/HTTPS URLs, byte length, SHA-256 and ZIP contents, stages replacement, preserves unrelated files and retains backups for rollback. Saved settings and avatar crops under `%LOCALAPPDATA%\Luxware` remain local. No recordings or avatar images are uploaded or bundled in public releases. Downloads are unsigned; included runtime licenses/notices and optional `Prerequisites/VC_redist.x64.exe` are from Microsoft. The installer is not run automatically.

The published x64 executable is built with MSVC on Windows CI. Source builds: install Visual Studio Desktop development with C++/CMake, run `python tools/fetch_runtime.py`, then `build-windows.ps1`. The packaged app itself requires no Python. `build-cross.sh` also supports a MinGW cross-build. Model/export sources and licenses are supplied; personal training images/checkpoints are excluded from the public repository. The bundled ONNX file contains the trained weights.

See VALIDATION.md and the Native*TestResults.json reports for exact scope. Controlled tests use injected event sinks and hidden owned UI windows; they do not post input to a live game. The model/runtime carry their included licenses, including AGPL-3.0 for model/application source.
