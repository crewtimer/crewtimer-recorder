#!/usr/bin/env python3

import argparse
import json
import subprocess
import sys

EXPECTED_INTERVAL_MS = 1000.0 / 60.0  # 16.666666...
DEFAULT_TOLERANCE_MS = 1.0


def get_frame_pts(filename: str) -> list[float]:
    cmd = [
        "ffprobe",
        "-v", "error",
        "-select_streams", "v:0",
        "-show_frames",
        "-show_entries", "frame=best_effort_timestamp_time,pts_time",
        "-of", "json",
        filename,
    ]

    try:
        result = subprocess.run(
            cmd,
            check=True,
            capture_output=True,
            text=True,
        )
    except FileNotFoundError:
        print("Error: ffprobe was not found in PATH.", file=sys.stderr)
        sys.exit(1)
    except subprocess.CalledProcessError as e:
        print(e.stderr, file=sys.stderr)
        sys.exit(1)

    data = json.loads(result.stdout)

    pts_values = []

    for frame in data.get("frames", []):
        # Prefer actual PTS. Fall back to FFmpeg's best-effort timestamp.
        timestamp = frame.get("pts_time")

        if timestamp is None:
            timestamp = frame.get("best_effort_timestamp_time")

        if timestamp is not None:
            pts_values.append(float(timestamp))

    return pts_values


def analyze_pts(
    pts_values: list[float],
    expected_ms: float,
    tolerance_ms: float,
) -> None:
    if len(pts_values) < 2:
        print("Not enough video frames found.")
        return

    print(f"Frames:            {len(pts_values)}")
    print(f"Expected spacing:  {expected_ms:.6f} ms")
    print(f"Tolerance:         {tolerance_ms:.3f} ms")
    print()

    bad_count = 0

    for frame_num in range(1, len(pts_values)):
        previous_pts = pts_values[frame_num - 1]
        current_pts = pts_values[frame_num]

        gap_ms = (current_pts - previous_pts) * 1000.0
        error_ms = gap_ms - expected_ms

        if abs(error_ms) > tolerance_ms:
            bad_count += 1

            print(
                f"Frame {frame_num:8d}: "
                f"PTS={current_pts:.6f}s  "
                f"gap={gap_ms:9.4f} ms  "
                f"error={error_ms:+9.4f} ms"
            )

    print()
    print(f"Unexpected gaps: {bad_count}")


def main():
    parser = argparse.ArgumentParser(
        description="Find unexpected video-frame PTS gaps in an MP4."
    )

    parser.add_argument(
        "filename",
        help="MP4 file to analyze",
    )

    parser.add_argument(
        "--fps",
        type=float,
        default=60.0,
        help="Expected frame rate (default: 60)",
    )

    parser.add_argument(
        "--tolerance",
        type=float,
        default=DEFAULT_TOLERANCE_MS,
        help="Allowed timing error in milliseconds (default: 1.0)",
    )

    args = parser.parse_args()

    expected_ms = 1000.0 / args.fps

    pts_values = get_frame_pts(args.filename)

    analyze_pts(
        pts_values,
        expected_ms,
        args.tolerance,
    )


if __name__ == "__main__":
    main()
