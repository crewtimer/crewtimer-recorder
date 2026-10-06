#!/usr/bin/env python3
"""Record identical footage at four quality targets; requires FFmpeg/pkg-config/C++."""
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
flags = shlex.split(subprocess.check_output(
    ["pkg-config", "--cflags", "--libs", "libavcodec", "libavformat", "libavutil", "libswscale"],
    text=True,
))
with tempfile.TemporaryDirectory(prefix="recording-quality-") as directory:
    executable = str(Path(directory) / "recording-quality")
    subprocess.run([
        "c++", "-std=c++11", "-DSTANDALONE", "-I" + str(root / "src"),
        str(root / "tests/recording-quality.cpp"), str(root / "src/FFRecorder.cpp"),
        *flags, "-o", executable,
    ], check=True)
    subprocess.run([executable, directory], check=True)
