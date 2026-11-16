#!/usr/bin/env python3
"""Keep only the video a regatta's timing points need.

Reads the regatta's times at a hint waypoint (and optionally a scored
waypoint) from CrewTimer, then writes a short MP4 and JSON sidecar for each
group of crossings, the way the recorder splits with overlap: every crossing
lies within the middle 2 s of a 4 s file, and files overlap where crossings
are close together. Video with no crossing is left out.

Video Review's scoring records (interpolation.json and each sidecar's
timestamps) are carried over for the times kept, pointing at the new files.

The destination folder can be opened in CrewTimer Video Review to score or
rescore the regatta.
"""

from __future__ import annotations

import argparse
from bisect import bisect_left
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field
from datetime import date, datetime, timedelta, timezone
from fractions import Fraction
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
from typing import Any
import urllib.error
import urllib.parse
import urllib.request


CREWTIMER_TIMESTAMP_TAG = "com.crewtimer.first_utc_us"
PROD_DATABASE = "https://crewtimer-results.firebaseio.com"
DEV_DATABASE = "https://crewtimer-results-dev.firebaseio.com"

FILE_SECONDS = 4.0
MIDDLE_SECONDS = 2.0
# Crossings grouped into one file span less than the middle, leaving a little
# room for frame quantization at its edges.
GROUP_SPAN_SECONDS = MIDDLE_SECONDS - 0.05
# The recorder splits files on keyframes every 12 frames, and Video Review may
# not read the last frame of a file that is not a multiple of it.
KEY_FRAME_INTERVAL = 12
# Recordings closer than this are treated as one continuous stretch of video.
MAX_GAP_SECONDS = 0.5
HALF_DAY_SECONDS = 12 * 3600
EPOCH_DATE = date(1970, 1, 1)


class OptimizeError(Exception):
    """An error that can be reported without a traceback."""


# ---------------------------------------------------------------- utilities


def run(command: list[str], *, capture: bool = False) -> subprocess.CompletedProcess:
    try:
        return subprocess.run(command, check=True, text=True, capture_output=capture)
    except FileNotFoundError as exc:
        raise OptimizeError(f"{command[0]} was not found in PATH") from exc
    except subprocess.CalledProcessError as exc:
        detail = (exc.stderr or "").strip()
        raise OptimizeError(
            f"{command[0]} failed: {detail}" if detail else f"{command[0]} failed"
        ) from exc


def probe_json(path: Path, *arguments: str) -> dict[str, Any]:
    result = run(
        ["ffprobe", "-v", "error", *arguments, "-of", "json", str(path)], capture=True
    )
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        raise OptimizeError(f"ffprobe returned invalid JSON for {path}") from exc


def utc_iso(epoch_us: int) -> str:
    seconds, micros = divmod(epoch_us, 1_000_000)
    value = datetime.fromtimestamp(seconds, tz=timezone.utc).replace(microsecond=micros)
    return value.isoformat(timespec="microseconds").replace("+00:00", "Z")


def local_text(epoch: float, tz_minutes: int) -> str:
    value = datetime.fromtimestamp(epoch, tz=timezone(timedelta(minutes=tz_minutes)))
    return value.strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]


def fraction_text(value: Fraction) -> str:
    """An exact value as an ffmpeg expression."""
    return f"({value.numerator}/{value.denominator})"


# ------------------------------------------------------------- timing data


@dataclass
class Crossing:
    waypoint: str
    time: str  # local time of day as entered, HH:MM:SS.sss
    seconds_of_day: float
    entered_ms: int | None  # lap Timestamp, when the time was recorded
    label: str
    uuid: str = ""  # the lap's uuid, which Video Review's scoring records are keyed by
    epoch: float = 0.0


def gate_from_waypoint(waypoint: str) -> str:
    if waypoint == "Start":
        return "S"
    if waypoint == "Finish":
        return "F"
    return f"G_{waypoint}"


def database_for(regatta_id: str) -> tuple[str, str]:
    """Database URL and path id; t. or 9 prefixes select the dev database."""
    if regatta_id.startswith("t.") or regatta_id.startswith("9"):
        return DEV_DATABASE, re.sub(r"^(t\.|9)", "", regatta_id)
    return PROD_DATABASE, regatta_id


def fetch_json(url: str) -> Any:
    request = urllib.request.Request(url, headers={"User-Agent": "crewtimer-optimize"})
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            return json.load(response)
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", "replace").strip()
        raise OptimizeError(f"CrewTimer request failed ({exc.code}): {detail}") from exc
    except (urllib.error.URLError, TimeoutError) as exc:
        raise OptimizeError(f"could not reach CrewTimer: {exc}") from exc


def regatta_waypoints(regatta_id: str) -> tuple[str, list[str]] | None:
    """The regatta's title and waypoints, or None when it has no regatta info."""
    base, path_id = database_for(regatta_id)
    info = fetch_json(f"{base}/web/{urllib.parse.quote(path_id)}/regattaInfo.json")
    if not isinstance(info, dict):
        return None
    waypoints = [
        name.strip() for name in str(info.get("Waypoints") or "").split(",") if name.strip()
    ]
    return str(info.get("Title") or regatta_id), ["Start", *waypoints, "Finish"]


def fetch_laps(regatta_id: str, waypoint: str) -> list[dict[str, Any]]:
    base, path_id = database_for(regatta_id)
    query = urllib.parse.urlencode(
        {"orderBy": json.dumps("Gate"), "equalTo": json.dumps(gate_from_waypoint(waypoint))}
    )
    data = fetch_json(f"{base}/regatta/{urllib.parse.quote(path_id)}/lapdata.json?{query}")
    return laps_with_uuid(data)


def laps_with_uuid(data: Any) -> list[dict[str, Any]]:
    """Laps from a lapdata map or list; a lap without a uuid takes its key, as Video Review does."""
    if isinstance(data, dict):
        return [
            {**lap, "uuid": lap.get("uuid") or key} for key, lap in data.items() if isinstance(lap, dict)
        ]
    if isinstance(data, list):
        return [lap for lap in data if isinstance(lap, dict)]
    return []


def load_laps_file(path: Path, waypoint: str) -> list[dict[str, Any]]:
    """Laps from a saved lapdata export: a map of laps, or a list of them."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise OptimizeError(f"could not read {path}: {exc}") from exc
    gate = gate_from_waypoint(waypoint)
    return [lap for lap in laps_with_uuid(data) if lap.get("Gate") == gate]


TIME_PATTERN = re.compile(r"^\s*(\d{1,2}):(\d{1,2}):(\d{1,2}(?:\.\d*)?)\s*$")


def crossings_from_laps(laps: list[dict[str, Any]], waypoint: str) -> list[Crossing]:
    crossings = []
    for lap in laps:
        if lap.get("State") == "Deleted":
            continue
        match = TIME_PATTERN.match(str(lap.get("Time") or ""))
        if not match:
            continue
        hours, minutes = int(match[1]), int(match[2])
        seconds = float(match[3])
        if hours > 23 or minutes > 59 or seconds >= 60:
            continue
        entered = lap.get("Timestamp")
        label = " ".join(
            part
            for part in (
                f"event {lap['EventNum']}" if lap.get("EventNum") else "",
                f"bow {lap['Bow']}" if lap.get("Bow") else "",
            )
            if part
        )
        crossings.append(
            Crossing(
                waypoint=waypoint,
                time=match[0].strip(),
                seconds_of_day=hours * 3600 + minutes * 60 + seconds,
                entered_ms=int(entered) if isinstance(entered, (int, float)) else None,
                label=label,
                uuid=str(lap.get("uuid") or ""),
            )
        )
    return crossings


# ------------------------------------------------------------ source video


@dataclass
class SourceFile:
    path: Path
    sidecar: dict[str, Any]
    start: float  # first frame, epoch seconds
    stop: float  # last frame, epoch seconds
    num_frames: int
    tz_minutes: int
    width: int
    height: int
    frames: list[tuple[float, int]] | None = None  # (epoch seconds, pts), in pts order
    time_base: Fraction | None = None
    first_pts: int = 0
    # Frame time = start_epoch + (pts - first_pts) * seconds_per_tick
    start_epoch: Fraction = Fraction(0)
    seconds_per_tick: Fraction = Fraction(0)

    @property
    def fps(self) -> float:
        if self.num_frames > 1 and self.stop > self.start:
            return (self.num_frames - 1) / (self.stop - self.start)
        return 60.0


@dataclass
class Coverage:
    """A stretch of continuous video from one or more source files."""

    files: list[SourceFile] = field(default_factory=list)
    start: float = 0.0
    stop: float = 0.0

    @property
    def tz_minutes(self) -> int:
        return self.files[0].tz_minutes


def read_sources(folder: Path) -> tuple[list[SourceFile], int]:
    sources = []
    skipped = 0
    for video in sorted(folder.glob("*.mp4")):
        if video.name.startswith("."):
            continue
        sidecar_path = video.with_suffix(".json")
        try:
            sidecar = json.loads(sidecar_path.read_text(encoding="utf-8"))
            info = sidecar["file"]
            start = float(info["startTs"])
            stop = float(info["stopTs"])
            num_frames = int(info["numFrames"])
        except (OSError, json.JSONDecodeError, KeyError, TypeError, ValueError):
            print(f"Warning: skipping {video.name}; it has no recorder sidecar", file=sys.stderr)
            skipped += 1
            continue
        source = sidecar.get("source") or {}
        sources.append(
            SourceFile(
                path=video,
                sidecar=sidecar,
                start=start,
                stop=stop,
                num_frames=num_frames,
                tz_minutes=int(info.get("tzOffset") or 0),
                width=int(source.get("width") or 0),
                height=int(source.get("height") or 0),
            )
        )
    sources.sort(key=lambda item: item.start)
    return sources, skipped


def coverage_from_sources(sources: list[SourceFile]) -> list[Coverage]:
    stretches: list[Coverage] = []
    for source in sources:
        current = stretches[-1] if stretches else None
        if (
            current is not None
            and source.start <= current.stop + MAX_GAP_SECONDS
            and (source.width, source.height) == (current.files[-1].width, current.files[-1].height)
            and source.tz_minutes == current.tz_minutes
        ):
            current.files.append(source)
            current.stop = max(current.stop, source.stop)
        else:
            stretches.append(Coverage([source], source.start, source.stop))
    return stretches


def load_frames(source: SourceFile) -> None:
    """Read each frame's capture time from the MP4 header clock and packet PTS."""
    if source.frames is not None:
        return
    probe = probe_json(
        source.path,
        "-select_streams",
        "v:0",
        "-show_entries",
        f"stream=time_base:stream_tags={CREWTIMER_TIMESTAMP_TAG}"
        f":format_tags={CREWTIMER_TIMESTAMP_TAG}:packet=pts",
        "-show_packets",
    )
    streams = probe.get("streams") or []
    if not streams:
        raise OptimizeError(f"{source.path.name} has no video stream")
    numerator, denominator = str(streams[0]["time_base"]).split("/")
    time_base = Fraction(int(numerator), int(denominator))
    pts_values = sorted(
        int(packet["pts"]) for packet in probe.get("packets", []) if packet.get("pts") is not None
    )
    if len(pts_values) < 2:
        raise OptimizeError(f"{source.path.name} has fewer than two video frames")
    tag = (probe.get("format", {}).get("tags") or {}).get(CREWTIMER_TIMESTAMP_TAG) or (
        streams[0].get("tags") or {}
    ).get(CREWTIMER_TIMESTAMP_TAG)
    first_pts = pts_values[0]
    if tag is not None:
        start_epoch = Fraction(int(tag), 1_000_000)
        seconds_per_tick = time_base
    else:
        # Older recordings have no header clock; their frames are spread
        # evenly between the sidecar's first and last frame times.
        print(
            f"Warning: {source.path.name} has no {CREWTIMER_TIMESTAMP_TAG}, so its frame "
            "times are spread between its sidecar start and stop; for exact times "
            "convert it first with video-utils.py --fix",
            file=sys.stderr,
        )
        start_epoch = Fraction(str(source.sidecar["file"]["startTs"]))
        span = Fraction(str(source.sidecar["file"]["stopTs"])) - start_epoch
        seconds_per_tick = span / (pts_values[-1] - first_pts)
    source.time_base = time_base
    source.first_pts = first_pts
    source.start_epoch = start_epoch
    source.seconds_per_tick = seconds_per_tick
    source.frames = [
        (float(start_epoch + (pts - first_pts) * seconds_per_tick), pts) for pts in pts_values
    ]


# ---------------------------------------------------------------- planning


def local_date(epoch: float, tz_minutes: int) -> date:
    return EPOCH_DATE + timedelta(days=int((epoch + tz_minutes * 60) // 86400))


@dataclass
class Placement:
    placed: list[tuple[float, Crossing, Coverage]]
    missing: list[Crossing]  # no video holds the time
    other_day: list[Crossing]  # entered live on a day other than --day
    ambiguous: list[Crossing]  # in video on more than one day; all were kept
    # Placed at the same moment as an earlier crossing, so sharing its file
    same_time: list[tuple[float, Crossing]] = field(default_factory=list)


def resolve_epochs(
    crossings: list[Crossing],
    stretches: list[Coverage],
    tz_override: int | None,
    only_day: date | None,
) -> Placement:
    """Place each local time of day on the date of the video that holds it.

    A lap's Timestamp is when its time was entered. Times entered live are
    within moments of the crossing and identify its day; times entered or
    imported later have a Timestamp unrelated to the video, so it only counts
    when it is near a recording.
    """

    def tz_of(stretch: Coverage) -> int:
        return stretch.tz_minutes if tz_override is None else tz_override

    video_days = set()
    for stretch in stretches:
        day = local_date(stretch.start, tz_of(stretch))
        while day <= local_date(stretch.stop, tz_of(stretch)):
            video_days.add((day, tz_of(stretch)))
            day += timedelta(days=1)

    placed: dict[float, tuple[float, Crossing, Coverage]] = {}
    result = Placement([], [], [], [])
    for crossing in crossings:
        entered = crossing.entered_ms / 1000 if crossing.entered_ms is not None else None
        if only_day is not None and entered is not None:
            # Entered live on another day this folder has video for.
            entered_days = {day for day, tz in video_days if local_date(entered, tz) == day}
            if entered_days and only_day not in entered_days:
                result.other_day.append(crossing)
                continue
        candidates: list[tuple[float, Coverage]] = []
        for stretch in stretches:
            offset = tz_of(stretch) * 60
            day = local_date(stretch.start, tz_of(stretch))
            while day <= local_date(stretch.stop, tz_of(stretch)):
                if only_day is None or day == only_day:
                    epoch = (day - EPOCH_DATE).days * 86400 - offset + crossing.seconds_of_day
                    if stretch.start <= epoch <= stretch.stop:
                        candidates.append((epoch, stretch))
                day += timedelta(days=1)
        if not candidates:
            result.missing.append(crossing)
            continue
        if len(candidates) > 1:
            nearest = (
                min(candidates, key=lambda item: abs(item[0] - entered))
                if entered is not None
                else None
            )
            if nearest is not None and abs(nearest[0] - entered) < HALF_DAY_SECONDS:
                candidates = [nearest]
            else:
                result.ambiguous.append(crossing)
        for epoch, stretch in candidates:
            key = round(epoch, 3)
            if key not in placed:
                placed[key] = (epoch, crossing, stretch)
            else:
                result.same_time.append((epoch, crossing))
    result.placed = sorted(placed.values(), key=lambda item: item[0])
    return result


@dataclass
class Plan:
    stretch: Coverage
    times: list[tuple[float, Crossing]]
    center: float
    name: str = ""
    # Filled in from the frames:
    # Runs of consecutive frames from one file: (file, first pts, last pts, frames)
    selections: list[tuple[SourceFile, int, int, int]] = field(default_factory=list)
    out_times: list[float] = field(default_factory=list)
    first_frame: tuple[SourceFile, int] | None = None  # (file, pts)
    reference: SourceFile | None = None

    def first_epoch(self) -> Fraction:
        """Exact capture time of the plan's first frame."""
        assert self.first_frame is not None
        source, pts = self.first_frame
        return source.start_epoch + (pts - source.first_pts) * source.seconds_per_tick


def group_crossings(placed: list[tuple[float, Crossing, Coverage]]) -> list[Plan]:
    """Group crossings that fit together in the middle 2 s of one file."""
    plans: list[Plan] = []
    for epoch, crossing, stretch in placed:
        current = plans[-1] if plans else None
        if (
            current is not None
            and current.stretch is stretch
            and epoch - current.times[0][0] <= GROUP_SPAN_SECONDS
        ):
            current.times.append((epoch, crossing))
        else:
            plans.append(Plan(stretch, [(epoch, crossing)], epoch))
    for plan in plans:
        plan.center = (plan.times[0][0] + plan.times[-1][0]) / 2
    return plans


def file_prefix(path: Path) -> str:
    match = re.search(r"\d{8}_\d{6}", path.stem)
    return path.stem[: match.start()] if match else "CT_"


def name_plans(plans: list[Plan], prefix: str | None) -> None:
    """Name files like the recorder: prefix plus the local time of the first frame."""
    used: set[str] = set()
    for plan in plans:
        reference = plan.reference or plan.stretch.files[0]
        start = plan.out_times[0] if plan.out_times else plan.center - FILE_SECONDS / 2
        milli = int(start * 1000 + 0.5)
        local = datetime.fromtimestamp(milli // 1000, tz=timezone.utc) + timedelta(
            minutes=reference.tz_minutes
        )
        base = (prefix if prefix is not None else file_prefix(reference.path)) + local.strftime(
            "%Y%m%d_%H%M%S"
        )
        name = base
        suffix = 2
        while name in used:
            name = f"{base}_{suffix}"
            suffix += 1
        used.add(name)
        plan.name = name


def select_frames(plan: Plan) -> None:
    """Choose the plan's frames, centered on its crossings, from its source files."""
    low = plan.center - FILE_SECONDS
    high = plan.center + FILE_SECONDS
    files = [item for item in plan.stretch.files if item.stop >= low and item.start <= high]
    for source in files:
        load_frames(source)

    # The file holding the center keeps as many frames as it has; the others
    # (overlapping or adjacent recordings) only fill in what it lacks.
    def center_distance(item: SourceFile) -> float:
        if item.start <= plan.center <= item.stop:
            return -min(plan.center - item.start, item.stop - plan.center)
        return min(abs(plan.center - item.start), abs(plan.center - item.stop))

    files.sort(key=center_distance)
    timeline: list[tuple[float, SourceFile, int]] = []
    times: list[float] = []
    for source in files:
        assert source.frames is not None
        period = 1 / source.fps
        for epoch, pts in source.frames:
            if epoch < low or epoch > high:
                continue
            index = bisect_left(times, epoch - period / 2)
            if index < len(times) and times[index] <= epoch + period / 2:
                continue  # an overlap frame already taken from another file
            times.insert(index, epoch)
            timeline.insert(index, (epoch, source, pts))

    if not timeline:
        return
    intervals = [b - a for a, b in zip(times, times[1:])]
    fps = 1 / statistics.median(intervals) if intervals else files[0].fps
    count = max(KEY_FRAME_INTERVAL, round(FILE_SECONDS * fps / KEY_FRAME_INTERVAL) * KEY_FRAME_INTERVAL)
    center_index = min(range(len(times)), key=lambda i: abs(times[i] - plan.center))
    first = max(0, min(center_index - count // 2, len(times) - count))
    chosen = timeline[first : first + count]
    plan.out_times = [epoch for epoch, _, _ in chosen]
    plan.first_frame = (chosen[0][1], chosen[0][2])
    plan.reference = timeline[center_index][1]

    runs: list[list[Any]] = []
    for _, source, pts in chosen:
        if runs and runs[-1][0] is source:
            runs[-1][2] = pts
            runs[-1][3] += 1
        else:
            runs.append([source, pts, pts, 1])
    plan.selections = [tuple(run) for run in runs]  # type: ignore[misc]


def crossing_warnings(plan: Plan) -> list[str]:
    """Crossings a recording's start or end kept out of the file's middle 2 s."""
    warnings = []
    middle_low = plan.out_times[0] + (FILE_SECONDS - MIDDLE_SECONDS) / 2
    middle_high = plan.out_times[-1] - (FILE_SECONDS - MIDDLE_SECONDS) / 2
    for epoch, crossing in plan.times:
        where = f"{crossing.waypoint} {crossing.time}"
        if epoch < plan.out_times[0] or epoch > plan.out_times[-1]:
            warnings.append(f"{where} is outside {plan.name}")
        elif not middle_low - 0.001 <= epoch <= middle_high + 0.001:
            warnings.append(f"{where} is near an edge of {plan.name}; its recording starts or ends close by")
    return warnings


# -------------------------------------------------- Video Review scoring records

INTERPOLATION_FILENAME = "interpolation.json"


def read_scoring_records(folder: Path, sources: list[SourceFile]) -> dict[str, dict[str, Any]]:
    """Video Review's scoring records by lap uuid.

    The folder's interpolation.json is the main copy; each sidecar's
    timestamps hold a copy of the records scored in that file, which Video
    Review falls back to.
    """
    records: dict[str, dict[str, Any]] = {}
    for source in sources:
        timestamps = source.sidecar.get("timestamps")
        if isinstance(timestamps, dict):
            for uuid, record in timestamps.items():
                if isinstance(record, dict):
                    records[str(uuid)] = record
    store_path = folder / INTERPOLATION_FILENAME
    if store_path.exists():
        try:
            store = json.loads(store_path.read_text(encoding="utf-8"))
            stored = store.get("records") if isinstance(store, dict) and store.get("version") == 1 else None
            if not isinstance(stored, dict):
                raise ValueError("not a version 1 store")
        except (OSError, ValueError) as exc:
            print(f"Warning: ignoring {store_path.name}: {exc}", file=sys.stderr)
        else:
            records.update({str(uuid): record for uuid, record in stored.items() if isinstance(record, dict)})
    return records


def frame_position(times: list[float], epoch: float) -> float:
    """Video Review's 1-based, fractional frame number for a time within a file."""
    if len(times) < 2:
        return 1.0
    index = min(max(bisect_left(times, epoch), 1), len(times) - 1)
    before, after = times[index - 1], times[index]
    fraction = (epoch - before) / (after - before) if after > before else 0.0
    return round(index + fraction, 6)


def assign_scoring_records(
    records: dict[str, dict[str, Any]], plans: list[Plan], placement: Placement
) -> tuple[dict[str, dict[str, dict[str, Any]]], int]:
    """Give each record for a kept time to the file holding its frame.

    Returns the rewritten records by plan name, and how many records were
    for kept times but are in none of the files.
    """
    epochs: dict[str, list[float]] = {}
    for epoch, crossing, _ in placement.placed:
        epochs.setdefault(crossing.uuid, []).append(epoch)
    for epoch, crossing in placement.same_time:
        epochs.setdefault(crossing.uuid, []).append(epoch)
    epochs.pop("", None)

    def span(plan: Plan) -> tuple[float, float]:
        if plan.out_times:
            return plan.out_times[0], plan.out_times[-1]
        return plan.center - FILE_SECONDS / 2, plan.center + FILE_SECONDS / 2  # a dry run

    assigned: dict[str, dict[str, dict[str, Any]]] = {}
    unplaced = 0
    for uuid, record in records.items():
        if uuid not in epochs:
            continue  # not one of the times kept
        frame_ms = record.get("frameTimestampMs")
        targets = [frame_ms / 1000] if isinstance(frame_ms, (int, float)) else epochs[uuid]
        best: tuple[tuple[int, int, float], Plan, float] | None = None
        for plan in plans:
            low, high = span(plan)
            for target in targets:
                if not low <= target <= high:
                    continue
                holds_time = any(crossing.uuid == uuid for _, crossing in plan.times)
                from_its_file = any(
                    source.path.name == record.get("videoFile") for source, _, _, _ in plan.selections
                )
                rank = (0 if holds_time else 1, 0 if from_its_file else 1, abs(target - plan.center))
                if best is None or rank < best[0]:
                    best = (rank, plan, target)
        if best is None:
            unplaced += 1
            continue
        _, plan, target = best
        moved = dict(record)
        moved["videoFile"] = f"{plan.name}.mp4"
        if isinstance(frame_ms, (int, float)) and plan.out_times:
            moved["frameNum"] = frame_position(plan.out_times, target)
        else:
            moved.pop("frameNum", None)  # it counted frames of the source file
        assigned.setdefault(plan.name, {})[uuid] = moved
    return assigned, unplaced


def write_scoring_store(destination: Path, records: dict[str, dict[str, Any]]) -> None:
    """Add records to the destination's interpolation.json, keeping any already there."""
    store_path = destination / INTERPOLATION_FILENAME
    merged: dict[str, Any] = {}
    if store_path.exists():
        try:
            existing = json.loads(store_path.read_text(encoding="utf-8"))
            if isinstance(existing, dict) and isinstance(existing.get("records"), dict):
                merged.update(existing["records"])
        except (OSError, json.JSONDecodeError) as exc:
            raise OptimizeError(f"could not read {store_path}: {exc}") from exc
    merged.update(records)
    temporary = destination / f".{INTERPOLATION_FILENAME}.partial"
    temporary.write_text(json.dumps({"version": 1, "records": merged}, indent=2) + "\n", encoding="utf-8")
    os.replace(temporary, store_path)


# ---------------------------------------------------------------- encoding


def encode(
    plan: Plan, destination: Path, crf: int, preset: str, records: dict[str, dict[str, Any]]
) -> int:
    """Write plan's MP4 and sidecar, with its scoring records; returns the MP4's size in bytes."""
    assert plan.reference is not None
    # Rounded down, so the first frame's PTS is zero and never negative: a
    # negative first PTS shifts the track and hides the first and last frames.
    out_start_us = math.floor(plan.first_epoch() * 1_000_000)
    out_start = Fraction(out_start_us, 1_000_000)
    time_bases = {source.time_base for source, _, _, _ in plan.selections}
    out_time_base = time_bases.pop() if len(time_bases) == 1 else Fraction(1, 1_000_000)
    assert out_time_base is not None

    # Each run of frames keeps its own frame spacing; concat joins the runs and
    # keeps the last frame's duration. The final setpts puts each run's first
    # frame at its capture time and the rest at their spacing from it. setpts
    # truncates, so each value gets half a tick to round it to the nearest.
    command = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin"]
    filters = []
    starts = []
    first_index = 0
    for index, (source, low_pts, high_pts, frames) in enumerate(plan.selections):
        assert source.time_base is not None
        # Seek near the frames (keeping the file's own PTS) instead of decoding
        # the whole recording; trim then takes the exact frames.
        seek = max(0.0, float((low_pts - source.first_pts) * source.time_base) - 1.0)
        command += ["-noaccurate_seek", "-ss", f"{seek:.6f}", "-copyts", "-i", str(source.path)]
        filters.append(
            f"[{index}:v:0]trim=start_pts={low_pts}:end_pts={high_pts + 1},"
            f"setpts=(PTS-STARTPTS)*{fraction_text(source.seconds_per_tick)}/TB+0.5,"
            f"settb=AVTB[v{index}]"
        )
        run_start = source.start_epoch + (low_pts - source.first_pts) * source.seconds_per_tick
        starts.append((first_index, run_start - out_start))
        first_index += frames
    placement = "PREV_OUTPTS+PTS-PREV_INPTS"
    for frame_index, offset in reversed(starts):
        placement = f"if(eq(N,{frame_index}),{fraction_text(offset)}/TB+0.5,{placement})"
    inputs = "".join(f"[v{index}]" for index in range(len(plan.selections)))
    filters.append(
        f"{inputs}concat=n={len(plan.selections)}:v=1:a=0,setpts='{placement}',"
        f"settb={out_time_base.numerator}/{out_time_base.denominator}[v]"
    )

    temporary = destination / f".{plan.name}.partial.mp4"
    final = destination / f"{plan.name}.mp4"
    sidecar_path = destination / f"{plan.name}.json"
    creation_time = utc_iso(out_start_us)
    command += [
        "-filter_complex",
        ";".join(filters),
        "-map",
        "[v]",
        "-an",
        "-c:v",
        "libx264",
        "-preset",
        preset,
        "-crf",
        str(crf),
        "-pix_fmt",
        "yuv420p",
        "-g",
        str(KEY_FRAME_INTERVAL),
        "-fps_mode:v",
        "passthrough",
        # Without this the encoder uses the nominal frame rate's time base,
        # snapping every frame to 1/fps and losing its capture time.
        "-enc_time_base:v",
        "filter",
        "-video_track_timescale",
        str(out_time_base.denominator // out_time_base.numerator),
        "-metadata",
        f"creation_time={creation_time}",
        "-metadata",
        f"{CREWTIMER_TIMESTAMP_TAG}={out_start_us}",
        "-metadata:s:v:0",
        f"creation_time={creation_time}",
        "-metadata:s:v:0",
        f"{CREWTIMER_TIMESTAMP_TAG}={out_start_us}",
        "-movflags",
        "+faststart+use_metadata_tags",
        "-y",
        str(temporary),
    ]
    try:
        run(command, capture=True)
        # Check what a reader decodes, not just what was muxed.
        probe = probe_json(
            temporary, "-select_streams", "v:0", "-show_entries", "stream=time_base:frame=pts", "-show_frames"
        )
        numerator, denominator = str(probe["streams"][0]["time_base"]).split("/")
        written_base = Fraction(int(numerator), int(denominator))
        pts_values = sorted(int(f["pts"]) for f in probe.get("frames", []) if f.get("pts") is not None)
        if len(pts_values) != len(plan.out_times):
            raise OptimizeError(f"decoded {len(pts_values)} frames, expected {len(plan.out_times)}")
        if pts_values[0] != 0:
            raise OptimizeError(f"first frame PTS is {pts_values[0]}, not 0")
        stop = out_start + pts_values[-1] * written_base
        if abs(float(stop) - plan.out_times[-1]) > 0.001:
            raise OptimizeError("frame timing does not match its source")

        sidecar = json.loads(json.dumps(plan.reference.sidecar))
        # The source's scoring records name it and its frame numbers; this
        # file gets its own, rewritten for it.
        sidecar.pop("timestamps", None)
        if records:
            sidecar["timestamps"] = records
        info = sidecar.setdefault("file", {})
        info["startTs"] = f"{float(out_start):.7f}"
        info["stopTs"] = f"{float(stop):.7f}"
        info["numFrames"] = len(pts_values)
        sidecar_path.write_text(json.dumps(sidecar, indent=2) + "\n", encoding="utf-8")
        # As the recorder does, the sidecar is in place before the video appears.
        os.replace(temporary, final)
    finally:
        temporary.unlink(missing_ok=True)
    return final.stat().st_size


# -------------------------------------------------------------------- main


def parse_day(value: str) -> date:
    try:
        return date.fromisoformat(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"{value!r} is not a date like 2025-10-18") from exc


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Copy only the video around a regatta's timing points into short, "
            "overlapping 4 s files that CrewTimer Video Review can score."
        )
    )
    parser.add_argument("regatta_id", help="CrewTimer regatta id, e.g. r12345")
    parser.add_argument("hint_waypoint", help="waypoint whose times locate the video, e.g. Finish")
    parser.add_argument("source", type=Path, help="folder of recorder MP4 and JSON files")
    parser.add_argument("destination", type=Path, help="folder for the optimized files")
    parser.add_argument(
        "-s", "--scored-waypoint", help="a second waypoint whose times also need video"
    )
    parser.add_argument(
        "--lapdata",
        type=Path,
        help="read times from a saved lapdata JSON file instead of CrewTimer",
    )
    parser.add_argument(
        "--day",
        type=parse_day,
        help=(
            "local date (YYYY-MM-DD) of the times, for a multi-day regatta; only "
            "that day's video is used"
        ),
    )
    parser.add_argument(
        "--tz-offset",
        type=int,
        help="minutes local time is ahead of UTC for the times (default: the recording's tzOffset)",
    )
    parser.add_argument("--prefix", help="file name prefix (default: the source file's, e.g. CT_)")
    parser.add_argument(
        "--crf", type=int, default=15, help="libx264 quality; lower is better (default: 15)"
    )
    parser.add_argument("--preset", default="medium", help="libx264 preset (default: medium)")
    parser.add_argument(
        "-j", "--jobs", type=int, default=min(4, os.cpu_count() or 1), help="files encoded at once"
    )
    parser.add_argument("--overwrite", action="store_true", help="replace files already in destination")
    parser.add_argument("-n", "--dry-run", action="store_true", help="show the files that would be written")
    parser.add_argument("-v", "--verbose", action="store_true", help="list each file's crossings")
    return parser


def main() -> int:
    # Progress and warnings appear in order when output is piped or logged.
    sys.stdout.reconfigure(line_buffering=True)  # type: ignore[union-attr]
    args = build_parser().parse_args()
    source_dir = args.source.expanduser().resolve()
    destination = args.destination.expanduser().resolve()
    if not source_dir.is_dir():
        raise OptimizeError(f"source folder does not exist: {source_dir}")
    if destination == source_dir:
        raise OptimizeError("destination must differ from source")
    if not 0 <= args.crf <= 51:
        raise OptimizeError("--crf must be between 0 and 51")
    if args.jobs < 1:
        raise OptimizeError("--jobs must be at least 1")

    waypoints = [args.hint_waypoint]
    if args.scored_waypoint and args.scored_waypoint != args.hint_waypoint:
        waypoints.append(args.scored_waypoint)

    crossings: list[Crossing] = []
    if args.lapdata:
        for waypoint in waypoints:
            found = crossings_from_laps(load_laps_file(args.lapdata, waypoint), waypoint)
            print(f"{waypoint}: {len(found)} times from {args.lapdata.name}")
            crossings += found
    else:
        regatta = regatta_waypoints(args.regatta_id)
        if regatta is None:
            print(f"Warning: regatta {args.regatta_id} has no regatta info", file=sys.stderr)
        else:
            print(f"Regatta: {regatta[0]}")
        for waypoint in waypoints:
            if regatta is not None and waypoint not in regatta[1]:
                known = regatta[1]
                print(
                    f"Warning: {waypoint} is not one of the regatta's waypoints ({', '.join(known)})",
                    file=sys.stderr,
                )
            found = crossings_from_laps(fetch_laps(args.regatta_id, waypoint), waypoint)
            print(f"{waypoint}: {len(found)} times")
            crossings += found
    if not crossings:
        raise OptimizeError("no timing points found")

    sources, _ = read_sources(source_dir)
    if not sources:
        raise OptimizeError(f"no recorder videos found in {source_dir}")
    stretches = coverage_from_sources(sources)
    placement = resolve_epochs(crossings, stretches, args.tz_offset, args.day)
    placed, missing = placement.placed, placement.missing
    if args.day and missing:
        # Most of these are other days' times.
        print(f"{len(missing)} times have no video on {args.day}" + ("" if args.verbose else " (-v lists them)"))
    if not args.day or args.verbose:
        for crossing in missing:
            print(
                f"Warning: no video for {crossing.waypoint} {crossing.time} {crossing.label}".rstrip(),
                file=sys.stderr,
            )
    if placement.other_day:
        print(f"Skipped {len(placement.other_day)} times entered on other days")
    for crossing in placement.ambiguous:
        print(
            f"Warning: {crossing.waypoint} {crossing.time} {crossing.label} is in video on "
            "more than one day; kept on each (use --day to choose)".replace("  ", " "),
            file=sys.stderr,
        )
    if not placed:
        raise OptimizeError("none of the timing points are in the source videos")

    plans = group_crossings(placed)
    print(
        f"{len(placed)} timing points in video ({len(missing)} without); "
        f"{len(plans)} files from {len(sources)} source videos"
    )

    records = read_scoring_records(source_dir, sources)

    if args.dry_run:
        name_plans(plans, args.prefix)
        if records:
            assigned, unplaced = assign_scoring_records(records, plans, placement)
            print(
                f"{sum(len(found) for found in assigned.values())} of {len(records)} Video Review "
                f"scoring records would be kept" + (f" ({unplaced} outside the files)" if unplaced else "")
            )
        for plan in plans:
            tz = plan.stretch.tz_minutes
            print(f"  {plan.name}  center {local_text(plan.center, tz)}  {len(plan.times)} times")
            if args.verbose:
                for epoch, crossing in plan.times:
                    print(f"      {crossing.waypoint} {crossing.time} {crossing.label}")
        return 0

    print("Reading source frame times...")
    for plan in plans:
        select_frames(plan)
    plans = [plan for plan in plans if plan.out_times]
    name_plans(plans, args.prefix)
    for plan in plans:
        for warning in crossing_warnings(plan):
            print(f"Warning: {warning}", file=sys.stderr)
    assigned, unplaced = assign_scoring_records(records, plans, placement)
    if unplaced:
        print(
            f"Warning: {unplaced} Video Review scoring records for kept times are outside the new files",
            file=sys.stderr,
        )

    destination.mkdir(parents=True, exist_ok=True)
    existing = [
        plan.name
        for plan in plans
        if (destination / f"{plan.name}.mp4").exists() or (destination / f"{plan.name}.json").exists()
    ]
    if existing and not args.overwrite:
        raise OptimizeError(
            f"{len(existing)} files already exist in {destination} (e.g. {existing[0]}); use --overwrite"
        )

    written_bytes = 0
    failures = 0
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {
            pool.submit(encode, plan, destination, args.crf, args.preset, assigned.get(plan.name, {})): plan
            for plan in plans
        }
        written: list[Plan] = []
        for done, future in enumerate(as_completed(futures), start=1):
            plan = futures[future]
            try:
                written_bytes += future.result()
            except OptimizeError as error:
                failures += 1
                print(f"Error: {plan.name}: {error}", file=sys.stderr)
                continue
            written.append(plan)
            line = f"[{done}/{len(plans)}] {plan.name}.mp4 ({len(plan.out_times)} frames)"
            if args.verbose:
                line += ": " + ", ".join(f"{c.waypoint} {c.time}" for _, c in plan.times)
            print(line)

    if records:
        kept = {uuid: record for plan in written for uuid, record in assigned.get(plan.name, {}).items()}
        if kept:
            write_scoring_store(destination, kept)
        print(f"Kept {len(kept)} of {len(records)} Video Review scoring records in {INTERPOLATION_FILENAME}")

    source_bytes = sum(source.path.stat().st_size for source in sources)
    print(
        f"Wrote {len(plans) - failures} files, {written_bytes / 1e6:.1f} MB "
        f"(source videos {source_bytes / 1e6:.1f} MB) to {destination}"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except OptimizeError as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        sys.exit(130)
