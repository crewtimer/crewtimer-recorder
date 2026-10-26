#!/usr/bin/env python3
"""Check that one encoder kept open across files can start each file with 2 s of overlap.

Tests every available H.264 encoder that FFRecorder might use; run it on each platform.
Requires FFmpeg/pkg-config/C++."""
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
flags = shlex.split(subprocess.check_output(
    ["pkg-config", "--cflags", "--libs", "libavcodec", "libavformat", "libavutil"],
    text=True,
))
with tempfile.TemporaryDirectory(prefix="overlap-encoder-") as directory:
    executable = str(Path(directory) / "overlap-encoder")
    subprocess.run([
        "c++", "-std=c++11", str(root / "tests/overlap-encoder.cpp"),
        *flags, "-o", executable,
    ], check=True)
    subprocess.run([executable, directory], check=True, cwd=directory)
