<p align="center">
  <img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="license">
  <img src="https://img.shields.io/badge/platform-Xbox%20Series%20%7C%20Windows%20UWP-blueviolet" alt="platform">
  <img src="https://img.shields.io/badge/language-C%2B%2B17-yellow" alt="language">
  <img src="https://img.shields.io/badge/Lua-5.4-00007C?logo=lua" alt="lua">
  <img src="https://img.shields.io/badge/build-manual-lightgrey" alt="build">
</p>

<h1 align="center">xb-xray</h1>

<p align="center">
  <strong>Remote diagnostics for UWP homebrews on Xbox Dev Mode.</strong><br>
  Real-time logs · Lua REPL · Live C++ variable inspection over TCP<br>
  <em>Zero dependency on Microsoft debug tooling.</em>
</p>

---

## Why?

**Developing UWP homebrews for Xbox Dev Mode is painful.**

The Visual Studio Remote Debugger frequently disconnects mid-session. The Xbox Device Portal web UI is slow and unreliable for iterative development. Every crash means re-deploying from scratch. You work in the dark.

**xb-xray is the flashlight.**

A lightweight C++ library you `add_subdirectory()` into your project. It opens a non-blocking TCP socket (port 9000-9009) and streams everything you need to debug remotely:

- **Live logs** — spdlog output over TCP, no OutputDebugString dependency
- **Live variables** — inspect and modify any C++ variable in real time via Lua
- **Struct navigation** — group related vars under dot notation (`perf.fps`)
- **No SDK required** — works on any UWP-compatible MSVC toolchain

Everything goes through a single header. No reflection, no codegen, no marshaling. Just pointers.

---

## Features

| What | How |
|------|-----|
| 🪵 **Log streaming** | spdlog → file + OutputDebugString + TCP (all three at once) |
| 💻 **Lua 5.4 REPL** | Full Lua — `if`, `for`, `math`, `string`, `pairs` — sandboxed |
| 🔗 **Live C++ binding** | `bind("hp", &hp)` → read/write from terminal, no marshaling |
| 🧱 **Struct binding** | `bind_struct("perf", &s, fields)` → `perf.fps` dot notation |
| 📦 **Array binding** | `bind_array("pos", arr, n)` → 1-based index, `#pos`, `pairs()` |
| 🔤 **String binding** | `bind_string("name", buf, N)` → read/write `char[]` live |
| 🔌 **Port scanning** | Auto-fallback 9000-9009, non-blocking network thread |
| 🛡️ **Safety** | `#ifdef XB_INSPECTOR_ENABLED` — zero overhead in Release |
| 📂 **File logging** | Timestamped files, 7-day auto-cleanup (Xbox: `E:\dosbox\logs\`) |

---

## Quick Start

### 1. Add to project

```cmake
# CMakeLists.txt
add_subdirectory(path/to/uwp-xray-depot)
target_link_libraries(my_app xb-xray)

# Debug only — zero overhead in Release
target_compile_definitions(my_app PRIVATE
    $<$<CONFIG:Debug>:XB_INSPECTOR_ENABLED>)
```

### 2. Initialize

```cpp
#include <xray/inspector.hpp>

int health = 100;
float pos[3] = {0.0f, 0.0f, 0.0f};
char level[32] = "menu";

xb::Xray::start("MyGame");
xb::Xray::bind("health", &health);
xb::Xray::bind_array("pos", pos, 3);
xb::Xray::bind_string("level", level, sizeof(level));
```

### 3. Frame hook (required)

```cpp
void update() {
    xb::Xray::update();  // ← executes REPL commands on main thread
    // ... game logic ...
}
```

### 4. Connect from terminal

```bash
# Python CLI (recommended)
pip install xb-connector
python -m xb_connector.cli 192.168.0.100

# Or raw netcat:
nc 192.168.0.100 9000

# Or GUI: XB Homebrew Vault
```

---

## Binding Types

### Scalar — `bind(name, &var)`

Expose any `int`, `float`, `double`, or `bool`.

```cpp
int score = 0;
float gravity = 9.81f;
double elapsed = 0.0;
bool paused = false;

xb::Xray::bind("score", &score);
xb::Xray::bind("gravity", &gravity);
xb::Xray::bind("elapsed", &elapsed);
xb::Xray::bind("paused", &paused);
```

```lua
> score       → 0
> score = 100 → C++ sees score = 100 immediately
> gravity     → 9.81
> if paused then print("game paused") end
> elapsed = math.floor(elapsed)
```

---

### Array — `bind_array(name, arr, len)`

Expose `float[]` or `int[]` as 1-based Lua arrays.

```cpp
float player_pos[3] = {0.0f, 0.0f, 0.0f};
int inventory[10] = {};

xb::Xray::bind_array("pos", player_pos, 3);
xb::Xray::bind_array("inv", inventory, 10);
```

```lua
> pos[1]      → 0.0 (1-based index)
> pos = {10, 20, 30}  → bulk assign
> pos[2] = 15 → modifies C++ array[1]
> #pos        → 3 (length)
> for i,v in ipairs(pos) do print(i,v) end
```

---

### String — `bind_string(name, buf, size)`

Expose a `char[]` buffer as a read/write Lua string.

```cpp
char level_name[64] = "menu";
char player_name[32] = "Hero";

xb::Xray::bind_string("level", level_name, sizeof(level_name));
xb::Xray::bind_string("player", player_name, sizeof(player_name));
```

```lua
> level       → "menu"
> level = "boss1"  → writes to C++ char[64], truncates if too long
> player = "Conan"
> level.."_"..player  → "boss1_Conan"
```

---

### Struct — `bind_struct(name, &s, fields, count)`

Group related variables under dot notation using automatic offset deduction.

```cpp
struct GameState {
    int hp, max_hp;
    float pos_x, pos_y;
    char name[32];
};
GameState gs;

static const xb::struct_field gs_fields[] = {
    xb::field("hp",     &GameState::hp),
    xb::field("max_hp", &GameState::max_hp),
    xb::field("pos_x",  &GameState::pos_x),
    xb::field("pos_y",  &GameState::pos_y),
    xb::field("name",   &GameState::name),
};
xb::Xray::bind_struct("gs", &gs, gs_fields,
    sizeof(gs_fields) / sizeof(gs_fields[0]));
```

```lua
> gs                    → {hp=100, max_hp=100, pos_x=0, pos_y=0, name=menu}
> gs.hp                 → 100
> gs.hp = 50            → C++ sees hp = 50 immediately
> gs.name = "boss1"     → writes to char[32] buffer
> for k,v in pairs(gs) do print(k, v) end
> if gs.hp < 20 then print("danger") end
```

---

## Lua REPL — Full Language, Live Access

```
> fps                         → 70.2 (live C++ value)
> fps = 75                    → modifies C++ variable!
> if fps > target_fps then
>>   print("over target")
>> end
> for i = 1, 10 do print(i) end
> gs.hp = gs.hp + 10          → math on C++ vars
> rom_name = "DOOM.DOSZ"      → change string buffer
```

Sandbox removes `io`, `os`, `dofile`, `loadfile`, `require`, `debug`. Everything else works — `math`, `string`, `table`, `pairs`, `ipairs`, control flow.

---

## Python CLI

```bash
# Install
pip install xb-connector

# Auto-scan ports 9000-9009:
python -m xb_connector.cli 192.168.0.100

# Commands:
#   :help    — show available commands
#   :list    — list all bound variables with types and values
#   :ls      — alias for :list
#   :quit    — disconnect
#   :terminate — shut down the Xbox app
#   <code>   — execute Lua on Xbox
```

---

## API Reference

| Function | Purpose | Thread safety |
|----------|---------|:---:|
| `start(app_name)` | Init spdlog, spawn network thread, bind port | Any |
| `stop()` | Join network thread, flush logs, destroy state | Any |
| `update()` | Execute queued REPL commands | **Main only** |
| `log(level, tag, msg)` | Log via spdlog (file + TCP + ODS) | Any |
| `bind(name, &var)` | Expose int/float/double/bool to Lua | Any* |
| `bind_array(name, arr, len)` | Expose int[]/float[] to Lua | Any* |
| `bind_string(name, buf, size)` | Expose char[] buffer to Lua | Any* |
| `bind_struct(name, &s, fields, n)` | Expose POD struct with dot notation | Any* |
| `set_on_terminate(fn)` | Callback on `terminate` command | Any |
| `set_log_path(path)` | Override log directory | Before `start()` |
| `is_connected()` | True if a client is connected | Any |
| `bound_port()` | Actual bound port (9000-9009) | Any |

\* Before first `update()` call.

---

## Component Architecture

```
┌──────────────────────────────────────────────────────────┐
│                     YOUR UWP APP                         │
│  ┌──────────────────────────────────────────────────┐    │
│  │  xb-xray (static lib)                       │    │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────────────┐  │    │
│  │  │ spdlog   │ │ Lua 5.4 │ │ xray-sock        │  │    │
│  │  │ file+net │ │ sandbox  │ │ TCP listener     │  │    │
│  │  │ +ODS     │ │ REPL     │ │ port 9000-9009   │  │    │
│  │  └──────────┘ └──────────┘ └──────────────────┘  │    │
│  └──────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────┘
         │                        ▲
         │ logs + repl_results    │ commands + repl_eval
         ▼                        │
┌──────────────────────────────────────────────────────────┐
│              DEV PC (Python CLI or Vault)                 │
│           TCP JSON, newline-delimited, port 9000          │
└──────────────────────────────────────────────────────────┘
```

---

## Build Prerequisites

- Visual Studio 2022 with **UWP** and **x64** C++ tools
- PowerShell 7+

```powershell
# One-command build (Lua 5.4 + xray-sock)
.\scripts\build-all.ps1
```

---

## Component Table

| Library | Type | Location | Role |
|---------|------|----------|------|
| `xb-xray` | CMake interface target | `x64/include/xray/` | Public API + aggregator |
| `xray-sock` | Prebuilt static lib | `x64/lib/xray-sock.lib` | TCP listener, safe queues |
| `lua5.4` | Prebuilt static lib | `x64/lib/lua5.4.lib` | Lua interpreter |
| `spdlog` | Header-only (submodule) | `external/spdlog/include/` | Logging framework |
| `nlohmann_json` | Header-only (submodule) | `external/json/single_include/` | JSON protocol |

---

## Documentation

| Doc | Topics |
|-----|--------|
| [QUICKSTART](docs/QUICKSTART.md) | 5-minute setup guide |
| [ARCHITECTURE](docs/ARCHITECTURE.md) | System design, data flow, ADRs |
| [CPP-API](docs/CPP-API.md) | Full C++ API reference, thread model |
| [LUA-REPL](docs/LUA-REPL.md) | Lua binding, sandbox, execution model |
| [NETWORK-PROTOCOL](docs/NETWORK-PROTOCOL.md) | Wire format, JSON messages, handshake |
| [LOGGING](docs/LOGGING.md) | spdlog sinks, backpressure, rotation |
| [SECURITY](docs/SECURITY.md) | Threat model, `#ifdef` guard |
| [DEPOT-STRUCTURE](docs/DEPOT-STRUCTURE.md) | Repository layout, CMake consumption |
| [LANGUAGE-BINDINGS](docs/LANGUAGE-BINDINGS.md) | C++ vs C# vs Rust |
| [ROADMAP](docs/ROADMAP.md) | Implementation phases 0–4 |

---

## Security

- **All inspector code guarded by `#ifdef XB_INSPECTOR_ENABLED`**
- **Must never appear in Release/shipping builds**
- Verify: `dumpbin /symbols my_app.exe | Select-String "XB_INSPECTOR_ENABLED"`
- No TLS/auth — trusted LAN + Dev Mode only
- Lua sandbox: `io`, `os`, `dofile`, `loadfile`, `require`, `debug` removed

---

## Known Limitations

- Dynamic unbinding — variables live for xb-xray lifetime
- `repl_result.id` — always 0 (Vault ID not echoed through SPSC)
- Custom usertypes — raw Lua C API only (no Sol2)
- No TLS — Dev Mode is a trusted environment

---

## License

MIT. See `lic/` for third-party licenses (spdlog, Lua 5.4, nlohmann_json, {fmt}).
