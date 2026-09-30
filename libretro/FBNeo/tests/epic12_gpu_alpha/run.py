#!/usr/bin/env python3
"""Standalone EPIC12 transparent-source metadata tests; no ROMs or Xbox SDK needed."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--toolchain-root", type=Path, help="optional extracted Debian PPC toolchain")
    ap.add_argument("--sanitize", action="store_true", help="host ASan/UBSan")
    ap.add_argument("--output", type=Path)
    ap.add_argument("--trace", type=Path, help="optional private BE page metadata/query trace")
    args = ap.parse_args()
    here = Path(__file__).resolve().parent
    env = dict(os.environ)
    compiler = env.get("CXX", "g++")
    flags = ["-std=gnu++98", "-O3", "-Wall", "-Wextra"]
    runner = []
    if args.toolchain_root:
        root = args.toolchain_root.resolve()
        bindir = root / "usr/bin"
        compiler = str(sorted(bindir.glob("powerpc-linux-gnu-g++*"))[0])
        flags += ["--sysroot=" + str(root), "-B" + str(bindir) + "/", "-static"]
        env["LD_LIBRARY_PATH"] = str(root / "usr/lib/x86_64-linux-gnu")
        runner = [str(bindir / "qemu-ppc")]
    elif args.sanitize:
        flags += ["-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    with tempfile.TemporaryDirectory(prefix="epic12-alpha-") as temp:
        out = args.output.resolve() if args.output else Path(temp)
        out.mkdir(parents=True, exist_ok=True)
        binary = out / "alpha-test"
        subprocess.run([compiler] + flags + [str(here / "test.cpp"), "-o", str(binary)], env=env, check=True)
        result = subprocess.run(runner + [str(binary)], env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (out / "result.log").write_text(result.stdout)
        print(result.stdout, end="")
        if result.returncode:
            raise SystemExit(result.returncode)
        if args.trace:
            binary = out / "alpha-trace"
            subprocess.run([compiler] + flags + [str(here / "trace.cpp"), "-o", str(binary)], env=env, check=True)
            result = subprocess.run(runner + [str(binary), str(args.trace.resolve())], env=env,
                                    text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            (out / "trace.log").write_text(result.stdout)
            print(result.stdout, end="")
            if result.returncode:
                raise SystemExit(result.returncode)
        print("Artifacts:", out)


if __name__ == "__main__":
    main()
