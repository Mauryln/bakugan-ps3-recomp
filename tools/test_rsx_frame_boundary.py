"""Arena complexity must not lower later menu presentation frequency."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

main = (ROOT / "bakugan-recomp/main.cpp").read_text(encoding="utf-8")
assert 'if (!getenv("RSX_ACCUM_FRAME")) _putenv_s("RSX_ACCUM_FRAME", "1");' in main
source = (ROOT / "ps3recomp-src/libs/video/rsx_d3d12_backend.c").read_text(encoding="utf-8")
parts = []
for name in ["static int rsx_draw_has_display_color(", "static void d3d12_present(", "static void d3d12_clear("]:
    start = source.index(name)
    parts.append(source[start:source.index("\n}", start) + 2])
out = ROOT / "diagnostics/performance/frame-boundary"
out.mkdir(parents=True, exist_ok=True)
(out / "frame_boundary.inc").write_text("\n".join(parts), encoding="utf-8")
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc",
    "/D_CRT_SECURE_NO_WARNINGS", "/I" + str(out),
    str(ROOT / "tools/tests/rsx_frame_boundary_test.cpp"),
    "/Fe:frame_boundary_test.exe"], cwd=out, env=environ(), check=True)
subprocess.run([str(out / "frame_boundary_test.exe")], check=True, timeout=10)
