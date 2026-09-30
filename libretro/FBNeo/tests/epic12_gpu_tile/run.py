#!/usr/bin/env python3
"""Check tiled atlas uploads, byte order, page boundaries and WC-safe stores."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolchain-root", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = args.toolchain_root.resolve()
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = str(root / "usr/lib/x86_64-linux-gnu")
    source = Path(__file__).resolve().parent / "test.cpp"
    with tempfile.TemporaryDirectory(prefix="fbneo-tile-") as temporary:
        output = args.output.resolve() if args.output else Path(temporary)
        output.mkdir(parents=True, exist_ok=True)
        runs = [
            ("host", [shutil.which("g++"), "-O2", "-fsanitize=address,undefined",
                      "-fno-omit-frame-pointer"], []),
            ("ppc", [str(root / "usr/bin/powerpc-linux-gnu-g++-13"), "-O3", "-static",
                     "-maltivec", "-mabi=altivec", "--sysroot=" + str(root),
                     "-B" + str(root / "usr/bin") + "/"],
             [str(root / "usr/bin/qemu-ppc"), "-cpu", "g4"]),
        ]
        for name, compiler, executor in runs:
            binary = output / ("tile-" + name)
            subprocess.run(compiler + ["-std=gnu++98", "-fno-strict-aliasing",
                                       str(source), "-o", str(binary)], env=env, check=True)
            result = subprocess.run(executor + [str(binary)], env=env,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            (output / (name + ".log")).write_text(result.stdout)
            print(name + ":\n" + result.stdout, end="", flush=True)
            if result.returncode:
                raise SystemExit(result.returncode)
        print("Artifacts:", output)


if __name__ == "__main__":
    main()
