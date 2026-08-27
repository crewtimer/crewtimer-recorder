#pragma once

#include <cstdint>

namespace TimingConstants
{
// Frame timestamps use 100 ns units. A positive correction is subtracted from
// the source timestamp before any recorded-output timing is derived.
constexpr std::uint64_t GlobalTimestampCorrection100ns = 11ULL * 10000ULL;
}
