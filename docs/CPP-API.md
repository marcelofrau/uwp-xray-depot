# 02 — xb-inspector: Xbox Native Library

## 1. Public API

```cpp
// xray/inspector.hpp

namespace xb {

// ── Lifecycle ──

// Initialize the Inspector.
// Spawns network thread, attempts bind on port 9000 (fallback 9000-9009).
// Non-blocking: returns immediately, doesn't stall if port is busy.
// Thread-safe.
void Inspector::start(const char* app_name = nullptr);

// Must be called once per frame at frame start, on the main thread.
// Consumes REPL commands from SPSC queue, executes Lua, pushes results.
void Inspector::update();

// Shut down Inspector, close socket, join network thread, destroy Lua state.
void Inspector::stop();

// ── Logging ──

// Log via spdlog. Dispatches to file + OutputDebugString + TCP (if connected).
void Inspector::log(LogLevel level, const char* tag, const char* msg);
void Inspector::log_info(const char* tag, const char* msg);
void Inspector::log_warn(const char* tag, const char* msg);
void Inspector::log_error(const char* tag, const char* msg);

// Shorthand without tag.
void Inspector::log_info(const char* msg);
void Inspector::log_warn(const char* msg);
void Inspector::log_error(const char* msg);

// ── Lua REPL Binding ──

// Bind a scalar variable (int, float, bool).
// Creates Lua userdata that reads/writes the pointer directly.
template <typename T>
void Inspector::bind(const char* name, T* ptr);

// Bind an array (int[], float[]).
// Creates Lua userdata with __index/__newindex/__len/__pairs.
template <typename T>
void Inspector::bind_array(const char* name, T* ptr, int len);

// ── Status ──

// Returns true if Vault is connected to the TCP socket.
bool Inspector::is_connected();

// Returns the port that was actually bound (9000-9009).
uint16_t Inspector::bound_port();

} // namespace xb
```

### LogLevel

```cpp
enum class LogLevel : int {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERROR = 3,
    FATAL = 4
};
```

### Macros (conditional on `XB_INSPECTOR_ENABLED`)

```cpp
#define XRAY_LOG(level, tag, ...)   xb::Inspector::log(level, tag, __VA_ARGS__)
#define XRAY_INFO(tag, ...)         xb::Inspector::log_info(tag, __VA_ARGS__)
#define XRAY_WARN(tag, ...)         xb::Inspector::log_warn(tag, __VA_ARGS__)
#define XRAY_ERROR(tag, ...)        xb::Inspector::log_error(tag, __VA_ARGS__)
```

Without `XB_INSPECTOR_ENABLED`, all macros expand to nothing.

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
│  │    update()     │    │    tick() → drain_log_queue()│ │
│  │    // consume   │    │    accept() non-blocking     │ │
│  │    // commands  │◄───│    recv() → json parse       │ │
│  │    // from SPSC │    │    → push to SPSC queue      │ │
│  │                 │    │                              │ │
│  │    game.update()│    │    → pop from MPSC queue     │ │
│  │    game.draw()  │    │    → send() non-blocking     │ │
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
- Runs `select()` non-blocking
- **Does NOT execute Lua** — only enqueues received commands to SPSC
- Drains MPSC queue (logs + REPL results) and sends over TCP

### 2.2 Main Thread (Game Loop)

- Calls `Inspector::update()` **at the start of each frame** (before game logic)
- Consumes SPSC queue, executes Lua, sends results back via MPSC
- If queue is empty, does nothing — zero per-frame overhead

### 2.3 Log Producer Threads

- Any thread: audio, render, IO, worker
- Call `Inspector::log()` which formats via spdlog and pushes to file + net sinks
- Net sink enqueues JSON to MPSC only when Vault is connected
- O(1) lock-free operation

## 3. Thread-Safe Queues

### MPSC — Logs + Results (Multi-Producer, Single-Consumer)

```
Producer: any app thread (logs), main thread (REPL results)
Consumer: network thread
Capacity: 4096 messages
Overflow: drop-oldest (oldest log silently discarded)
```

### SPSC — Commands (Single-Producer, Single-Consumer)

```
Producer: network thread
Consumer: main thread (Inspector::update)
Capacity: 64 commands
Overflow: drop-newest (command discarded if main thread not consuming)
```

## 4. Lifecycle

```
start("MyApp") called
    │
    ├── Init Lua state + sandbox
    ├── Init spdlog (file + OutputDebugString + net sinks)
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
                Log warn "no ports available"
                Thread sleeps, retries every 30s
```

### Suspend/Resume (UWP)

When Xbox suspends the app:

1. `Suspending` event fires
2. `Inspector::stop()` — closes socket, joins network thread, destroys Lua state
3. Port 9000-9009 is freed
4. On resume, `Inspector::start()` is called again
5. Re-binds port, re-initializes Lua, awaits new connections

## 5. Library Dependencies

| Dependency | Type | Purpose |
|---|---|---|
| Winsock2 (`ws2_32.lib`) | SDK | Non-blocking TCP socket |
| `nlohmann/json.hpp` | Header-only (external/) | JSON serialization |
| `spdlog` | Header-only (external/) | Logging |
| `Lua 5.4` | Static lib (prebuilt) | Lua interpreter |
| `xray-sock` | Static lib (prebuilt) | TCP listener + safe queues |

**Not used: Sol2, moodycamel/ConcurrentQueue.** Binding is done via raw Lua C API. Queue is custom lock-free ring buffer.
