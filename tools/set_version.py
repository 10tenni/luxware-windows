"""Prepare the source for a future release, e.g. python tools/set_version.py 1.2.0."""
from pathlib import Path
import json, re, sys
v = sys.argv[1]
if not re.fullmatch(r'\d{1,4}\.\d{1,4}\.\d{1,4}', v): raise SystemExit('Use major.minor.patch')
root = Path(__file__).resolve().parents[1]
for name, pattern, value in [
    ('src/update.hpp', r'appVersion = L"[0-9.]+"', f'appVersion = L"{v}"'),
    ('CMakeLists.txt', r'project\(Luxware VERSION [0-9.]+', f'project(Luxware VERSION {v}'),
    ('app.rc', r'(FILEVERSION|PRODUCTVERSION) [0-9,]+', lambda m: m[1]+' '+v.replace('.',',')+',0'),
    ('app.rc', r'"[0-9]+\.[0-9]+\.[0-9]+\\0"', lambda m: '"'+v+'\\0"'),
]:
    p=root/name;p.write_text(re.sub(pattern,value,p.read_text()))
(root/'release.json').write_text(json.dumps({'appId':'luxware-windows-x64','version':v})+'\n')
print('Version set to',v)
