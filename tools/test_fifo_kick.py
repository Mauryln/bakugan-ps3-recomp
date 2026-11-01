"""The port must wait on the FIFO kick, and a kick must interrupt idle sleep."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

source = (ROOT / "bakugan-recomp/main.cpp").read_text(encoding="utf-8")
clock = source[source.index("static DWORD WINAPI frame_clock("):]
clock = clock[:clock.index("\n    return 0;")]
assert "cellGcm_fifo_kick_wait(4);" in clock, "frame clock ignores FIFO wakeups"
idle = clock[clock.index("    for (;;) {"):clock.index("        ULONGLONG now")]
windows_idle = idle.split("#else")[0]
assert "Sleep(4);" not in windows_idle, "fixed sleep delays each guest fence"

source = (ROOT / "ps3recomp-src/libs/video/cellGcmSys.c").read_text(encoding="utf-8")
parts = []
for name in ["void* cellGcm_fifo_kick_event(", "void cellGcm_fifo_kick_wait("]:
    start = source.index(name)
    parts.append(source[start:source.index("\n}", start) + 2])
out = ROOT / "diagnostics/performance/fifo-kick"
out.mkdir(parents=True, exist_ok=True)
(out / "fifo_kick.inc").write_text("\n".join(parts), encoding="utf-8")
source = (ROOT / "ps3recomp-src/runtime/ppu/ppu_loader.cpp").read_text(encoding="utf-8")
start = source.index("void vm_write32(uint64_t a, uint32_t v) {")
end = source.index("\nvoid vm_write64(", start)
(out / "fifo_put.inc").write_text(source[start:end], encoding="utf-8")
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc",
    "/I" + str(out), str(ROOT / "tools/tests/fifo_kick_test.cpp"),
    "/Fe:fifo_kick_test.exe"], cwd=out, env=environ(), check=True)
subprocess.run([str(out / "fifo_kick_test.exe")], check=True, timeout=10)
