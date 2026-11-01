"""Native strips must produce the same oriented triangles as list expansion."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

source = (ROOT / "ps3recomp-src/libs/video/rsx_d3d12_backend.c").read_text(encoding="utf-8")
parts = []
for name in ["static u32 upload_strip_vp_indexed(", "static u32 upload_native_strip_vp_indexed("]:
    assert name in source, "native strip upload is not implemented"
    start = source.index(name)
    parts.append(source[start:source.index("\n}", start) + 2])
out = ROOT / "diagnostics/performance/native-strip"
out.mkdir(parents=True, exist_ok=True)
(out / "native_strip.inc").write_text("\n".join(parts), encoding="utf-8")
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc",
    "/I" + str(out), str(ROOT / "tools/tests/rsx_native_strip_test.cpp"),
    "/DNOMINMAX", "/Fe:native_strip_test.exe", "/link", "d3d11.lib", "d3dcompiler.lib"], cwd=out, env=environ(), check=True)
subprocess.run([str(out / "native_strip_test.exe")], check=True, timeout=15)
