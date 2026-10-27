#pragma once

#include <cstdint>

namespace TimingConstants
{
// Frame timestamps use 100 ns units. A positive correction is subtracted from
// the source timestamp before any recorded-output timing is derived.
constexpr std::uint64_t GlobalTimestampCorrection100ns = 11ULL * 10000ULL;

// With overlapped recording, each file after the first also holds at least this much of
// the previous file's end, rounded back to a keyframe. Covers Video Review's Auto Zoom
// search (120 frames at 60 fps).
constexpr std::uint64_t RecordingOverlap100ns = 2ULL * 10000000ULL;

// A timed or requested split waits until the file has this much new video, after any
// overlap, so every overlap is complete and only neighbouring files overlap. Stopping,
// a reconnect or a source change still closes the file at once.
constexpr std::uint64_t MinimumNewVideo100ns = 2ULL * 10000000ULL;
}
