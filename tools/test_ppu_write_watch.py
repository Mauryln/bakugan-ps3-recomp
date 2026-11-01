"""Disabled write diagnostics must be cheap without losing armed probes."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

source = (ROOT / "ps3recomp-src/runtime/ppu/ppu_loader.cpp").read_text(encoding="utf-8")
parts = []
for name in ["static int ppu_write_watch_env_enabled(", "static inline void barrier_watch_hit("]:
    assert name in source, "write diagnostics still execute all inactive probes per store"
    start = source.index(name)
    parts.append(source[start:source.index("\n}", start) + 2])
assert "s_vm_write_watch_enabled = ppu_write_watch_env_enabled();" in source
out = ROOT / "diagnostics/performance/write-watch"
out.mkdir(parents=True, exist_ok=True)
(out / "write_watch.inc").write_text("\n".join(parts), encoding="utf-8")
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc",
    "/I" + str(out), str(ROOT / "tools/tests/ppu_write_watch_test.cpp"),
    "/Fe:write_watch_test.exe"], cwd=out, env=environ(), check=True)
subprocess.run([str(out / "write_watch_test.exe")], check=True, timeout=10)
