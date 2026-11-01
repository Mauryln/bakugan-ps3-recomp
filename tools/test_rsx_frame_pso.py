"""Prepare PSOs once per draw and retain them through GPU completion."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

source = (ROOT / "ps3recomp-src/libs/video/rsx_d3d12_backend.c").read_text(encoding="utf-8")
parts = []
for name in ["static void vp_store_frame_pso(", "static void vp_release_frame_psos(", "static ID3D12PipelineState* vp_prepare_draw_pso("]:
    assert name in source, "PSO is still looked up twice per draw without frame ownership"
    start = source.index(name)
    parts.append(source[start:source.index("\n}", start) + 2])
assert source.count("vp_get_fp_pso(") == 2, "draw pass must reuse the prepared pipeline"
assert source.count("wait_for_gpu();\n    vp_release_frame_psos();") == 2
out = ROOT / "diagnostics/performance/frame-pso"
out.mkdir(parents=True, exist_ok=True)
(out / "frame_pso.inc").write_text("\n".join(parts), encoding="utf-8")
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc",
    "/I" + str(out), str(ROOT / "tools/tests/rsx_frame_pso_test.cpp"),
    "/Fe:frame_pso_test.exe"], cwd=out, env=environ(), check=True)
for mode in ["normal", "opaque"]:
    subprocess.run([str(out / "frame_pso_test.exe"), mode], check=True, timeout=10)
