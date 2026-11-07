#!/usr/bin/env python3
"""Test the staging column collector and row-major copy without ROMs or an SDK."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolchain-root", type=Path)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    env = dict(os.environ)
    flags = ["-std=gnu++98", "-Wall", "-Wextra", "-Werror"]
    runner = []
    if args.toolchain_root:
        if args.sanitize:
            parser.error("use --sanitize with the native host compiler")
        root = args.toolchain_root.resolve()
        compiler = str(root / "usr/bin/powerpc-linux-gnu-g++-13")
        env["LD_LIBRARY_PATH"] = str(root / "usr/lib/x86_64-linux-gnu")
        flags += ["-O3", "-static", "--sysroot=" + str(root), "-B" + str(root / "usr/bin") + "/"]
        runner = [str(root / "usr/bin/qemu-ppc")]
    else:
        compiler = shutil.which("g++") or shutil.which("c++")
        if not compiler:
            parser.error("a C++ compiler is required")
        flags += (["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
                  if args.sanitize else ["-O2"])
    with tempfile.TemporaryDirectory(prefix="epic12-columns-") as temp:
        out = args.output.resolve() if args.output else Path(temp)
        out.mkdir(parents=True, exist_ok=True)
        binary = out / "columns-test"
        source = Path(__file__).resolve().with_name("test.cpp")
        subprocess.run([compiler] + flags + [str(source), "-o", str(binary)], env=env, check=True)
        result = subprocess.run(runner + [str(binary)], env=env, capture_output=True, text=True)
        (out / "result.log").write_text(result.stdout + result.stderr)
        print(result.stdout + result.stderr, end="")
        if result.returncode:
            raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()
