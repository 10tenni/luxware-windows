"""Reproduce pinned official runtime downloads. The packaged application needs no Python."""
from pathlib import Path
import urllib.request, zipfile, io, hashlib, json
root=Path(__file__).resolve().parents[1]
expected={'microsoft.ml.onnxruntime.directml':'a33ec2382b3c440bab74042a135733bb6e5085f293b908d3997688a58fe307e7','microsoft.ai.directml':'4e7cb7ddce8cf837a7a75dc029209b520ca0101470fcdf275c1f49736a3615b9'}
def package(name,version):
    url=f'https://api.nuget.org/v3-flatcontainer/{name}/{version}/{name}.{version}.nupkg'
    data=urllib.request.urlopen(url).read()
    assert hashlib.sha256(data).hexdigest()==expected[name], 'Runtime package checksum mismatch'
    return zipfile.ZipFile(io.BytesIO(data)),{'url':url,'sha256':hashlib.sha256(data).hexdigest()}
ort, ort_source=package('microsoft.ml.onnxruntime.directml','1.23.0')
dml, dml_source=package('microsoft.ai.directml','1.15.4')
(root/'vendor').mkdir(exist_ok=True)
for name in ort.namelist():
    if name.startswith('build/native/include/') and not name.endswith('/'):
        (root/'vendor'/Path(name).name).write_bytes(ort.read(name))
for name in ['onnxruntime.dll','onnxruntime_providers_shared.dll']:
    (root/name).write_bytes(ort.read('runtimes/win-x64/native/'+name))
(root/'ONNXRuntime-LICENSE.txt').write_bytes(ort.read('LICENSE'))
(root/'ONNXRuntime-ThirdPartyNotices.txt').write_bytes(ort.read('ThirdPartyNotices.txt'))
(root/'DirectML.dll').write_bytes(dml.read('bin/x64-win/DirectML.dll'))
(root/'DirectML-LICENSE.txt').write_bytes(dml.read('LICENSE.txt'))
(root/'DirectML-LICENSE-CODE.txt').write_bytes(dml.read('LICENSE-CODE.txt'))
vc_url='https://aka.ms/vs/17/release/vc_redist.x64.exe'
vc=urllib.request.urlopen(vc_url).read()
(root/'Prerequisites').mkdir(exist_ok=True)
(root/'Prerequisites/VC_redist.x64.exe').write_bytes(vc)
(root/'RuntimeSources.json').write_text(json.dumps({'onnxRuntime':ort_source,'directML':dml_source,'visualCppRedistributable':{'url':vc_url,'sha256':hashlib.sha256(vc).hexdigest()}},indent=2))
print('Pinned Windows x64 runtimes and license files restored.')
