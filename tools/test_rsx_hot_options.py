"""Startup-only FP options must not scan the process environment per draw."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

source = (ROOT / "ps3recomp-src/libs/video/rsx_d3d12_backend.c").read_text(encoding="utf-8")
name = "static int fp_constbuf_enabled("
assert name in source, "FP_CONSTBUF is still queried in the per-draw hot path"
start = source.index(name)
helper = source[start:source.index("\n}", start) + 2]
assert source.count("fp_constbuf_enabled()") == 1, "FP hashing must use the cached option"
out = ROOT / "diagnostics/performance/hot-options"
out.mkdir(parents=True, exist_ok=True)
(out / "hot_options.inc").write_text(helper, encoding="utf-8")
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc",
    "/I" + str(out), str(ROOT / "tools/tests/rsx_hot_options_test.cpp"),
    "/Fe:hot_options_test.exe"], cwd=out, env=environ(), check=True)
for value in ["absent", "", "0", "1", "other"]:
    subprocess.run([str(out / "hot_options_test.exe"), value], check=True, timeout=5)
