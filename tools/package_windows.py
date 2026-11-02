"""Package a clean, public Windows release. Never includes recordings or avatar presets."""
from pathlib import Path
import argparse, hashlib, json, shutil, zipfile

p = argparse.ArgumentParser()
p.add_argument('--binary-dir', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
args = p.parse_args()
root = Path(__file__).resolve().parents[1]
identity = json.loads((root / 'release.json').read_text())
version = identity['version']
args.output.mkdir(parents=True, exist_ok=True)
stage = args.output / 'Luxware'
if stage.exists(): shutil.rmtree(stage)
stage.mkdir()
for name in ['Luxware.exe', 'onnxruntime.dll', 'onnxruntime_providers_shared.dll', 'DirectML.dll']:
    shutil.copy2(args.binary_dir / name, stage / name)
for name in ['Models', 'Updates', 'Prerequisites']:
    shutil.copytree(root / name, stage / name)
for name in ['release.json', 'README.md', 'START-HERE.txt', 'VALIDATION.md', 'RuntimeSources.json',
             'MODEL-LICENSE.txt', 'ONNXRuntime-LICENSE.txt', 'ONNXRuntime-ThirdPartyNotices.txt',
             'DirectML-LICENSE.txt', 'DirectML-LICENSE-CODE.txt']:
    shutil.copy2(root / name, stage / name)
for name in ['WindowsTestResults.json','UpdaterTestResults.json','NativeCoreTestResults.json','NativeUpdaterTestResults.json','NativeUITestResults.json']:
    if (root / name).exists(): shutil.copy2(root / name, stage / name)
(stage / 'SHA256SUMS.txt').write_text(''.join(
    hashlib.sha256(f.read_bytes()).hexdigest() + '  ' + f.relative_to(stage).as_posix() + '\n'
    for f in sorted(stage.rglob('*')) if f.is_file()))
archive = args.output / f'Luxware-{version}-Windows-x64.zip'
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    for file in sorted(stage.rglob('*')):
        if file.is_file(): z.write(file, 'Luxware/' + file.relative_to(stage).as_posix())
with zipfile.ZipFile(archive) as z: assert z.testzip() is None
release = dict(identity, url=f'https://github.com/10tenni/luxware-windows/releases/download/v{version}/{archive.name}',
               sha256=hashlib.sha256(archive.read_bytes()).hexdigest(), bytes=archive.stat().st_size)
(args.output / 'latest.json').write_text(json.dumps(release, indent=2) + '\n')
print(archive)
