# 04 — Lua REPL

## 1. Overview

The remote REPL allows the developer to execute Lua code on the Xbox in real time, inspect/modify native C++ variables, and call app-exposed functions — all from the Vault terminal on the PC.

```
┌──── Vault ────┐          ┌────────── Xbox ──────────┐
│               │          │                           │
│  print(x)     │──TCP────►│  Network Thread            │
│               │          │  ┌──────────────────┐     │
│               │          │  │ json parse → SPSC │     │
│               │          │  └────────┬─────────┘     │
│               │          │           │                │
│               │          │  Main Thread (next frame)  │
│               │          │  ┌──────────────────┐     │
│               │          │  │ lua.script(cmd)  │     │
│               │          │  │ lua_State exec   │     │
│               │          │  └────────┬─────────┘     │
│               │          │           │                │
│  ← "42"      │◄───TCP───│  MPSC ◄───┘                │
└───────────────┘          └───────────────────────────┘
```

## 2. Lua 5.4

### Choice

| Criteria | Lua 5.4 | LuaJIT | Lua 5.1 |
|---|---|---|---|
| .lib footprint | ~180KB | ~1MB | ~170KB |
| GC | Generational | Incremental | Mark-and-sweep |
| GC pause | Low | Medium | High |
| `to-be-closed` vars | Yes | No | No |
| `const` / `close` attrs | Yes | No | No |
| UWP patch needed | None | Required | None |

Lua 5.4 is the right choice for a debug REPL: lower GC pause (important to avoid frame drops), modern language features, no patching needed.

### Build

Lua 5.4.7 is built as **static library** (.lib) for UWP x64.

```
x64/lib/lua5.4.lib
x64/include/lua/
├── lua.h
├── luaconf.h
├── lualib.h
├── lauxlib.h
├── lua.hpp
└── luaxlib.h
```

## 3. Sol2 — C++ ↔ Lua Binding

We use **Sol2** (header-only, C++17) to expose C++ structs and variables to Lua.

```cpp
// Register Player type for Lua to understand its structure
xb::Inspector::bind_type<Player>("Player",
    "health", &Player::health,
    "name", &Player::name
);

// Expose a specific instance
Player player1;
xb::Inspector::bind("player", &player1);
```

In the Vault terminal:

```lua
> print(player.name)
Marcos
> player.health = 50  -- modifies C++ in real time
```

### What can be bound

| C++ Type | Lua Access |
|---|---|
| `int`, `float`, `double` | Read/write |
| `std::string` | Read/write |
| `bool` | Read/write |
| Registered `struct` with `bind_type` | Read/write exposed fields |
| Pointers to struct | Full access via registered fields |
| Functions/Callbacks | Callable from Lua |
| Enum classes | Read/write (with cast) |

### Naming convention

- Bindings should use `snake_case` in Lua (e.g. `player.health`, `engine.get_fps`)
- The library auto-converts or the user declares explicitly

## 4. SPI — Service Provider Interface for Binding

The homebrew developer does not need to know Sol2 or the Lua API. The `xb::Inspector` SPI hides all complexity:

```cpp
// Register type and variables in one call
xb::Inspector::bind_type<EngineConfig>("Config",
    "vsync",    &EngineConfig::vsync_enabled,
    "fps_limit",&EngineConfig::fps_limit
);

EngineConfig cfg;
xb::Inspector::bind("engine_config", &cfg);

// Register custom getters/setters
xb::Inspector::bind_function("engine_reset", []() {
    Engine::reset();
});

// Function that returns a value for inspection
xb::Inspector::bind_function("get_fps", []() {
    return Engine::get_fps();
});
```

## 5. Safe Execution on Main Thread

### Why not execute Lua on the network thread?

The network thread must not touch game state (variables, objects, scenes) without causing data races. Lua is executed exclusively on the **main thread** (game loop).

### Flow

```
1. Vault sends:    {"event": "repl_eval", "payload": {"id":5, "script":"..."}}
2. Network thread: receives, parses JSON, pushes to SPSC queue
3. Main thread (frame start):
   a. If SPSC queue not empty:
      - Pop command
      - lua_State* L = get_repl_state()
      - luaL_dostring(L, command.script)
      - Format result (print/output string or error)
      - Push to MPSC queue (→ network thread sends)
   b. If SPSC queue empty:
      - Nothing happens, zero cycles wasted
4. Network thread: pop from MPSC, send() to Vault
```

### Protections

- SPSC queue has a max capacity of 64 commands (drop-newest)
- `luaL_dostring` is wrapped in `lua_pcall` to catch errors without crashing the app
- Timeout: if a Lua script takes >100ms, it is aborted (prevents infinite loops)
- Always calls `lua_settop` to clear the stack after each command

## 6. Sandbox

The REPL runs in a controlled Lua environment:

| Access | Allowed | Denied |
|---|---|---|
| Bound app variables | Yes | — |
| `print()`, `type()`, `pairs()`, `ipairs()` | Yes | — |
| `math.*`, `string.*`, `table.*` | Yes | — |
| `io.*` (file I/O) | — | No (sandbox) |
| `os.execute()`, `os.popen()` | — | No (sandbox) |
| `dofile()`, `loadfile()` | — | No (sandbox) |
| `require()` | — | No (sandbox) |
| Full `_G` access | — | No (restricted table) |

```cpp
// Create Lua state with safe libs only
auto L = luaL_newstate();
luaL_openlibs(L);

// Remove dangerous libs
lua_pushnil(L);
lua_setglobal(L, "io");
lua_pushnil(L);
lua_setglobal(L, "os");
lua_pushnil(L);
lua_setglobal(L, "dofile");
lua_pushnil(L);
lua_setglobal(L, "loadfile");
lua_pushnil(L);
lua_setglobal(L, "require");
```

## 7. Pretty Print Helper

A `dump()` function is injected into the Lua state for table inspection:

```lua
> dump(player)
{
  health = 100,
  name = "Marcos",
  position = { x = 12.5, y = 0.0, z = -45.1 }
}
```

Implemented in C++ as a C function that iterates the table with `pairs()` and serializes to an indented string.
