# 00 — System Architecture

## 1. Overview

**XB-Inspector** is a decentralized real-time diagnostics and remote execution suite designed for the UWP homebrew ecosystem on Xbox Dev Mode. The system works around the instability of Microsoft's official tools (Visual Studio Remote Debugger, Xbox Device Portal) by providing:

- Continuous real-time log capture
- Remote Lua-based REPL for runtime memory inspection/manipulation
- Zero dependency on Microsoft debug tooling

```
┌─────────────────────────────────────────────────────────────────┐
│                     LOCAL NETWORK (TCP/IP)                       │
│                                                                  │
│  ┌───────────────────────┐         ┌───────────────────────┐    │
│  │     XBOX SERIES X/S   │         │     DEV PC             │    │
│  │     (Dev Mode / UWP)  │         │  (XB Homebrew Vault)   │    │
│  │                       │         │                        │    │
│  │  ┌─────────────────┐  │   TCP   │  ┌──────────────────┐  │    │
│  │  │  Homebrew App    │  │◄───────►│  │  Inspector Tab   │  │    │
│  │  │  ┌───────────┐   │  │  JSON   │  │  ┌────────────┐  │  │    │
│  │  │  │xb-inspector│   │  │         │  │  │ Console    │  │  │    │
│  │  │  │ lib        │   │  │         │  │  │ (Log Feed) │  │  │    │
│  │  │  │           │   │  │         │  │  └────────────┘  │  │    │
│  │  │  │ - spdlog  │   │  │         │  │  ┌────────────┐  │  │    │
│  │  │  │ - Lua 5.4 │   │  │         │  │  │ REPL Input │  │  │    │
│  │  │  │ - Sockets │   │  │         │  │  └────────────┘  │  │    │
│  │  │  └───────────┘   │  │         │  └──────────────────┘  │    │
│  │  └─────────────────┘  │         │                        │    │
│  └───────────────────────┘         └───────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
```

## 2. Components

### Xbox Side — `xb-inspector` (this depot)

Native C++ library embedded in the homebrew. Responsibilities:

| Component | Role |
|---|---|
| `xb::Inspector` | Public API: start/stop/log/bind |
| `xray-sock` | Non-blocking TCP listener, port fallback (9000-9009) |
| `spdlog` + `uwp_sink` | File + network logging |
| `Lua 5.4` + Sol2 | Embedded interpreter for REPL |
| `safe_queue` | MPSC (logs) and SPSC (commands) thread-safe queues |
| `nlohmann/json` | Protocol serialization/deserialization |

### PC Side — `Inspector` (in XB Homebrew Vault)

Desktop interface in the separate [xb-homebrew-vault](https://github.com/marcelofrau/xb-homebrew-vault) repository.

| Component | Role |
|---|---|
| Console panel | Color-coded log feed (ERROR=red, WARN=yellow, INFO=green) |
| REPL Input | Command line for sending Lua scripts to Xbox |
| Scan | Button that scans ports 9000-9009 on the Xbox IP |
| State Watch | (future) Visual tree of mapped variables |

## 3. Data Flow

```
                   ┌──────────────────┐
                   │  Any Thread      │
                   │  (audio, render, │
                   │   IO, etc.)      │
                   │                  │
                   │  xb::log.info()  │
                   └────────┬─────────┘
                            │ MPSC queue
                            ▼
                   ┌──────────────────┐
                   │  Network Thread  │─── TCP ──► Vault (Console)
                   │  (xray-sock)     │◄── TCP ─── Vault (REPL input)
                   │                  │
                   └────────┬─────────┘
                            │ SPSC queue
                            ▼
                   ┌──────────────────┐
                   │  Main Thread     │
                   │  (game loop /    │
                   │   frame update)  │── executes Lua ──► mutates native state
                   │                  │
                   │  consume cmd     │
                   │  queue at frame  │
                   │  start           │
                   └──────────────────┘
```

### Flow directions:

| Direction | What | Queue | Producer | Consumer |
|---|---|---|---|---|
| Xbox → Vault | Logs, REPL results | MPSC (multi-producer) | Any app thread | Network thread |
| Vault → Xbox | REPL commands | SPSC (single-producer) | Network thread | Main thread (game loop) |

## 4. Architectural Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Connection direction | Xbox = passive TCP server | Vault already knows Xbox IP, no Zeroconf/mDNS needed |
| Port range | 9000-9009, sequential fallback | Covers multiple instances without port hashing |
| Scan | Manual button in Vault | App runs 100% without Vault connected |
| Protocol | JSON over raw TCP, versioned | Simple, multi-language, debuggable |
| Lua threading | Executed on main thread, not network | Prevents data races on game state |
| Security | `#ifdef XB_INSPECTOR_ENABLED` compile-time | Impossible to leak REPL in release builds |
| Log backpressure | Circular buffer + drop-oldest + synthetic warning | Slow Vault doesn't stall Xbox |
| spdlog | Header-only + custom UWP sink | Popular, sink-based, extensible |
| Lua 5.4 (not LuaJIT) | Smaller footprint, no patching needed | Occasional REPL doesn't need JIT |
| Sol2 (not raw Lua C API) | Declarative C++ struct binding | Less boilerplate, safer |
| nlohmann/json | Header-only, C++17 modern | Industry standard, zero build |

## 5. Non-Goals (explicit scope)

- Does NOT replace VS debugger for breakpoints/step-through
- Does NOT do advanced CPU/GPU profiling (maybe future)
- Does NOT replace Xbox Device Portal for deploy/install
- Does NOT work in release builds without `#ifdef XB_INSPECTOR_ENABLED`
- Does NOT have TLS/authentication (trusted LAN only, Dev Mode only)
