#!/usr/bin/env python3
"""Exact VMX alpha metadata, native PPC execution and sprite-crop integration."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--toolchain-root", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    root, out = args.toolchain_root.resolve(), args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    here = Path(__file__).resolve().parent
    env = dict(os.environ, LD_LIBRARY_PATH=str(root / "usr/lib/x86_64-linux-gnu"))
    configurations = [
        ("host", [shutil.which("g++"), "-O2", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"], []),
        ("ppc", [str(root / "usr/bin/powerpc-linux-gnu-g++-13"), "-O3", "-static", "-maltivec", "-mabi=altivec",
                 "--sysroot=" + str(root), "-B" + str(root / "usr/bin") + "/"],
         [str(root / "usr/bin/qemu-ppc"), "-cpu", "g4"]),
    ]
    for name, compiler, executor in configurations:
        for suite in ["test", "integration"]:
            binary = out / (name + "-" + suite)
            command = compiler + ["-std=gnu++98", "-Wall", "-Wextra", str(here / (suite + ".cpp")), "-o", str(binary)]
            with (out / (name + "-" + suite + "-compile.log")).open("w") as log:
                result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT)
            if result.returncode:
                print((out / (name + "-" + suite + "-compile.log")).read_text())
                raise SystemExit(result.returncode)
            result = subprocess.run(executor + [str(binary)], env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            (out / (name + "-" + suite + ".log")).write_text(result.stdout)
            print(name, suite, result.returncode, result.stdout, flush=True)
            if result.returncode:
                raise SystemExit(result.returncode)
    print("Scope: PPC and host metadata/crop correctness; not Xenos execution or measured Xbox FPS")


if __name__ == "__main__":
    main()
