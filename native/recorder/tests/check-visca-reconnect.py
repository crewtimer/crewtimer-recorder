#!/usr/bin/env python3
"""Check VISCA refused-port reconnect/shutdown on macOS and Linux."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="visca-reconnect-") as directory:
    executable = str(Path(directory) / "visca-reconnect")
    subprocess.run([
        "c++", "-std=c++11", "-pthread", "-I" + str(root / "src"),
        str(root / "tests/visca-reconnect.cpp"),
        str(root / "src/visca/ViscaTcpClient.cpp"), "-o", executable,
    ], check=True)
    subprocess.run([executable], check=True, timeout=10)
