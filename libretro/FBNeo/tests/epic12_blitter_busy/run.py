#!/usr/bin/env python3
import subprocess
import sys
from pathlib import Path

def main():
    test_dir = Path(__file__).parent
    cpp_file = test_dir / "test.cpp"
    bin_file = test_dir / "test_bin"

    cmd = ["g++", "-O2", "-Wall", "-Wextra", "-Werror", str(cpp_file), "-o", str(bin_file)]
    res = subprocess.run(cmd)
    if res.returncode != 0:
        sys.exit(res.returncode)

    res = subprocess.run([str(bin_file)])
    if bin_file.exists():
        bin_file.unlink()
    sys.exit(res.returncode)

if __name__ == "__main__":
    main()
