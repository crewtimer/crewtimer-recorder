// SrtReconnectingReader.hpp
// Requires FFmpeg (libavformat, libavcodec, libavutil). Works for any URL, but
// intended for "srt://..." inputs. C++17+ for <atomic>, <chrono>, <string>.
#pragma once

#include "../SystemEventQueue.hpp"

extern "C"
{
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

#include <atomic>
#include <chrono>
#include <thread>
#include <string>
#include <iostream>
#include <functional>

struct SrtReconnectConfig
{
  std::string url;                // e.g. "srt://:9000?mode=listener&latency=120"
  int64_t open_timeout_ms = 5000; // max time to block in open/read before interrupt
  int64_t read_timeout_ms = 5000; // max time to block in av_read_frame before interrupt
  int max_retries = -1;           // -1 = forever; otherwise max attempts per reconnect
  int base_backoff_ms = 250;      // exponential backoff base delay
  int max_backoff_ms = 4000;      // cap for backoff delay
                                  // Optional SRT/FFmpeg URL options as AVDictionary (use "srt_" / protocol keys via URL string usually).
                                  // If you still want to set options programmatically, you can extend this struct.
};

class SrtReconnectingReader
{
public:
  explicit SrtReconnectingReader(const SrtReconnectConfig &cfg)
      : cfg_(cfg)
  {
    resetInterruptState();
  }

  ~SrtReconnectingReader()
  {
    close();
  }

  // Start or re-establish connection (blocks until success or cancelled/timeout)
  bool open()
  {
    close();
    cancelled_ = false;

    using clock = std::chrono::steady_clock;
    auto attempt = 0;
    int backoff = cfg_.base_backoff_ms;

    while (!cancelled_)
    {
      if (!tryOpenOnce(cfg_.open_timeout_ms))
      {
        if (cancelled_)
          return false;
        if (cfg_.max_retries >= 0 && attempt >= cfg_.max_retries)
        {
          SystemEventQueue::push("SRT", "[SRT] Reconnect: reached max attempts");
          return false;
        }
        // Backoff before retry
        attempt++;
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
        backoff = std::min(backoff * 2, cfg_.max_backoff_ms);
        continue;
      }

      // Found streams (do this after open for codecs/extradata)
      if (avformat_find_stream_info(fmt_, nullptr) < 0)
      {
        SystemEventQueue::push("SRT", "Error: avformat_find_stream_info failed; retrying");
        close();
        continue;
      }

      // Reset read blocking window now that we're connected
      last_io_tick_ = clock::now();
      connectionGeneration_.fetch_add(1, std::memory_order_acq_rel);
      // Reset the disconnect notification flag so future disconnects will be reported.
      disconnectNotified_.store(false);
      return true;
    }
    return false;
  }

  // Non-owning; still owned by reader and valid until next reconnect/close
  AVFormatContext *formatContext() const { return fmt_; }

  uint64_t connectionGeneration() const
  {
    return connectionGeneration_.load(std::memory_order_acquire);
  }

  // Reads next packet; on dropout it will *attempt to reconnect* and continue.
  // Returns 0 on success (pkt filled), <0 if cancelled or unrecoverable error.
  int readFrame(AVPacket *pkt)
  {
    using clock = std::chrono::steady_clock;

    for (;;)
    {
      if (!fmt_)
      {
        // Not open yet or after failure — try to open.
        if (!open())
          return AVERROR_EXIT;
      }

      // Update read deadline window and call read
      io_deadline_ms_ = cfg_.read_timeout_ms;
      last_io_tick_ = clock::now();

      int ret = av_read_frame(fmt_, pkt);
      if (ret >= 0)
      {
        return 0; // success
      }

      // EAGAIN: transient; try again unless cancelled/timeout
      if (ret == AVERROR(EAGAIN))
      {
        if (cancelled_)
          return AVERROR_EXIT;
        continue;
      }

      // EOF or I/O errors -> attempt reconnect
      SystemEventQueue::push("SRT", std::string("[SRT] read error ") + std::to_string(ret) + " (" + av_err2str2(ret) + "), reconnecting...");

      // Notify once immediately that we've detected a disconnect and are attempting reconnect.
      if (onDisconnect_ && !disconnectNotified_.exchange(true))
      {
        try
        {
          onDisconnect_();
        }
        catch (...)
        {
        }
      }

      av_packet_unref(pkt);
      if (!reconnect())
      {
        // give up (cancelled or max retries hit)
        return ret;
      }
      // after reconnect, loop retries read
    }
  }

  // Register a callback invoked when the reader gives up reconnecting.
  void setDisconnectCallback(std::function<void()> cb)
  {
    onDisconnect_ = std::move(cb);
  }

  void cancel()
  {
    cancelled_ = true;
  }

  void close()
  {
    cancel();
    if (fmt_)
    {

      std::cerr << "SrtReconnectingReader: closing\n";
      AVFormatContext *tmp = fmt_;
      fmt_ = nullptr; // avoid re-entrancy in interrupt callback
      avformat_close_input(&tmp);
    }
  }

private:
  // Small helper to stringify FFmpeg error codes
  static const char *av_err2str2(int errnum)
  {
    thread_local char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, errbuf, sizeof(errbuf));
    return errbuf;
  }

  bool reconnect()
  {
    close();
    return open();
  }

  bool tryOpenOnce(int64_t timeout_ms)
  {
    using clock = std::chrono::steady_clock;

    fmt_ = avformat_alloc_context();
    if (!fmt_)
      return false;

    // Configure interrupt callback so open/read don’t block forever
    fmt_->interrupt_callback.callback = &SrtReconnectingReader::interruptThunk;
    fmt_->interrupt_callback.opaque = this;

    // Set the open deadline
    last_io_tick_ = clock::now();
    io_deadline_ms_ = timeout_ms;

    AVDictionary *opts = nullptr;
    // Snappier lock-on for live inputs and fix bad timestamps:
    // nobuffer -> lower latency
    // genpts   -> generate PTS if missing
    // igndts   -> ignore broken DTS
    av_dict_set(&opts, "probesize", "256k", 0);
    av_dict_set(&opts, "analyzeduration", "1000000", 0); // 1.0s
    av_dict_set(&opts, "fflags", "+nobuffer+genpts+igndts", 0);

    // Tip: you can also pass AVDictionary* options here if desired
    int ret = avformat_open_input(&fmt_, cfg_.url.c_str(), nullptr, &opts);
    if (ret < 0)
    {
      char errbuf[256];
      av_strerror(ret, errbuf, sizeof(errbuf));
      SystemEventQueue::push("SRT", std::string("Error: avformat_open_input failed for ") + cfg_.url + " -> " + errbuf);
      av_dict_free(&opts);
      close();
      return false;
    }

    return true;
  }

  // Interrupt callback fires when (now - last_io_tick_) > io_deadline_ms_ OR cancelled_
  static int interruptThunk(void *opaque)
  {
    return static_cast<SrtReconnectingReader *>(opaque)->shouldInterrupt() ? 1 : 0;
  }

  bool shouldInterrupt() const
  {
    if (cancelled_)
      return 1;
    using clock = std::chrono::steady_clock;
    if (io_deadline_ms_ <= 0)
      return 0;
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - last_io_tick_).count();
    return (elapsed > io_deadline_ms_) ? 1 : 0;
  }

  void resetInterruptState()
  {
    last_io_tick_ = std::chrono::steady_clock::now();
    io_deadline_ms_ = cfg_.open_timeout_ms;
    cancelled_ = false;
    disconnectNotified_.store(false);
  }

private:
  SrtReconnectConfig cfg_;
  AVFormatContext *fmt_ = nullptr;

  // interrupt / timing
  std::atomic<bool> cancelled_{false};
  std::chrono::steady_clock::time_point last_io_tick_{};
  std::atomic<int64_t> io_deadline_ms_{0};
  std::atomic<bool> disconnectNotified_{false};
  std::atomic<uint64_t> connectionGeneration_{0};
  std::function<void()> onDisconnect_;
};

// -------------------- Example usage --------------------
/*
int main() {
    av_log_set_level(AV_LOG_WARNING);

    SrtReconnectConfig cfg;
    cfg.url = "srt://:9000?mode=listener&transtype=live&latency=120&rcvbuf=262144";
    cfg.open_timeout_ms = 8000;
    cfg.read_timeout_ms = 5000;
    cfg.max_retries = -1; // forever
    cfg.base_backoff_ms = 250;
    cfg.max_backoff_ms = 3000;

    SrtReconnectingReader reader(cfg);
    if (!reader.open()) {
        std::cerr << "Initial open failed.\n";
        return 1;
    }

    AVPacket pkt;
    av_init_packet(&pkt);

    while (true) {
        int r = reader.readFrame(&pkt);
        if (r < 0) {
            std::cerr << "Reader stopped with error: " << r << "\n";
            break;
        }

        // Process packet...
        // pkt.stream_index, pkt.pts/dts, pkt.data, pkt.size
        av_packet_unref(&pkt);
    }

    reader.close();
    return 0;
}
*/
