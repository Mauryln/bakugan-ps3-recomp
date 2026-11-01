"""Render scrolling UVs using the backend's actual sampler translation."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

out = ROOT / "diagnostics/arena/samplers"
out.mkdir(parents=True, exist_ok=True)
source = (ROOT / "ps3recomp-src/libs/video/rsx_d3d12_backend.c").read_text(encoding="utf-8")
parts = []
if "static D3D12_SAMPLER_DESC rsx_sampler_desc(" in source:
    for name in ("static D3D12_TEXTURE_ADDRESS_MODE rsx_sampler_wrap(",
                 "static D3D12_SAMPLER_DESC rsx_sampler_desc("):
        start = source.index(name)
        parts.append(source[start:source.index("\n}", start) + 2])
else:
    # RED baseline: use the real guest root signature's fixed unit-zero sampler.
    start = source.index("D3D12_STATIC_SAMPLER_DESC samp[RSX_MAX_TEXTURES]")
    end = source.index("D3D12_ROOT_SIGNATURE_DESC rd", start)
    parts.append("static D3D12_SAMPLER_DESC rsx_sampler_desc(u32, u32, u32, u32) {\n" +
                 source[start:end].replace("{0}", "{}") +
                 "D3D12_SAMPLER_DESC d = {}; d.Filter=samp[0].Filter; "
                 "d.AddressU=samp[0].AddressU; d.AddressV=samp[0].AddressV; "
                 "d.AddressW=samp[0].AddressW; d.MinLOD=samp[0].MinLOD; "
                 "d.MaxLOD=samp[0].MaxLOD; d.MaxAnisotropy=1; return d; }")
(out / "sampler_decode.inc").write_text("\n".join(parts).replace("{0}", "{}"), encoding="utf-8")
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc",
    "/I" + str(out), "/I" + str(ROOT / "ps3recomp-src/include"),
    "/I" + str(ROOT / "ps3recomp-src/libs/video"),
    str(ROOT / "tools/tests/rsx_samplers_test.cpp"), "/Fe:samplers_test.exe",
    "/link", "d3d11.lib", "d3dcompiler.lib"], cwd=out, env=environ(), check=True)
subprocess.run([str(out / "samplers_test.exe")], check=True, timeout=30)

if "static int rsx_prepare_samplers(" in source:
    start = source.index("#define SAMPLER_TABLES_PER_PAGE")
    end = source.index("\n}", source.index("static int rsx_prepare_samplers(", start)) + 2
    (out / "sampler_tables.inc").write_text(
        source[start:end].replace("{0}", "{}").replace(
            "&IID_ID3D12DescriptorHeap", "IID_ID3D12DescriptorHeap"), encoding="utf-8")
    subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc",
        "/I" + str(out), str(ROOT / "tools/tests/rsx_sampler_tables_test.cpp"),
        "/Fe:sampler_tables_test.exe", "/link", "d3d12.lib"],
        cwd=out, env=environ(), check=True)
    subprocess.run([str(out / "sampler_tables_test.exe")], check=True, timeout=30)
