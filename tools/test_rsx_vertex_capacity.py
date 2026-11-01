"""Verify growing VP storage preserves vertex data across both frame parities."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

out = ROOT / "diagnostics/arena/vertex-capacity"
out.mkdir(parents=True, exist_ok=True)
source = (ROOT / "ps3recomp-src/libs/video/rsx_d3d12_backend.c").read_text(encoding="utf-8")
parts = []
for name in ["static u32 vp_capacity_for(","static void vp_copy_vertex_frames(",
             "static void vp_flush_vertex_frame("]:
    if name in source:
        start = source.index(name)
        end = source.index("\n}", start) + 2
        parts.append(source[start:end])
if "static void vp_flush_vertex_frame(" not in source:
    parts.append("static void vp_flush_vertex_frame(void*, const void*, u32, u32, u32) {}")
extracted = "\n".join(parts)
(out / "vertex_capacity.inc").write_text(extracted, encoding="utf-8")
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc",
    "/I" + str(out), str(ROOT / "tools/tests/rsx_vertex_capacity_test.cpp"),
    "/Fe:vertex_capacity_test.exe"], cwd=out, env=environ(), check=True)
subprocess.run([str(out / "vertex_capacity_test.exe")], check=True, timeout=15)
