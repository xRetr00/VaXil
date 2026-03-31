# -*- mode: python ; coding: utf-8 -*-

from pathlib import Path

from PyInstaller.utils.hooks import collect_data_files, collect_submodules


runtime_root = Path(SPECPATH).resolve()

hiddenimports = sorted(set(
    collect_submodules("vaxil_runtime")
    + collect_submodules("vaxil_runtime.Actions")
    + collect_submodules("playwright")
))

datas = []
datas += collect_data_files("playwright")
datas += collect_data_files("certifi")


a = Analysis(
    [str(runtime_root / "vaxil_runtime" / "main.py")],
    pathex=[str(runtime_root)],
    binaries=[],
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
)

pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name="vaxil_python_runtime",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    console=True,
    disable_windowed_traceback=False,
)

coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=False,
    upx_exclude=[],
    name="vaxil_python_runtime",
)
