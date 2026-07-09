# 03 — Logging System

## 1. Overview

The logging system combines **spdlog** (header-only, sink-based) with two custom sinks:

```
  App code
      │
      ▼
  spdlog logger (level: debug)
      │
      ├──► [uwp_file_sink]   ──► D:\logs\xray.log  (rotation: 5 files)
      │                           └──► OutputDebugStringA()  (always)
      │
      └──► [uwp_net_sink]    ──► MPSC queue ──► network thread ──► TCP ──► Vault
                                    │
                                    └── Only when Vault connected + level >= INFO
```

## 2. spdlog Configuration

Set up internally by `Inspector::start()`:

```cpp
// Pattern: [HH:MM:SS.sss] [LEVEL] [tag] message
self_->logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] [%n] %v");
self_->logger->set_level(spdlog::level::debug);
```

## 3. Sinks

### uwp_file_sink

| Property | Value |
|---|---|
| Path candidates | `D:\logs\` (Xbox external drive) — first writable wins |
| Fallback | If no candidate works, file logging disabled |
| Rotation | 5 files: `xray.log` (current), `xray.1.log` .. `xray.4.log` |
| Rotation trigger | On every `Inspector::start()` |
| Encoding | UTF-8 |
| Minimum level | `DEBUG` (all) |
| OutputDebugString | Always, regardless of file availability |

The sink uses `GetDriveTypeA()` + `CreateDirectoryA()` to test candidates at construction. If `D:\` is not present (PC dev, Xbox without external drive), it silently disables file writes — `OutputDebugStringA()` still works.

Rotation shifts `xray.log → xray.1.log`, `xray.1.log → xray.2.log`, etc., deleting `xray.4.log`.

### uwp_net_sink

| Property | Value |
|---|---|
| Active | Only when TCP connection is established |
| Transport | Formats log as JSON → MPSC queue → network thread → `send()` |
| Minimum level | `INFO` (DEBUG/trace filtered at sink level) |
| Queue | 4096 messages, drop-oldest |
| Filter | `set_level_filter(spdlog::level::info)` |

If no Vault is connected, `uwp_net_sink` returns at the `if (!connected_)` check — zero network overhead.

## 4. Per-Sink Level Filtering

| Sink | Receives | Filtered out |
|---|---|---|
| `uwp_file_sink` | DEBUG, INFO, WARN, ERROR, FATAL | — |
| `OutputDebugString` | DEBUG, INFO, WARN, ERROR, FATAL | — |
| `uwp_net_sink` | INFO, WARN, ERROR, FATAL | DEBUG, TRACE |

Filtering is explicit in `uwp_net_sink::sink_it_()` — not relying on spdlog's per-sink level mechanism (which operates at the logger level, not per sink).

## 5. Backpressure

When the Vault is slow to consume, the MPSC queue (4096 slots) fills up.

```
Vault                                          Xbox
  │                                              │
  │◄────── log ◄────── log ◄────── log ◄──────  │ (fast: 10k logs/s)
  │   (UI thread busy, not consuming fast)       │
  │                                              │
  │   MPSC queue full ──► drop oldest + count    │
```

### Policy

1. MPSC `try_enqueue` returns false when full → item is deleted + drop count incremented
2. Network thread `drain_log_queue()` checks `queue.drop_count()`
3. If drops > 0, sends synthetic WARN message:
   ```json
   {"event":"log","payload":{"level":"WARN","message":"Net queue full, dropped 142 messages"}}
   ```
4. Counter resets after each warning

## 6. Log File Format

```
[15:30:01.002] [INFO] [xray] Engine initialized
[15:30:01.015] [DEBUG] [xray] [AUDIO] XAudio2 mastering voice created
[15:30:02.100] [WARN] [xray] [RENDER] Texture 'ui_bg.png' not found, using default
[15:30:05.001] [ERROR] [xray] [FS] Failed to open save slot 2: access denied
```

## 7. Log API

```cpp
// Basic usage
xb::Inspector::log_info("Engine initialized");
xb::Inspector::log_warn("AUDIO", "Buffer underrun");
xb::Inspector::log_error("RENDER", "Shader compilation failed");

// Conditional macros (compile-time no-op if XB_INSPECTOR_ENABLED not defined)
XRAY_INFO("AUDIO", "Mixer created");
XRAY_WARN("FS", "File not found: %s", path);
XRAY_ERROR("NET", "Connection timeout");
```
