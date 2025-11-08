# 🕒 CrewTimer MP4 Timestamp Encoding Specification

**Version:** 1.0  
**Last updated:** 2025-10-22  
**Applies to:** CrewTimer Video Review, Recorders for CrewTimer Video Review and compatible ingest pipelines  

---

## Overview

CrewTimer MP4 files embed **precise UTC timing metadata** to synchronize captured video segments with external timing systems (e.g., finish-line clocks, telemetry, or event logs).

The timestamps are written **without modifying frame PTS/DTS values**, ensuring that the MP4 files remain compliant with standard media players and editors while preserving accurate capture-time information in metadata.

Timestamps for individual frames can be derived from the **UTC timing medata** and **PTS** values for each frame.

---

## Goals

- Represent the **absolute wall-clock time** (UTC) corresponding to the **first video frame** in each file.  
- Use a **lossless, monotonic epoch timestamp** format with microsecond precision.  
- Ensure interoperability across FFmpeg-based and non-FFmpeg encoders.  
- Allow multiple streams (video, audio) within the same MP4 file to share a consistent capture start time.

---

## Metadata Fields

### 1. `com.crewtimer.first_utc_us`

| Field | Type | Units | Scope | Example |
|:------|:------|:------|:------|:--------|
| `com.crewtimer.first_utc_us` | String (integer) | **microseconds since Unix epoch (UTC)** | Container & per-stream | `"1734893104123456"` |

- Represents the **UTC time of the first encoded frame** (usually the first keyframe) in the segment.  
- Stored as a 64-bit integer encoded in **decimal string form** (to match MP4 metadata conventions).  
- The same value is written to:
  - The **container** (`format->metadata`)
  - Each **stream** (`stream[i]->metadata`)

The microsecond timestamp can be generated as:

```cpp
using namespace std::chrono;
int64_t utc_us = duration_cast<microseconds>(
    system_clock::now().time_since_epoch()).count();
```

This yields the number of microseconds elapsed since **1970-01-01T00:00:00Z**.

---

### 2. `creation_time`

| Field | Type | Format | Scope | Example |
|:------|:------|:------|:------|:--------|
| `creation_time` | String | ISO-8601 UTC with microseconds | Container & per-stream | `"2025-10-22T07:45:12.123456Z"` |

- Standard MP4/QuickTime field recognized by most players and editors.  
- Provides a **human-readable** UTC timestamp corresponding to `com.crewtimer.first_utc_us`.  
- This field is redundant but serves as a fallback when integer precision isn’t needed.

**Why store both fields?** `creation_time` is convenient for humans, but many muxers and editing tools rewrite it (sometimes truncating to milliseconds or applying local time). The vendor tag `com.crewtimer.first_utc_us` remains untouched, preserves the full 64-bit epoch value, and is trivial for ingest pipelines to parse without timezone handling. Keeping both ensures we always retain an exact, machine-friendly capture start time even if the ISO string is later normalized.

Generated as:

```cpp
"%04d-%02d-%02dT%02d:%02d:%02d.%03dZ"
```

---

### 3. (Optional) `PRFT` Box

If the FFmpeg build supports it, the muxer is instructed to write a **Producer Reference Time (PRFT)** atom:

```cpp
av_opt_set_int(ofmt->priv_data, "write_prft", 1, 0);
av_opt_set(ofmt->priv_data, "prft", "wallclock", 0);
```

This provides a direct mapping between presentation timestamps (PTS) and wall-clock time at the first sample.  
Players or analysis tools that understand PRFT boxes can use this to estimate real-time playback alignment.

---

## Encoding Rules

1. **Clock Source:**  
   All timestamps use the system’s **UTC clock** at the time of segment start (`system_clock::now()`).

2. **Resolution:**  
   Microseconds (1 × 10⁻⁶ s).  
   For example,  
   ```
   1734893104123456 → 2025-10-22T07:45:04.123456Z
   ```

3. **Representation:**  
   Decimal ASCII string (no separators, no decimals).

4. **Synchronization:**  
   The same `com.crewtimer.first_utc_us` value applies to **all streams** within the file.  
   Each file’s timestamp marks the time of the first written **keyframe** or **first valid packet** after rotation.

---

## Writing the Metadata

### Writing with FFmpeg CLI

To generate compatible files from other sources (e.g., RTSP, SDI, NDI, or camera ingest), simply:

1. Capture the **system UTC timestamp** when the first encoded frame is written.  
2. Add both fields to the output container metadata:

```bash
ffmpeg -i input.mp4 -metadata creation_time="2025-10-22T07:45:12.123Z" \
       -metadata com.crewtimer.first_utc_us="1734893104123456" -c copy output.mp4
```

3. Ensure all subsequent files follow the same epoch encoding format.


### Writing with FFmpeg C/C++ API

```c++
    int64_t utc_us = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
    AVFormatContext *ofmt = nullptr; 
    const AVOutputFormat *o = av_guess_format("mp4", nullptr, nullptr);
    if (!o) {
      die("av_guess_format(mp4)", AVERROR_MUXER_NOT_FOUND);
    }

    if (int err = avformat_alloc_output_context2(&ofmt, o, nullptr, nullptr); err < 0) {
      die("avformat_alloc_output_context2", err);
    }

    av_opt_set_int(ofmt->priv_data, "write_prft", 1, 0);
    av_opt_set(ofmt->priv_data, "prft", "wallclock", 0);
    
    // Prepare values
    const std::string iso = iso8601_utc_now_us(utc_us);

    char utc_us_buf[32];
    std::snprintf(utc_us_buf, sizeof(utc_us_buf), "%lld", (long long)utc_us);

    // Stamp container + streams with custom key
    av_dict_set(&ofmt->metadata, "creation_time", iso.c_str(), 0);
    av_dict_set(&ofmt->metadata, "com.crewtimer.first_utc_us", utc_us_buf, 0);

    for (unsigned i = 0; i < ofmt->nb_streams; ++i)
    {
      av_dict_set(&ofmt->streams[i]->metadata, "creation_time", iso.c_str(), 0);
      av_dict_set(&ofmt->streams[i]->metadata, "com.crewtimer.first_utc_us", utc_us_buf, 0);
    }

    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "movflags", "use_metadata_tags", 0); // allow custom tags
    int err = avformat_write_header(ofmt, &opts);
    av_dict_free(&opts);
    if (err < 0)
    {
      die("Error: Cannot write mp4 header");
    }
```

---

## Reading the Metadata

### Reading with FFmpeg CLI

```bash
ffprobe -v quiet -show_entries format_tags=com.crewtimer.first_utc_us   -of default=nw=1:nk=1 your_file.mp4
```

Example output:
```
1734893104123456
```

---

### Reading with FFmpeg C/C++ API

```cpp
AVFormatContext* fmt = nullptr;
avformat_open_input(&fmt, "your_file.mp4", nullptr, nullptr);
avformat_find_stream_info(fmt, nullptr);

AVDictionaryEntry* e = av_dict_get(fmt->metadata, "com.crewtimer.first_utc_us", nullptr, 0);
uint64_t first_utc_us = 0;
if (e) {
    std::cout << "Capture start (UTC us): " << e->value << std::endl;
    first_utc_us = std::stoull(e->value);
}
avformat_close_input(&fmt);
```

---


## Validation

You can verify proper tagging using:

```bash
ffprobe -show_entries format_tags -of json your_file.mp4 | jq
```

Expected output snippet:
```json
{
  "tags": {
    "creation_time": "2025-10-22T07:45:12.123Z",
    "com.crewtimer.first_utc_us": "1734893104123456"
  }
}
```

---

## Implementation Compatibility

| Encoder / Tool | Compatibility | Notes |
|:----------------|:---------------|:------|
| **FFmpeg ≥ 4.0** | ✅ | Reads/writes arbitrary tags |
| **VLC** | ⚠️ | Displays `creation_time` only |
| **MP4Box (GPAC)** | ✅ | Preserves custom tags |
| **QuickTime / macOS Preview** | ⚠️ | Ignores custom metadata |
| **CrewTimer Video Recorder** | ✅ | Native support |
| **RiaB Basler Recorder** | ✅ | Native support |
| **CrewTimer Video Review Tools** | ✅ | Parses `com.crewtimer.first_utc_us` as epoch-µs |

---

## Summary

| Field | Description | Format | Example |
|:------|:-------------|:--------|:---------|
| `com.crewtimer.first_utc_us` | UTC timestamp of first frame | Integer (µs since 1970-01-01 UTC) | `1734893104123456` |
| `creation_time` | ISO-8601 UTC timestamp | `YYYY-MM-DDTHH:MM:SS.mmmZ` | `2025-10-22T07:45:12.123Z` |
| PRFT (optional) | Wall-clock reference | ISO base-media box | `prft=wallclock` |

These values allow independent systems (SRT receivers, NDI recorders, camera SDKs) to **encode synchronized MP4 files** with a shared reference epoch and precise alignment across capture devices.
