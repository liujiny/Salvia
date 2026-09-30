#!/usr/bin/env python3
"""Build and run the SH3 -> PPC differential tests without ROMs or an Xbox SDK."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolchain-root", type=Path,
                        help="optional root containing extracted Debian PPC toolchain packages")
    parser.add_argument("--output", type=Path, help="keep the binary and log in this directory")
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    src = here.parent.parent / "src"
    env = dict(os.environ)
    extra = []
    if args.toolchain_root:
        toolroot = args.toolchain_root.resolve()
        bindir = toolroot / "usr/bin"
        compilers = sorted(bindir.glob("powerpc-linux-gnu-g++*"))
        compiler = str(compilers[0]) if compilers else None
        qemu = str(bindir / "qemu-ppc")
        extra = ["--sysroot=" + str(toolroot), "-B" + str(bindir) + "/"]
        libraries = str(toolroot / "usr/lib/x86_64-linux-gnu")
        env["LD_LIBRARY_PATH"] = libraries + (":" + env["LD_LIBRARY_PATH"] if env.get("LD_LIBRARY_PATH") else "")
    else:
        compiler = os.environ.get("PPC_CXX") or shutil.which("powerpc-linux-gnu-g++")
        qemu = os.environ.get("QEMU_PPC") or shutil.which("qemu-ppc")
    if not compiler or not qemu:
        parser.error("install powerpc-linux-gnu-g++ and qemu-ppc, or use --toolchain-root")
    with tempfile.TemporaryDirectory(prefix="fbneo-sh3-ppc-") as temp:
        out = args.output.resolve() if args.output else Path(temp)
        out.mkdir(parents=True, exist_ok=True)
        binary = out / "sh3-ppc-test"
        include_dirs = [here, src / "burn", src / "cpu", src / "burn/snd",
                        src / "burn/devices", src / "burner", src / "cpu/sh4"]
        command = [compiler, "-std=gnu++98", "-O3", "-static", "-DNDEBUG",
                   "-D__LIBRETRO__", "-DSH3_PPC_DRC_TEST", "-Wno-write-strings",
                   "-Wno-array-bounds", "-ffast-math", "-frounding-math"]
        command += extra + ["-I" + str(p) for p in include_dirs]
        command += [str(here / "test.cpp"), "-o", str(binary)]
        subprocess.run(command, env=env, cwd=out, check=True)
        result = subprocess.run([qemu, str(binary)], env=env, cwd=out,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        (out / "result.log").write_text(result.stdout)
        print(result.stdout, end="")
        if result.returncode:
            raise SystemExit(result.returncode)
        if args.output:
            print("Artifacts:", out)


if __name__ == "__main__":
    main()
