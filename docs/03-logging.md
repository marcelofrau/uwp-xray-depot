# 03 — Logging System

## 1. Overview

The logging system combines **spdlog** (header-only, sink-based) with a **custom UWP sink** that writes to a local file and optionally forwards over TCP when the Vault is connected.

```
  App code
      │
      ▼
  spdlog logger
      │
      ├──► [uwp_file_sink]  ──► LocalState/xray.log
      │
      └──► [uwp_net_sink]   ──► TCP ──► Vault (when connected)
                                  │
                                  ▼
                              [MPSC queue] ──► network thread ──► send()
```

## 2. spdlog Configuration

```cpp
#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>
#include <xray/inspector.hpp>

void xb::Inspector::init_logging() {
    auto app_data = Windows::Storage::ApplicationData::Current;
    auto log_path = app_data->LocalFolder->Path + "\\xray.log";

    auto file_sink = std::make_shared<uwp_file_sink>(log_path);
    auto net_sink  = std::make_shared<uwp_net_sink>();

    auto logger = std::make_shared<spdlog::logger>("xray", spdlog::sinks_init_list{file_sink, net_sink});
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] [%!] %v");

    // Debug mode: file + OutputDebugString only
    // Runtime mode: file + TCP forward
    // Both coexist; net_sink only transmits when connection is active
}
```

## 3. Sinks

### uwp_file_sink

Writes to `LocalState/xray.log` inside the UWP sandbox.

| Property | Value |
|---|---|
| Path | `ApplicationData::Current->LocalFolder->Path + "\\xray.log"` |
| Rotation | None (replaces on each startup) |
| Encoding | UTF-8 |
| Minimum level | `DEBUG` |

Also mirrors to `OutputDebugStringA()` for compatibility with Windows capture tools.

### uwp_net_sink

Forwards logs over TCP to the Vault.

| Property | Value |
|---|---|
| Active | Only when TCP connection is established |
| Transport | MPSC queue → network thread → `send()` |
| Minimum level | Configurable (default: `INFO`) |
| Queue | 4096 messages, drop-oldest |

If no Vault is connected, `uwp_net_sink` enqueues nothing — zero network overhead.

## 4. Backpressure Policy

When the Vault is slow to consume, the MPSC queue may fill up.

### Scenario

```
Vault                                          Xbox
  │                                              │
  │◄────── log ◄────── log ◄────── log ◄──────  │ (fast: 10k logs/s)
  │                                              │ (send() at same rate)
  │   (UI thread busy, not consuming fast)       │
  │                                              │
  │   ┌──── MPSC queue ────┐                     │
  │   │ [L1] [L2] [L3] ... │ ◄── overflow!       │
  │   └────────────────────┘                     │
```

### Policy: Drop-Oldest + Warning

1. When the queue reaches 4096 messages, the oldest is discarded
2. Every 64 drops (configurable), a synthetic message is injected:
   ```
   [WARN] [XB-INSPECTOR] 142 logs dropped due to backpressure
   ```
3. The Vault sees the warning in the feed and knows data was lost
4. The drop counter resets after each injection

### Conceptual implementation

```cpp
void uwp_net_sink::sink_it_(const spdlog::details::log_msg& msg) {
    if (!m_connected) return;

    auto json_msg = format_log_json(msg);
    if (!m_queue.try_enqueue(std::move(json_msg))) {
        // queue full: drop-oldest
        m_queue.try_dequeue(m_drop_buffer);  // discard oldest
        m_queue.try_enqueue(std::move(json_msg));  // retry
        m_drop_count++;

        if (m_drop_count >= 64) {
            inject_drop_warning(m_drop_count);
            m_drop_count = 0;
        }
    }
}
```

## 5. Log API

```cpp
// Basic usage
xb::Inspector::log_info("Engine initialized");
xb::Inspector::log_warn("AUDIO", "Buffer underrun");
xb::Inspector::log_error("RENDER", "Shader compilation failed: {}", error_msg);

// Equivalent to spdlog LOG_TAG
#define XRAY_LOG(level, tag, ...) \
    xb::Inspector::log(level, tag, fmt::format(__VA_ARGS__))

// Conditional macros (compile-time)
#define XRAY_INFO(tag, ...) XRAY_LOG(INFO, tag, __VA_ARGS__)
```

## 6. Log File Format

```
[08-07-2026 15:30:01.002] [INFO] [GENERAL] Engine initialized
[08-07-2026 15:30:01.015] [DEBUG] [AUDIO] XAudio2 mastering voice created
[08-07-2026 15:30:02.100] [WARN] [RENDER] Texture 'ui_bg.png' not found, using default
[08-07-2026 15:30:05.001] [ERROR] [FS] Failed to open save slot 2: access denied
```
