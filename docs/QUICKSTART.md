# xb-xray — Quick Start

Remote diagnostics for UWP homebrews on Xbox Dev Mode.
Real-time logs, live Lua REPL, C++ variable inspection — all over TCP.

## Integration

### 1. Add to CMake

```cmake
add_subdirectory(path/to/uwp-xray-depot)
target_link_libraries(my_app xb-xray)
```

Define `XB_INSPECTOR_ENABLED` in Debug builds only:

```cmake
target_compile_definitions(my_app PRIVATE
    $<$<CONFIG:Debug>:XB_INSPECTOR_ENABLED>)
```

### 2. Initialize

```cpp
#include <xray/inspector.hpp>

// At startup (main thread):
xb::Xray::start("MyGame");

// Bind live C++ variables:
int player_hp = 100;
float walk_speed = 3.5f;
bool god_mode = false;
xb::Xray::bind("hp", &player_hp);
xb::Xray::bind("speed", &walk_speed);
xb::Xray::bind("god", &god_mode);

// Optional: terminate callback
xb::Xray::set_on_terminate([]() {
    CoreApplication::Exit();
});
```

### 3. Frame hook (required!)

```cpp
void update() {
    xb::Xray::update();  // ← executes REPL commands
    // ... game logic ...
}
```

### 4. Build & deploy

```powershell
msbuild /p:Configuration=Debug /p:Platform=x64 my_app.sln
# Deploy to Xbox Dev Mode as usual
```

## Binding API

| Type | API | Lua usage |
|------|-----|-----------|
| `int`, `float`, `double`, `bool` | `bind("name", &var)` | `hp = 50` |
| `float[]`, `int[]` | `bind_array("arr", ptr, len)` | `pos[1]`, `#arr` |
| `char buf[N]` | `bind_string("name", buf, N)` | `rom_name`, `rom_name = "new"` |
| POD struct | `bind_struct("name", &s, fields, N)` | `perf.fps`, `pairs(perf)` |

### Struct binding example

```cpp
struct GameState {
    int hp, max_hp;
    float pos_x, pos_y, pos_z;
    bool paused;
    char level_name[64];
};
GameState gs;

// Define fields (typed, offset auto-computed):
static constexpr xb::struct_field gs_fields[] = {
    xb::field("hp",      &GameState::hp),
    xb::field("max_hp",  &GameState::max_hp),
    xb::field("pos_x",   &GameState::pos_x),
    xb::field("pos_y",   &GameState::pos_y),
    xb::field("pos_z",   &GameState::pos_z),
    xb::field("paused",  &GameState::paused),
    xb::field("level",   &GameState::level_name),
};
xb::Xray::bind_struct("gs", &gs, gs_fields,
    sizeof(gs_fields) / sizeof(gs_fields[0]));
```

In Lua:

```lua
> gs              → {hp=100, max_hp=100, pos_x=0, ...}
> gs.hp           → 100
> gs.hp = 50      → C++ sees hp = 50
> gs.paused = true
> for k,v in pairs(gs) do print(k,v) end  → iterate fields
> if gs.hp < 20 then print("danger") end  → Lua control flow
```

## Python CLI

### Install

```bash
git clone https://github.com/marcelofrau/xb-xray-py-connector
cd xb-xray-py-connector
pip install .
```

### Connect

```bash
# Auto-scan ports 9000-9009:
python -m xb_connector.cli 192.168.0.100

# Or specific port:
python -m xb_connector.cli 192.168.0.100 -p 9002

# Verbose mode (raw JSON):
python -m xb_connector.cli 192.168.0.100 -v
```

### Commands

| Input | Action |
|-------|--------|
| `:help` or `:h` | Show help |
| `:list` or `:ls` | List all bound variables + types + values |
| `:quit` or `:q` | Disconnect |
| `:terminate` | Shut down the Xbox app |
| `<lua code>` | Execute Lua on Xbox in real time |

### Lua REPL examples

```lua
> fps                          → 70.2 (live C++ value)
> fps = 75                     → modifies C++ variable!
> target_fps                   → 70.0
> hp                           → 100
> if hp < 20 then print("low") end
> for i = 1, 10 do print(i) end
> print(math.sin(1.5))
> perf.total_ms                → 8.34 (struct field)
> rom_name = "MYGAME.DOSZ"     → changes char buffer
```

## Logs

| Platform | Location |
|----------|----------|
| PC (Desktop UWP) | `%TEMP%\dosbox-pure\logs\xray-YYYY-MM-DD-HH-MM-SS.log` |
| Xbox Dev Mode | `E:\dosbox\logs\xray-YYYY-MM-DD-HH-MM-SS.log` |

Logs auto-clean after 7 days.

## Protocol

JSON over TCP, newline-delimited. Port range 9000-9009.
Handshake on connect, then log stream + REPL exchange.

## Quick reference

| Item | Detail |
|------|--------|
| Library | `xb-xray` (static lib) |
| Guard define | `XB_INSPECTOR_ENABLED` (Debug only) |
| TCP ports | 9000–9009 (auto scan) |
| Lua version | 5.4 |
| Lua sandbox | `io`, `os`, `dofile`, `loadfile`, `require`, `debug` removed |
| Thread model | Network I/O on worker thread; Lua exec on main thread via `update()` |
| Log format | `[HH:MM:SS.mmm] [LEVEL] [tag] message` |

## See also

- [ARCHITECTURE.md](ARCHITECTURE.md) — system design
- [CPP-API.md](CPP-API.md) — full C++ API reference
- [LUA-REPL.md](LUA-REPL.md) — Lua REPL developer manual
- [NETWORK-PROTOCOL.md](NETWORK-PROTOCOL.md) — wire format
