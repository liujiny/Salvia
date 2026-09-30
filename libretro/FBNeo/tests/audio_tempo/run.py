#!/usr/bin/env python3
"""FBNeo audio tempo and buffer integration tests. No ROMs or Xbox SDK needed."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--toolchain-root", type=Path, help="optional extracted Debian PPC cross toolchain")
    ap.add_argument("--sanitize", action="store_true", help="host ASan/UBSan")
    ap.add_argument("--output", type=Path)
    args = ap.parse_args()
    here = Path(__file__).resolve().parent
    project = here.parents[3]
    env = dict(os.environ)
    compiler = env.get("CXX", "g++")
    flags = ["-std=c++11", "-O2", "-g", "-Wall", "-Wextra", "-pthread", "-D_XBOX",
             "-I" + str(here / "mock"), "-I" + str(project / "src")]
    runner = []
    if args.toolchain_root:
        root = args.toolchain_root.resolve()
        bindir = root / "usr/bin"
        compiler = str(sorted(bindir.glob("powerpc-linux-gnu-g++*"))[0])
        flags += ["--sysroot=" + str(root), "-B" + str(bindir) + "/", "-static"]
        env["LD_LIBRARY_PATH"] = str(root / "usr/lib/x86_64-linux-gnu")
        runner = [str(bindir / "qemu-ppc")]
    elif args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    with tempfile.TemporaryDirectory(prefix="salvia-audio-") as temp:
        out = args.output.resolve() if args.output else Path(temp)
        out.mkdir(parents=True, exist_ok=True)
        log = []
        for test in ["test", "buffer"]:
            binary = out / test
            subprocess.run([compiler] + flags + [str(here / (test + ".cpp")), "-o", str(binary)],
                           env=env, check=True)
            result = subprocess.run(runner + [str(binary)], env=env, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            log.append(result.stdout)
            print(result.stdout, end="")
            (out / "result.log").write_text("".join(log))
            if result.returncode:
                raise SystemExit(result.returncode)
        print("Artifacts:", out)


if __name__ == "__main__":
    main()
