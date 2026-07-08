# 02 — xb-inspector: Xbox Native Library

## 1. Public API

```cpp
// xray/inspector.hpp

namespace xb {

// Initialize the Inspector.
// Spawns network thread, attempts bind on port 9000 (fallback 9000-9009).
// Non-blocking: returns immediately, doesn't stall the app if port is busy.
// Thread-safe: call from any thread.
void Inspector::start();

// Shut down Inspector, close socket, join network thread.
void Inspector::stop();

// Logging via spdlog + TCP forward.
using LogLevel = enum { DEBUG, INFO, WARN, ERROR, FATAL };

void Inspector::log(LogLevel level, std::string_view tag, std::string_view msg);
void Inspector::log_info(std::string_view tag, std::string_view msg);
void Inspector::log_warn(std::string_view tag, std::string_view msg);
void Inspector::log_error(std::string_view tag, std::string_view msg);

// Shorthand without tag (uses "GENERAL").
void Inspector::log_info(std::string_view msg);
void Inspector::log_warn(std::string_view msg);
void Inspector::log_error(std::string_view msg);

// (REPL - phase 3) Register variable/object for Lua inspection.
// Creates a Sol2 binding so the remote terminal can read and
// modify the value at runtime.
template <typename T>
void Inspector::bind(std::string_view name, T* ptr);

// (REPL - phase 3) Register a struct/class type with metadata.
template <typename T>
void Inspector::bind_type(std::string_view name);

// Connection state (for debug UI or polling).
bool Inspector::is_connected();

} // namespace xb
```

## 2. Thread Model

```
┌──────────────────────────────────────────────────────────┐
│                     PROCESS (Xbox)                        │
│                                                           │
│  ┌─────────────────┐    ┌──────────────────────────────┐ │
│  │  MAIN THREAD    │    │  NETWORK THREAD              │ │
│  │  (game loop)    │    │  (xray-sock listener)        │ │
│  │                 │    │                              │ │
│  │  while(running){│    │  while(running){             │ │
│  │    // consume   │    │    accept() non-blocking     │ │
│  │    // commands  │    │    recv() → json parse       │ │
│  │    // from SPSC │◄───│    → push to SPSC queue      │ │
│  │                 │    │    → pop from MPSC queue     │ │
│  │    update()     │    │    → json stringify          │ │
│  │    draw()       │    │    → send() non-blocking     │ │
│  │  }              │    │  }                           │ │
│  └─────────────────┘    └──────────────────────────────┘ │
│                                                           │
│  ┌─────────────────┐    ┌──────────────────────────────┐ │
│  │  AUDIO THREAD   │    │  RENDER THREAD              │ │
│  │  (any)          │    │  (any)                      │ │
│  │                 │    │                              │ │
│  │  log() → MPSC   │    │  log() → MPSC               │ │
│  └─────────────────┘    └──────────────────────────────┘ │
└──────────────────────────────────────────────────────────┘
```

### 2.1 Network Thread

- Created by `Inspector::start()`
- Binds TCP socket on first free port in range 9000-9009
- Runs `select()` or `WSAPoll()` non-blocking
- **Does NOT execute Lua** — only enqueues received commands

### 2.2 Main Thread (Game Loop)

- Consumes the SPSC queue **at the start of each frame** (before `update()`)
- Executes received Lua script
- Pushes result back via MPSC queue
- If queue is empty, does nothing — zero per-frame overhead

### 2.3 Log Producer Threads

- Any thread: audio, render, IO, worker
- Call `Inspector::log()` which pushes onto the MPSC queue
- O(1) lock-free operation (moodycamel::ConcurrentQueue or equivalent)

## 3. Thread-Safe Queues

### MPSC — Logs (Multi-Producer, Single-Consumer)

```
Producer: any app thread
Consumer: network thread
Capacity: 4096 messages (configurable)
Overflow policy: drop-oldest (discards oldest)
```

Suggested implementation: `moodycamel::ConcurrentQueue` (lock-free, header-only) or custom atomic ring buffer.

### SPSC — Commands (Single-Producer, Single-Consumer)

```
Producer: network thread
Consumer: main thread (game loop)
Capacity: 64 commands (REPL doesn't need burst)
Overflow policy: drop-newest (discards command if queue full)
```

Suggested implementation: lock-free ring buffer with atomic indices (simpler than MPSC, no CAS needed).

## 4. Lifecycle

```
start() called
    │
    ▼
Spawn network thread
    │
    ▼
Attempt bind on port 9000
    │
    ├── OK ──► Listen ──► Accept loop
    │
    └── FAILED (port busy)
            │
            ▼
        Try 9001, 9002... up to 9009
            │
            ├── OK ──► Listen ──► Accept loop
            │
            └── FAILED (whole range busy)
                    │
                    ▼
                Log warn "xray-sock: no ports available"
                Thread sleeps, retries every 30s
```

### Suspend/Resume (UWP)

When Xbox suspends the app:

1. `Suspending` event fires
2. `Inspector` stops network thread, closes socket
3. Port 9000-9009 is freed
4. On resume, `Inspector::start()` is called again
5. Re-binds port, awaits new connections

**Note:** On Xbox, "Games" category apps are typically **terminated** on exit, not suspended. The re-bind logic covers both scenarios regardless.

## 5. Library Dependencies

| Dependency | Type | Purpose |
|---|---|---|
| Winsock2 (`ws2_32.lib`) | SDK | Non-blocking TCP socket |
| `nlohmann/json.hpp` | Header-only (external/) | JSON serialization |
| `spdlog` | Header-only (external/) | Logging |
| `moodycamel/ConcurrentQueue` | Header-only (future) | MPSC queue (or custom implementation) |
| `Lua 5.4` | Static lib (prebuilt) | Lua interpreter |
| `Sol2` | Header-only (external/) | C++ ↔ Lua binding |
