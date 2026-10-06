#!/usr/bin/env python3
"""Check fused UYVY rotate+I420 against swscale and benchmark it; requires FFmpeg/pkg-config/C++."""
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
flags = shlex.split(subprocess.check_output(
    ["pkg-config", "--cflags", "--libs", "libavutil", "libswscale"],
    text=True,
))
with tempfile.TemporaryDirectory(prefix="frame-rotation-") as directory:
    executable = str(Path(directory) / "frame-rotation")
    subprocess.run([
        "c++", "-std=c++14", "-O2", "-I" + str(root / "src"),
        str(root / "tests/frame-rotation.cpp"), str(root / "src/VideoUtils.cpp"),
        *flags, "-o", executable,
    ], check=True)
    subprocess.run([executable, "--benchmark"], check=True)
