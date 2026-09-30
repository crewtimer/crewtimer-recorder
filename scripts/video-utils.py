#!/usr/bin/env python3
"""Extract a timestamp-centered clip that CrewTimer Video Review can ingest."""

from __future__ import annotations

import argparse
from bisect import bisect_left
from datetime import datetime, time as datetime_time, timedelta, timezone
from decimal import Decimal, InvalidOperation
import json
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
from typing import Any


CREWTIMER_TIMESTAMP_TAG = "com.crewtimer.first_utc_us"


class ClipError(Exception):
    """An error that can be reported without a traceback."""


def run(
    command: list[str], *, capture_output: bool = False
) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            command,
            check=True,
            text=True,
            capture_output=capture_output,
        )
    except FileNotFoundError as exc:
        raise ClipError(f"{command[0]} was not found in PATH") from exc
    except subprocess.CalledProcessError as exc:
        detail = (exc.stderr or "").strip()
        if detail:
            raise ClipError(f"{command[0]} failed: {detail}") from exc
        raise ClipError(f"{command[0]} failed with exit code {exc.returncode}") from exc


def probe_json(path: Path, *arguments: str) -> dict[str, Any]:
    result = run(
        ["ffprobe", "-v", "error", *arguments, "-of", "json", str(path)],
        capture_output=True,
    )
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        raise ClipError(f"ffprobe returned invalid JSON for {path}") from exc


def embedded_frame_time(path: Path, frame_index: int) -> Decimal | None:
    """Decode the recorder's 128x3 Unix 100-nanosecond timestamp overlay."""
    command = [
        "ffmpeg",
        "-v",
        "error",
        "-i",
        str(path),
        "-vf",
        f"select=eq(n\\,{frame_index}),crop=128:3:0:0",
        "-frames:v",
        "1",
        "-pix_fmt",
        "rgb24",
        "-fps_mode:v",
        "passthrough",
        "-f",
        "rawvideo",
        "pipe:1",
    ]
    try:
        result = subprocess.run(command, check=True, capture_output=True)
    except (FileNotFoundError, subprocess.CalledProcessError):
        return None
    if len(result.stdout) not in (128 * 2 * 3, 128 * 3 * 3):
        return None
    row = result.stdout[128 * 3 : 2 * 128 * 3]
    sums = [sum(row[index * 6 : index * 6 + 6]) for index in range(64)]
    ordered = sorted(sums)
    gaps = [ordered[index + 1] - ordered[index] for index in range(63)]
    largest_gap = max(gaps)
    if largest_gap < 10:
        return None
    gap_index = gaps.index(largest_gap)
    threshold = (ordered[gap_index] + ordered[gap_index + 1]) / 2
    ticks = 0
    for value in sums:
        ticks = (ticks << 1) | int(value > threshold)
    if ticks <= 0:
        return None
    return Decimal(ticks) / Decimal(10_000_000)


def embedded_frame_times(
    path: Path, sidecar: dict[str, Any] | None = None
) -> list[Decimal]:
    command = [
        "ffmpeg",
        "-v",
        "error",
        "-i",
        str(path),
        "-vf",
        "crop=128:2:0:0",
        "-pix_fmt",
        "rgb24",
        "-fps_mode:v",
        "passthrough",
        "-f",
        "rawvideo",
        "pipe:1",
    ]
    try:
        result = subprocess.run(command, check=True, capture_output=True)
    except FileNotFoundError as exc:
        raise ClipError("ffmpeg was not found in PATH") from exc
    except subprocess.CalledProcessError as exc:
        raise ClipError("could not decode embedded frame timestamps") from exc
    frame_size = 128 * 2 * 3
    if not result.stdout or len(result.stdout) % frame_size:
        raise ClipError("could not read timestamp-overlay pixels from the input")
    decoded: list[Decimal | None] = []
    for position in range(0, len(result.stdout), frame_size):
        frame = result.stdout[position : position + frame_size]
        row = frame[128 * 3 : 2 * 128 * 3]
        sums = [sum(row[index * 6 : index * 6 + 6]) for index in range(64)]
        ordered = sorted(sums)
        gaps = [ordered[index + 1] - ordered[index] for index in range(63)]
        largest_gap = max(gaps)
        if largest_gap < 10:
            decoded.append(None)
            continue
        gap_index = gaps.index(largest_gap)
        threshold = (ordered[gap_index] + ordered[gap_index + 1]) / 2
        ticks = 0
        for value in sums:
            ticks = (ticks << 1) | int(value > threshold)
        decoded.append(Decimal(ticks) / Decimal(10_000_000) if ticks > 0 else None)

    # Damaged pixels can still form a syntactically valid 64-bit number. Reject
    # clock values that are implausible in general or inconsistent with the
    # sidecar's known recording interval before using them for interpolation.
    earliest_plausible = Decimal("946684800")  # 2000-01-01 UTC
    latest_plausible = Decimal("4102444800")  # 2100-01-01 UTC
    expected_start: Decimal | None = None
    expected_stop: Decimal | None = None
    if sidecar is not None:
        file_metadata = sidecar.get("file", {})
        if file_metadata.get("startTs") is not None:
            expected_start = decimal_from_metadata(
                file_metadata["startTs"], "sidecar file.startTs"
            )
        if file_metadata.get("stopTs") is not None:
            expected_stop = decimal_from_metadata(
                file_metadata["stopTs"], "sidecar file.stopTs"
            )
    sidecar_tolerance = Decimal(2)
    if expected_start is not None and expected_stop is not None:
        sidecar_tolerance = max(
            sidecar_tolerance, (expected_stop - expected_start) * Decimal("0.05")
        )
    for frame_index, timestamp in enumerate(decoded):
        if timestamp is None:
            continue
        plausible = earliest_plausible <= timestamp <= latest_plausible
        if (
            expected_start is not None
            and expected_stop is not None
            and len(decoded) > 1
        ):
            expected = expected_start + (expected_stop - expected_start) * Decimal(
                frame_index
            ) / Decimal(len(decoded) - 1)
            plausible = plausible and abs(timestamp - expected) <= sidecar_tolerance
        if not plausible:
            decoded[frame_index] = None

    valid = [(index, value) for index, value in enumerate(decoded) if value is not None]
    if len(valid) < 2:
        raise ClipError("fewer than two embedded frame timestamps could be decoded")
    per_frame_intervals = [
        (current_time - previous_time) / Decimal(current_index - previous_index)
        for (previous_index, previous_time), (current_index, current_time) in zip(
            valid, valid[1:]
        )
    ]
    nominal_interval = Decimal(str(statistics.median(per_frame_intervals)))

    missing_ranges: list[tuple[int, int]] = []
    index = 0
    while index < len(decoded):
        if decoded[index] is not None:
            index += 1
            continue
        first = index
        while index + 1 < len(decoded) and decoded[index + 1] is None:
            index += 1
        last = index
        missing_ranges.append((first, last))
        previous_time = decoded[first - 1] if first > 0 else None
        next_time = decoded[last + 1] if last + 1 < len(decoded) else None
        if previous_time is not None and next_time is not None:
            interval = (next_time - previous_time) / Decimal(last - first + 2)
            for missing_index in range(first, last + 1):
                decoded[missing_index] = previous_time + interval * Decimal(
                    missing_index - first + 1
                )
        elif next_time is not None:
            for missing_index in range(last, first - 1, -1):
                decoded[missing_index] = next_time - nominal_interval * Decimal(
                    last - missing_index + 1
                )
        elif previous_time is not None:
            for missing_index in range(first, last + 1):
                decoded[missing_index] = previous_time + nominal_interval * Decimal(
                    missing_index - first + 1
                )
        index += 1

    timestamps = [value for value in decoded if value is not None]
    for first, last in missing_ranges:
        frame_label = f"frame {first}" if first == last else f"frames {first}-{last}"
        print(
            f"Warning: interpolated embedded timestamp for {frame_label}: "
            f"{local_clock_text(timestamps[first], sidecar)}",
            file=sys.stderr,
        )
    return timestamps


def timestamp_discontinuities(
    timestamps: list[Decimal],
) -> tuple[list[Decimal], Decimal, list[tuple[int, Decimal]]]:
    if len(timestamps) < 2:
        raise ClipError("at least two timestamped frames are required")
    deltas = [
        timestamps[index] - timestamps[index - 1] for index in range(1, len(timestamps))
    ]
    nominal = Decimal(str(statistics.median(deltas)))
    discontinuities = [
        (index, delta)
        for index, delta in enumerate(deltas, start=1)
        if abs(delta - nominal) > Decimal("0.004")
    ]
    return deltas, nominal, discontinuities


def timestamp_setpts_expression(timestamps: list[Decimal]) -> str:
    _, _, discontinuities = timestamp_discontinuities(timestamps)
    segment_starts = [0, *(index for index, _ in discontinuities)]

    expressions: list[str] = []
    for segment_index, start in enumerate(segment_starts):
        end = (
            segment_starts[segment_index + 1] - 1
            if segment_index + 1 < len(segment_starts)
            else len(timestamps) - 1
        )
        slope = (timestamps[end] - timestamps[start]) / max(1, end - start)
        offset = timestamps[start] - timestamps[0]
        expressions.append(
            f"({decimal_text(offset)}+(N-{start})*{decimal_text(slope)})/TB"
        )

    expression = expressions[-1]
    for index in range(len(expressions) - 2, -1, -1):
        boundary = segment_starts[index + 1]
        expression = f"if(lt(N\\,{boundary})\\,{expressions[index]}\\,{expression})"
    return expression


def sidecar_timezone(sidecar: dict[str, Any] | None) -> timezone:
    if sidecar is None:
        raise ClipError(
            "a wall-clock timestamp requires an input JSON sidecar with file.tzOffset"
        )
    offset_value = sidecar.get("file", {}).get("tzOffset")
    if offset_value is None:
        raise ClipError(
            "a wall-clock timestamp requires file.tzOffset in the input sidecar"
        )
    offset_minutes = decimal_from_metadata(offset_value, "sidecar file.tzOffset")
    if offset_minutes != offset_minutes.to_integral_value() or not (
        Decimal(-1439) <= offset_minutes <= Decimal(1439)
    ):
        raise ClipError("sidecar file.tzOffset must be whole minutes")
    # Recorder tzOffset is local time minus UTC (for example, -420 for PDT).
    return timezone(timedelta(minutes=int(offset_minutes)))


def local_clock_text(epoch_seconds: Decimal, sidecar: dict[str, Any] | None) -> str:
    try:
        local_timezone = sidecar_timezone(sidecar)
        label = "local"
    except ClipError:
        local_timezone = timezone.utc
        label = "UTC"
    try:
        value = datetime.fromtimestamp(float(epoch_seconds), tz=local_timezone)
    except (OverflowError, OSError, ValueError):
        return f"{decimal_text(epoch_seconds)} epoch seconds (out of range)"
    return f"{value.strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]} {label}"


def timestamped_video_path(
    input_path: Path, epoch_seconds: Decimal, sidecar: dict[str, Any] | None
) -> Path:
    try:
        local_timezone = sidecar_timezone(sidecar)
    except ClipError:
        local_timezone = timezone.utc
    label = datetime.fromtimestamp(float(epoch_seconds), tz=local_timezone).strftime(
        "%Y%m%d_%H%M%S"
    )
    match = re.search(r"\d{8}_\d{6}", input_path.stem)
    if match:
        stem = (
            f"{input_path.stem[: match.start()]}{label}{input_path.stem[match.end() :]}"
        )
    else:
        stem = f"{input_path.stem}_{label}"
    output_path = input_path.with_name(f"{stem}.mp4")
    if output_path == input_path:
        output_path = input_path.with_name(f"{stem}_B.mp4")
    return output_path


def parse_timestamp(
    value: str,
    sidecar: dict[str, Any] | None = None,
    recording_start: Decimal | None = None,
    recording_duration: Decimal | None = None,
) -> Decimal:
    """Return a user timestamp as Unix epoch seconds."""
    try:
        numeric = Decimal(value)
    except InvalidOperation:
        text = value.strip()
        if text.endswith("Z"):
            text = text[:-1] + "+00:00"
        if ":" in text and "T" not in text and " " not in text:
            try:
                clock_time = datetime_time.fromisoformat(text)
            except ValueError as exc:
                raise ClipError(f"invalid wall-clock time: {value!r}") from exc
            if clock_time.tzinfo is not None:
                raise ClipError("a time without a date must not include a UTC offset")
            if recording_start is None or recording_duration is None:
                raise ClipError(
                    "a time without a date requires the recording time range"
                )
            local_timezone = sidecar_timezone(sidecar)
            first_date = datetime.fromtimestamp(
                float(recording_start), tz=local_timezone
            ).date()
            last_date = datetime.fromtimestamp(
                float(recording_start + recording_duration), tz=local_timezone
            ).date()
            candidates: list[Decimal] = []
            current_date = first_date
            while current_date <= last_date:
                candidate = datetime.combine(
                    current_date, clock_time, tzinfo=local_timezone
                )
                candidate_epoch = Decimal(str(candidate.timestamp()))
                if (
                    recording_start
                    <= candidate_epoch
                    <= recording_start + recording_duration
                ):
                    candidates.append(candidate_epoch)
                current_date += timedelta(days=1)
            if not candidates:
                raise ClipError(f"wall-clock time {value!r} is outside the recording")
            if len(candidates) > 1:
                raise ClipError(
                    f"wall-clock time {value!r} occurs more than once in the recording; "
                    "include the date"
                )
            return candidates[0]
        try:
            parsed = datetime.fromisoformat(text)
        except ValueError as exc:
            raise ClipError(
                "timestamp must be ISO-8601 or Unix epoch seconds, milliseconds, "
                "or microseconds"
            ) from exc
        if parsed.tzinfo is None:
            parsed = parsed.replace(tzinfo=sidecar_timezone(sidecar))
        return Decimal(str(parsed.timestamp()))

    magnitude = abs(numeric)
    if magnitude >= Decimal("1e14"):
        return numeric / Decimal(1_000_000)
    if magnitude >= Decimal("1e11"):
        return numeric / Decimal(1_000)
    return numeric


def decimal_from_metadata(value: Any, description: str) -> Decimal:
    try:
        return Decimal(str(value))
    except (InvalidOperation, ValueError) as exc:
        raise ClipError(f"invalid {description}: {value!r}") from exc


def read_sidecar(input_path: Path) -> tuple[Path, dict[str, Any] | None]:
    sidecar_path = input_path.with_suffix(".json")
    if not sidecar_path.exists():
        return sidecar_path, None
    try:
        with sidecar_path.open(encoding="utf-8") as sidecar_file:
            data = json.load(sidecar_file)
    except (OSError, json.JSONDecodeError) as exc:
        raise ClipError(f"could not read sidecar {sidecar_path}: {exc}") from exc
    if not isinstance(data, dict):
        raise ClipError(f"sidecar {sidecar_path} must contain a JSON object")
    return sidecar_path, data


def find_start_time(probe: dict[str, Any], sidecar: dict[str, Any] | None) -> Decimal:
    tags = probe.get("format", {}).get("tags", {})
    value = tags.get(CREWTIMER_TIMESTAMP_TAG)
    if value is None:
        for stream in probe.get("streams", []):
            value = stream.get("tags", {}).get(CREWTIMER_TIMESTAMP_TAG)
            if value is not None:
                break
    if value is not None:
        return decimal_from_metadata(value, CREWTIMER_TIMESTAMP_TAG) / Decimal(
            1_000_000
        )

    if sidecar is not None:
        value = sidecar.get("file", {}).get("startTs")
        if value is not None:
            return decimal_from_metadata(value, "sidecar file.startTs")

    raise ClipError(
        f"input has neither {CREWTIMER_TIMESTAMP_TAG} metadata nor sidecar file.startTs"
    )


def has_crewtimer_pts_clock(probe: dict[str, Any]) -> bool:
    if CREWTIMER_TIMESTAMP_TAG in probe.get("format", {}).get("tags", {}):
        return True
    return any(
        CREWTIMER_TIMESTAMP_TAG in stream.get("tags", {})
        for stream in probe.get("streams", [])
    )


def utc_iso(epoch_seconds: Decimal) -> str:
    epoch_us = int(epoch_seconds * Decimal(1_000_000))
    seconds, microseconds = divmod(epoch_us, 1_000_000)
    value = datetime.fromtimestamp(seconds, tz=timezone.utc).replace(
        microsecond=microseconds
    )
    return value.isoformat(timespec="microseconds").replace("+00:00", "Z")


def decimal_text(value: Decimal) -> str:
    text = format(value, "f")
    if "." in text:
        text = text.rstrip("0").rstrip(".")
    return text or "0"


def source_frame_offsets(path: Path) -> list[Decimal]:
    data = probe_json(
        path,
        "-select_streams",
        "v:0",
        "-show_frames",
        "-show_entries",
        "frame=best_effort_timestamp_time,pts_time",
    )
    timestamps: list[Decimal] = []
    for frame in data.get("frames", []):
        value = frame.get("best_effort_timestamp_time")
        if value is None:
            value = frame.get("pts_time")
        if value is not None:
            timestamps.append(decimal_from_metadata(value, "source frame timestamp"))
    if not timestamps:
        raise ClipError("the input contains no timestamped video frames")
    first_timestamp = timestamps[0]
    return [timestamp - first_timestamp for timestamp in timestamps]


def nearest_frame_time(value: Decimal, frame_offsets: list[Decimal]) -> Decimal:
    return min(frame_offsets, key=lambda timestamp: abs(timestamp - value))


def source_frame_wall_times(
    input_start: Decimal,
    frame_offsets: list[Decimal],
    sidecar: dict[str, Any] | None,
) -> list[Decimal]:
    if sidecar is not None:
        file_metadata = sidecar.get("file", {})
        stop_value = file_metadata.get("stopTs")
        frame_count = file_metadata.get("numFrames")
        if (
            stop_value is not None
            and frame_count == len(frame_offsets)
            and frame_count > 1
        ):
            sidecar_start = decimal_from_metadata(
                file_metadata.get("startTs", input_start), "sidecar file.startTs"
            )
            sidecar_stop = decimal_from_metadata(stop_value, "sidecar file.stopTs")
            frame_period = (sidecar_stop - sidecar_start) / Decimal(frame_count - 1)
            return [
                sidecar_start + Decimal(index) * frame_period
                for index in range(frame_count)
            ]
    return [input_start + offset for offset in frame_offsets]


def nearest_frame_index(value: Decimal, frame_times: list[Decimal]) -> int:
    return min(
        range(len(frame_times)), key=lambda index: abs(frame_times[index] - value)
    )


def refine_split_index_from_embedded_time(
    path: Path, estimated_index: int, target_time: Decimal, frame_count: int
) -> int:
    first = max(1, estimated_index - 12)
    last = min(frame_count - 1, estimated_index + 12)
    decoded = [
        (index, embedded_frame_time(path, index)) for index in range(first, last + 1)
    ]
    candidates = [
        (index, timestamp)
        for index, timestamp in decoded
        if timestamp is not None and timestamp >= target_time
    ]
    if not candidates:
        return estimated_index
    return min(candidates, key=lambda item: item[1])[0]


def output_frame_stats(path: Path) -> tuple[int, Decimal]:
    data = probe_json(
        path,
        "-select_streams",
        "v:0",
        "-show_frames",
        "-show_entries",
        "frame=best_effort_timestamp_time,pts_time",
    )
    timestamps: list[Decimal] = []
    for frame in data.get("frames", []):
        value = frame.get("best_effort_timestamp_time", frame.get("pts_time"))
        if value is not None:
            timestamps.append(decimal_from_metadata(value, "output frame timestamp"))
    if not timestamps:
        raise ClipError("the output contains no timestamped video frames")
    return len(timestamps), max(timestamps) - min(timestamps)


def write_sidecar(
    path: Path,
    source: dict[str, Any] | None,
    width: int,
    height: int,
    start_time: Decimal,
    stop_time: Decimal,
    frame_count: int,
) -> None:
    result = (
        dict(source)
        if source is not None
        else {
            "source": {
                "width": width,
                "height": height,
                "originalWidth": width,
                "originalHeight": height,
                "crop": {
                    "x": 0,
                    "y": 0,
                    "width": width,
                    "height": height,
                },
            },
            "sensor": {
                "nativeWidth": width,
                "nativeHeight": height,
                "sourceRotationDegrees": 0,
                "rollingShutter": None,
            },
            "guide": {"pt1": 0, "pt2": 0},
        }
    )
    file_metadata = dict(result.get("file", {}))
    if "tzOffset" not in file_metadata:
        file_metadata["tzOffset"] = 0
    file_metadata.update(
        {
            "startTs": f"{start_time:.7f}",
            "stopTs": f"{stop_time:.7f}",
            "numFrames": frame_count,
        }
    )
    result["file"] = file_metadata
    try:
        with path.open("w", encoding="utf-8") as output:
            json.dump(result, output, indent=2)
            output.write("\n")
    except OSError as exc:
        raise ClipError(f"could not write sidecar {path}: {exc}") from exc


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Extract an MP4 around an absolute UTC timestamp and write CrewTimer "
            "timestamp metadata plus a JSON sidecar."
        )
    )
    parser.add_argument("input", type=Path, help="source MP4")
    parser.add_argument(
        "timestamp",
        nargs="?",
        help=(
            "center time as HH:MM:SS, ISO-8601, or Unix epoch time; local values "
            "use the recording date and sidecar tzOffset"
        ),
    )
    parser.add_argument(
        "seconds",
        type=Decimal,
        nargs="?",
        help="seconds to include before and after timestamp (omit with --split)",
    )
    parser.add_argument(
        "--split",
        action="store_true",
        help="split the entire recording into basenameA.mp4 and basenameB.mp4",
    )
    parser.add_argument(
        "--fix",
        action="store_true",
        help="rewrite an old recording using its embedded frame timestamps",
    )
    parser.add_argument(
        "--validate",
        action="store_true",
        help="read the MP4 and sidecar and report timing problems without writing files",
    )
    parser.add_argument(
        "--in-place",
        action="store_true",
        help="with --fix, replace the input MP4 and its same-name sidecar",
    )
    parser.add_argument("-o", "--output", type=Path, help="output MP4 path")
    parser.add_argument(
        "--overwrite", action="store_true", help="replace existing output files"
    )
    parser.add_argument(
        "--crf",
        type=int,
        default=18,
        help="H.264 constant-rate-factor quality (default: 18)",
    )
    parser.add_argument(
        "--preset", default="fast", help="libx264 encoding preset (default: fast)"
    )
    return parser


def encode_clip(
    input_path: Path,
    temporary_output: Path,
    start_frame: int,
    end_frame: int,
    clip_offset: Decimal,
    clip_end: Decimal,
    output_start: Decimal,
    has_audio: bool,
    preset: str,
    crf: int,
) -> tuple[Decimal, int, Decimal]:
    output_start_us = int(output_start * Decimal(1_000_000))
    output_start = Decimal(output_start_us) / Decimal(1_000_000)
    command = [
        "ffmpeg",
        "-hide_banner",
        "-loglevel",
        "error",
        "-i",
        str(input_path),
        "-map",
        "0:v:0",
    ]
    if has_audio:
        command += ["-map", "0:a:0"]
    command += [
        "-c:v",
        "libx264",
        "-preset",
        preset,
        "-crf",
        str(crf),
        "-pix_fmt",
        "yuv420p",
        "-vf",
        f"trim=start_frame={start_frame}:end_frame={end_frame},setpts=PTS-STARTPTS",
        "-fps_mode:v",
        "passthrough",
    ]
    if has_audio:
        command += [
            "-c:a",
            "aac",
            "-af",
            f"atrim=start={decimal_text(clip_offset)}:end={decimal_text(clip_end)},"
            "asetpts=PTS-STARTPTS",
        ]
    creation_time = utc_iso(output_start)
    command += [
        "-metadata",
        f"creation_time={creation_time}",
        "-metadata",
        f"{CREWTIMER_TIMESTAMP_TAG}={output_start_us}",
        "-metadata:s:v:0",
        f"creation_time={creation_time}",
        "-metadata:s:v:0",
        f"{CREWTIMER_TIMESTAMP_TAG}={output_start_us}",
    ]
    if has_audio:
        command += [
            "-metadata:s:a:0",
            f"creation_time={creation_time}",
            "-metadata:s:a:0",
            f"{CREWTIMER_TIMESTAMP_TAG}={output_start_us}",
        ]
    command += [
        "-movflags",
        "+faststart+use_metadata_tags",
        "-y",
        str(temporary_output),
    ]
    run(command)
    frame_count, last_frame_offset = output_frame_stats(temporary_output)
    return output_start, frame_count, last_frame_offset


def encode_fixed_video(
    input_path: Path,
    output_path: Path,
    timestamps: list[Decimal],
    has_audio: bool,
    preset: str,
    crf: int,
) -> None:
    first_utc_us = int(timestamps[0] * Decimal(1_000_000))
    creation_time = utc_iso(timestamps[0])
    command = [
        "ffmpeg",
        "-hide_banner",
        "-loglevel",
        "error",
        "-i",
        str(input_path),
        "-map",
        "0:v:0",
    ]
    if has_audio:
        command += ["-map", "0:a:0"]
    command += [
        "-c:v",
        "libx264",
        "-preset",
        preset,
        "-crf",
        str(crf),
        "-pix_fmt",
        "yuv420p",
        "-vf",
        f"setpts={timestamp_setpts_expression(timestamps)}",
        "-fps_mode:v",
        "passthrough",
    ]
    if has_audio:
        command += ["-c:a", "aac"]
    command += [
        "-metadata",
        f"creation_time={creation_time}",
        "-metadata",
        f"{CREWTIMER_TIMESTAMP_TAG}={first_utc_us}",
        "-metadata:s:v:0",
        f"creation_time={creation_time}",
        "-metadata:s:v:0",
        f"{CREWTIMER_TIMESTAMP_TAG}={first_utc_us}",
        "-movflags",
        "+faststart+use_metadata_tags",
        "-y",
        str(output_path),
    ]
    run(command)


def validate_recording(
    input_path: Path,
    source_probe: dict[str, Any],
    sidecar: dict[str, Any] | None,
) -> int:
    issues = 0
    print(f"Validating {input_path}")
    video_stream = next(
        (
            stream
            for stream in source_probe.get("streams", [])
            if stream.get("codec_type") == "video"
        ),
        None,
    )
    if video_stream is None:
        print("ISSUE: MP4 contains no video stream.")
        return 1

    frame_offsets = source_frame_offsets(input_path)
    frame_count = len(frame_offsets)
    print(f"Frames: {frame_count}")
    pts_deltas = [
        frame_offsets[index] - frame_offsets[index - 1]
        for index in range(1, frame_count)
    ]
    nominal_pts_interval = (
        Decimal(str(statistics.median(pts_deltas))) if pts_deltas else Decimal(0)
    )
    expected_track_duration = frame_offsets[-1] + nominal_pts_interval
    duration_value = video_stream.get("duration") or source_probe.get("format", {}).get(
        "duration"
    )
    if duration_value is not None:
        track_duration = decimal_from_metadata(duration_value, "track duration")
        duration_error_ms = (track_duration - expected_track_duration) * 1000
        print(
            f"PTS duration: {track_duration:.6f} s; expected from final frame: "
            f"{expected_track_duration:.6f} s"
        )
        if abs(duration_error_ms) > Decimal(1):
            issues += 1
            print(
                f"ISSUE: track duration differs from final PTS plus one frame by "
                f"{duration_error_ms:+.3f} ms."
            )

    has_pts_clock = has_crewtimer_pts_clock(source_probe)
    if has_pts_clock:
        print(f"MP4 clock: {CREWTIMER_TIMESTAMP_TAG} with frame PTS")
    else:
        issues += 1
        print(
            f"ISSUE: MP4 lacks {CREWTIMER_TIMESTAMP_TAG}; the old-style embedded "
            "clock is required."
        )

    embedded_times: list[Decimal] | None
    try:
        embedded_times = embedded_frame_times(input_path, sidecar)
        print(f"Embedded clock: decoded {len(embedded_times)} frame timestamps")
    except ClipError as error:
        embedded_times = None
        print(f"Embedded clock: unavailable ({error})")
        if not has_pts_clock:
            issues += 1
            print("ISSUE: no usable absolute frame clock was found.")

    if embedded_times is not None:
        if len(embedded_times) != frame_count:
            issues += 1
            print(
                f"ISSUE: embedded clock has {len(embedded_times)} timestamps but "
                f"the MP4 has {frame_count} frames."
            )
        if len(embedded_times) >= 2:
            _, nominal, discontinuities = timestamp_discontinuities(embedded_times)
            positive_gaps = [
                (index, interval)
                for index, interval in discontinuities
                if interval > nominal
            ]
            if not positive_gaps:
                print("Embedded timestamp gaps: none")
            for frame_index, interval in positive_gaps:
                issues += 1
                print(
                    "ISSUE: timestamp gap before frame "
                    f"{frame_index} at "
                    f"{local_clock_text(embedded_times[frame_index], sidecar)}: "
                    f"{interval * 1000:.3f} ms interval "
                    f"({(interval - nominal) * 1000:.3f} ms gap)."
                )

    if sidecar is None:
        issues += 1
        print("ISSUE: companion JSON sidecar is missing.")
    else:
        file_metadata = sidecar.get("file", {})
        json_count = file_metadata.get("numFrames")
        if json_count != frame_count:
            issues += 1
            print(
                f"ISSUE: JSON numFrames is {json_count!r}; MP4 contains {frame_count}."
            )
        else:
            print(f"JSON numFrames: {json_count} (matches MP4)")

        for field, embedded_value in (
            ("startTs", embedded_times[0] if embedded_times else None),
            ("stopTs", embedded_times[-1] if embedded_times else None),
        ):
            value = file_metadata.get(field)
            if value is None:
                issues += 1
                print(f"ISSUE: JSON file.{field} is missing.")
                continue
            json_time = decimal_from_metadata(value, f"JSON file.{field}")
            print(
                f"JSON {field}: {json_time:.7f} "
                f"({local_clock_text(json_time, sidecar)})"
            )
            if embedded_value is not None:
                error_ms = (json_time - embedded_value) * 1000
                if abs(error_ms) > Decimal(1):
                    issues += 1
                    print(
                        f"ISSUE: JSON {field} differs from the embedded frame clock "
                        f"by {error_ms:+.3f} ms."
                    )

    if has_pts_clock and embedded_times:
        pts_start = find_start_time(source_probe, sidecar)
        pts_last = pts_start + frame_offsets[-1]
        end_error_ms = (pts_last - embedded_times[-1]) * 1000
        if abs(end_error_ms) > Decimal(1):
            issues += 1
            print(
                "ISSUE: MP4 header + final PTS differs from the embedded final "
                f"timestamp by {end_error_ms:+.3f} ms."
            )

    if issues:
        print(f"Validation found {issues} issue(s).")
        return 1
    print("Validation passed with no timing issues.")
    return 0


def main() -> int:
    args = build_parser().parse_args()
    input_path = args.input.expanduser().resolve()
    if not input_path.is_file():
        raise ClipError(f"input file does not exist: {input_path}")
    if input_path.suffix.lower() != ".mp4":
        raise ClipError("input must be an MP4 file")
    if sum((args.fix, args.split, args.validate)) > 1:
        raise ClipError("--fix, --split, and --validate are mutually exclusive")
    if args.fix and (args.timestamp is not None or args.seconds is not None):
        raise ClipError("omit timestamp and seconds when using --fix")
    if args.in_place and not args.fix:
        raise ClipError("--in-place can only be used with --fix")
    if args.in_place and args.output:
        raise ClipError("--in-place and --output cannot be used together")
    if args.validate and (args.timestamp is not None or args.seconds is not None):
        raise ClipError("omit timestamp and seconds when using --validate")
    if not args.fix and not args.validate and args.timestamp is None:
        raise ClipError("timestamp is required unless --fix or --validate is used")
    if args.split and args.seconds is not None:
        raise ClipError("omit seconds when using --split")
    if args.split and args.overwrite and args.output:
        raise ClipError("omit --output when using --split with --overwrite")
    if not args.split and not args.fix and not args.validate and args.seconds is None:
        raise ClipError(
            "seconds is required unless --split, --fix, or --validate is used"
        )
    if args.seconds is not None and args.seconds <= 0:
        raise ClipError("seconds must be greater than zero")
    if not 0 <= args.crf <= 51:
        raise ClipError("--crf must be between 0 and 51")

    _, sidecar = read_sidecar(input_path)
    source_probe = probe_json(input_path, "-show_format", "-show_streams")
    if args.validate:
        return validate_recording(input_path, source_probe, sidecar)
    source_has_pts_clock = has_crewtimer_pts_clock(source_probe)
    use_embedded_clock = (
        not source_has_pts_clock and embedded_frame_time(input_path, 0) is not None
    )
    if use_embedded_clock and not args.fix:
        print(
            "Notice: using the old-style embedded frame clock instead of MP4 PTS.",
            file=sys.stderr,
        )
    input_start = find_start_time(source_probe, sidecar)
    duration_value = source_probe.get("format", {}).get("duration")
    if duration_value is None:
        raise ClipError("ffprobe did not report the input duration")
    input_duration = decimal_from_metadata(duration_value, "input duration")
    has_audio = any(
        stream.get("codec_type") == "audio"
        for stream in source_probe.get("streams", [])
    )
    video_stream = next(
        (
            stream
            for stream in source_probe.get("streams", [])
            if stream.get("codec_type") == "video"
        ),
        None,
    )
    if video_stream is None:
        raise ClipError("input contains no video stream")
    width = int(video_stream.get("width", 0))
    height = int(video_stream.get("height", 0))
    if width <= 0 or height <= 0:
        raise ClipError("ffprobe did not report valid video dimensions")
    if args.fix:
        if args.in_place:
            output_path = input_path
        else:
            output_path = (
                args.output.expanduser().resolve()
                if args.output
                else input_path.with_name(f"{input_path.stem}_fixed.mp4")
            )
        output_sidecar = output_path.with_suffix(".json")
        if output_path == input_path and not args.in_place:
            raise ClipError("output path must differ from input path")
        if (
            not args.in_place
            and not args.overwrite
            and (output_path.exists() or output_sidecar.exists())
        ):
            existing = output_path if output_path.exists() else output_sidecar
            raise ClipError(f"output already exists: {existing}; use --overwrite")
        output_path.parent.mkdir(parents=True, exist_ok=True)
        if source_has_pts_clock:
            print(
                "Notice: using the existing CrewTimer MP4 header and PTS; an "
                "embedded pixel clock (if present) will not be decoded.",
                file=sys.stderr,
            )
            timestamps = [
                input_start + offset for offset in source_frame_offsets(input_path)
            ]
        else:
            timestamps = embedded_frame_times(input_path, sidecar)
            print(
                "Notice: using the old-style embedded frame clock instead of MP4 PTS.",
                file=sys.stderr,
            )
        _, nominal_interval, discontinuities = timestamp_discontinuities(timestamps)
        for frame_index, interval in discontinuities:
            if interval > nominal_interval:
                excess = interval - nominal_interval
                print(
                    "Warning: timestamp gap detected before frame "
                    f"{frame_index} at {local_clock_text(timestamps[frame_index], sidecar)}: "
                    f"{interval * 1000:.3f} ms interval "
                    f"({excess * 1000:.3f} ms gap).",
                    file=sys.stderr,
                )
        if len(timestamps) != int(video_stream.get("nb_frames", len(timestamps))):
            raise ClipError("timestamp count does not match the video frame count")
        temporary_dir = Path(
            tempfile.mkdtemp(prefix=".crewtimer-fix-", dir=output_path.parent)
        )
        temporary_output = temporary_dir / output_path.name
        try:
            encode_fixed_video(
                input_path,
                temporary_output,
                timestamps,
                has_audio,
                args.preset,
                args.crf,
            )
            frame_count, _ = output_frame_stats(temporary_output)
            if frame_count != len(timestamps):
                raise ClipError("fixed output frame count does not match the input")
            temporary_sidecar = temporary_output.with_suffix(".json")
            write_sidecar(
                temporary_sidecar,
                sidecar,
                width,
                height,
                timestamps[0],
                timestamps[-1],
                frame_count,
            )
            if args.overwrite and not args.in_place:
                output_path.unlink(missing_ok=True)
                output_sidecar.unlink(missing_ok=True)
            temporary_output.replace(output_path)
            temporary_sidecar.replace(output_sidecar)
        finally:
            shutil.rmtree(temporary_dir, ignore_errors=True)
        print(f"Wrote {output_path}")
        print(f"Wrote {output_sidecar}")
        print(
            f"Fixed {frame_count} frames: {utc_iso(timestamps[0])} to "
            f"{utc_iso(timestamps[-1])}"
        )
        return 0

    assert args.timestamp is not None
    center_time = parse_timestamp(args.timestamp, sidecar, input_start, input_duration)
    frame_offsets = source_frame_offsets(input_path)
    frame_times = source_frame_wall_times(input_start, frame_offsets, sidecar)
    if center_time < frame_times[0] or center_time > frame_times[-1]:
        raise ClipError(
            "timestamp is outside the input: video spans "
            f"{utc_iso(frame_times[0])} through {utc_iso(frame_times[-1])}"
        )
    if args.split:
        # A contains only frames before the requested wall time; B starts with
        # the first frame at or after it. Choosing the nearest frame can put a
        # pre-split frame in B and shift its displayed clock by one frame.
        split_index = bisect_left(frame_times, center_time)
        if use_embedded_clock:
            split_index = refine_split_index_from_embedded_time(
                input_path, split_index, center_time, len(frame_times)
            )
        if split_index <= 0 or split_index >= len(frame_times):
            raise ClipError("split time must leave at least one frame in each output")
        if args.overwrite:
            jobs = [
                (input_path, 0, split_index),
                (
                    timestamped_video_path(
                        input_path, frame_times[split_index], sidecar
                    ),
                    split_index,
                    len(frame_times),
                ),
            ]
        else:
            base_path = (
                args.output.expanduser().resolve() if args.output else input_path
            )
            jobs = [
                (base_path.with_name(f"{base_path.stem}A.mp4"), 0, split_index),
                (
                    base_path.with_name(f"{base_path.stem}B.mp4"),
                    split_index,
                    len(frame_times),
                ),
            ]
    else:
        assert args.seconds is not None
        start_index = nearest_frame_index(
            max(frame_times[0], center_time - args.seconds), frame_times
        )
        end_index = nearest_frame_index(
            min(frame_times[-1], center_time + args.seconds), frame_times
        )
        end_index = max(start_index + 1, end_index)
        if args.output:
            output_path = args.output.expanduser().resolve()
        else:
            timestamp_label = datetime.fromtimestamp(
                float(center_time), tz=timezone.utc
            ).strftime("%Y%m%d_%H%M%S")
            output_path = input_path.with_name(
                f"{input_path.stem}_clip_{timestamp_label}.mp4"
            )
        jobs = [(output_path, start_index, min(end_index, len(frame_times)))]

    for output_path, _, _ in jobs:
        output_sidecar = output_path.with_suffix(".json")
        replacing_split_input = (
            args.split and args.overwrite and output_path == input_path
        )
        if output_path == input_path and not replacing_split_input:
            raise ClipError("output path must differ from input path")
        if not args.overwrite and (output_path.exists() or output_sidecar.exists()):
            existing = output_path if output_path.exists() else output_sidecar
            raise ClipError(f"output already exists: {existing}; use --overwrite")
        output_path.parent.mkdir(parents=True, exist_ok=True)

    temporary_dir = Path(
        tempfile.mkdtemp(prefix=".crewtimer-clip-", dir=jobs[0][0].parent)
    )
    try:
        completed = []
        for index, (output_path, start_frame, end_frame) in enumerate(jobs):
            temporary_output = temporary_dir / f"{index}-{output_path.name}"
            source_start = (
                embedded_frame_time(input_path, start_frame)
                if use_embedded_clock
                else None
            )
            source_stop = (
                embedded_frame_time(input_path, end_frame - 1)
                if use_embedded_clock
                else None
            )
            output_start, frame_count, last_frame_offset = encode_clip(
                input_path,
                temporary_output,
                start_frame,
                end_frame,
                frame_offsets[start_frame],
                frame_offsets[end_frame]
                if end_frame < len(frame_offsets)
                else input_duration,
                source_start or frame_times[start_frame],
                has_audio,
                args.preset,
                args.crf,
            )
            completed.append(
                (
                    output_path,
                    temporary_output,
                    output_start,
                    source_stop or frame_times[end_frame - 1],
                    frame_count,
                    last_frame_offset,
                )
            )

        for (
            output_path,
            temporary_output,
            output_start,
            output_stop,
            frame_count,
            last_frame_offset,
        ) in completed:
            output_sidecar = output_path.with_suffix(".json")
            if args.overwrite:
                output_path.unlink(missing_ok=True)
                output_sidecar.unlink(missing_ok=True)
            temporary_output.replace(output_path)
            write_sidecar(
                output_sidecar,
                sidecar,
                width,
                height,
                output_start,
                output_stop,
                frame_count,
            )
            print(f"Wrote {output_path}")
            print(f"Wrote {output_sidecar}")
            print(
                f"Clip: {utc_iso(output_start)} to "
                f"{utc_iso(output_stop)} ({frame_count} frames)"
            )
    finally:
        shutil.rmtree(temporary_dir, ignore_errors=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except ClipError as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)
