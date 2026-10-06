#!/usr/bin/env python3
"""Compile the actual recorder and verify MP4 PTS; requires FFmpeg/pkg-config/C++."""
import json
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
flags = shlex.split(subprocess.check_output(
    ["pkg-config", "--cflags", "--libs", "libavcodec", "libavformat", "libavutil", "libswscale"],
    text=True,
))
with tempfile.TemporaryDirectory(prefix="recording-timing-") as directory:
    executable = str(Path(directory) / "recording-timing")
    subprocess.run([
        "c++", "-std=c++11", "-DSTANDALONE", "-I" + str(root / "src"),
        str(root / "tests/recording-timing.cpp"), str(root / "src/FFRecorder.cpp"),
        *flags, "-o", executable,
    ], check=True)
    subprocess.run([executable, directory], check=True)
    for name in ("ndi", "srt"):
        probe = json.loads(subprocess.check_output([
            "ffprobe", "-v", "error", "-show_frames", "-show_format", "-of", "json",
            str(Path(directory) / (name + ".mp4")),
        ], text=True))
        times = [float(frame["pts_time"]) for frame in probe["frames"]]
        expected = [0, 0.016683, 0.033367, 1.001, 1.017683]
        assert len(times) == len(expected), times
        assert all(abs(actual - target) <= 0.000002
                   for actual, target in zip(times, expected)), times
        assert probe["format"]["tags"]["com.crewtimer.first_utc_us"] == "1734893104112456"
        assert 1.03 < float(probe["format"]["duration"]) < 1.04
        print(f"{name}: source PTS gaps, fractional rate, UTC metadata, and segment reset verified")
