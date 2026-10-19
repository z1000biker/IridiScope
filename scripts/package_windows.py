#!/usr/bin/env python3
"""Stage x64 PE imports recursively; fail on missing DLLs/exports.

Requires pefile (python -m pip install pefile). Use --dll-dir for each library
prefix's bin directory and the compiler's runtime DLL directory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import pefile

SYSTEM = set('''advapi32 bcrypt cfgmgr32 comctl32 comdlg32 crypt32 d3d11 d3d12
    dbghelp dnsapi dwmapi dxgi gdi32 hid imm32 iphlpapi kernel32 msvcrt mswsock
    ncrypt netapi32 normaliz ntdll ole32 oleaut32 opengl32 powrprof propsys
    psapi rpcrt4 secur32 setupapi shell32 shlwapi user32 userenv usp10 uxtheme
    version winhttp wininet winmm winspool wintrust ws2_32 wsock32 wtsapi32'''.split())

def system(name):
    stem = name.lower().removesuffix('.dll')
    return stem in SYSTEM or stem.startswith(('api-ms-win-', 'ext-ms-win-'))

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', required=True, type=Path)
    p.add_argument('--dll-dir', action='append', type=Path, default=[])
    p.add_argument('--output', required=True, type=Path)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    roots = [a.exe.parent, *a.dll_dir]
    available = {}
    for root in roots:
        for path in root.glob('*.dll'):
            available.setdefault(path.name.lower(), path)
    if a.exe.resolve() != (a.output / a.exe.name).resolve():
        shutil.copy2(a.exe, a.output / a.exe.name)
    pending = [a.output / a.exe.name]
    visited, manifest = set(), {}
    exports = {}
    while pending:
        path = pending.pop()
        name = path.name.lower()
        if name in visited:
            continue
        visited.add(name)
        with pefile.PE(str(path)) as pe:
            if pe.FILE_HEADER.Machine != 0x8664:
                raise RuntimeError(f'Not Windows x64: {path.name}')
            deps = []
            for dep in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', []):
                dll = dep.dll.decode('ascii').lower()
                deps.append(dll)
                if system(dll):
                    continue
                source = available.get(dll)
                if source is None:
                    raise RuntimeError(f'{path.name} needs missing DLL {dll}')
                target = a.output / source.name
                if source.resolve() != target.resolve():
                    shutil.copy2(source, target)
                if dll not in exports:
                    with pefile.PE(str(source)) as lib:
                        symbols = getattr(lib, 'DIRECTORY_ENTRY_EXPORT', None)
                        exports[dll] = {s.name for s in symbols.symbols if s.name} if symbols else set()
                missing = [i.name.decode('ascii') for i in dep.imports
                           if i.name and i.name not in exports[dll]]
                if missing:
                    raise RuntimeError(f'{path.name}: {dll} lacks exports {missing}')
                pending.append(target)
        manifest[path.name] = {'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                               'imports': deps, 'machine': 'AMD64'}
    (a.output / 'binary-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Validated {len(manifest)} Windows x64 binaries and all bundled import/export names')

if __name__ == '__main__':
    main()
