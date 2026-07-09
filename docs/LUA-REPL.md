# 04 — Lua REPL Developer Manual

## 1. Overview

The Lua REPL (Read-Eval-Print Loop) allows a developer connected via the Vault to:

- Execute arbitrary Lua code on the Xbox in real time
- Read and modify live C++ variables (`int`, `float`, `bool`, arrays)
- Inspect game state without recompiling or pausing

**Architecture:** raw Lua 5.4 C API (no Sol2 dependency). Bindings are lightweight userdata wrappers around pointers — no marshaling, no copies.

## 2. Developer API

### 2.1 Frame Hook — `Xray::update()`

**Must be called once per frame, at frame start, on the main thread.**

```cpp
#include <xray/inspector.hpp>

void Game::update()
{
    Xray::update();  // ← consume REPL commands, execute Lua

    // ... rest of game logic
    player.update();
    render();
}
```

If `update()` is never called, REPL commands queue up in the SPSC (64 slots, drop-newest). The inspector still works — logs and handshake are unaffected — but `repl_eval` commands silently expire.

### 2.2 Binding Scalars

Expose a live C++ variable to Lua. The variable is **not copied** — Lua reads/writes the memory directly.

```cpp
int player_hp = 100;
float walk_speed = 3.5f;
bool god_mode = false;

Xray::start("MyGame");
Xray::bind("player_hp", &player_hp);   // int
Xray::bind("walk_speed", &walk_speed); // float
Xray::bind("god_mode", &god_mode);     // bool
```

From the Vault:

```lua
> player_hp
100
> player_hp = 50
> player_hp
50
-- C++ sees: player_hp == 50
```

**Supported types:** `int`, `float`, `bool`. Other types (`double`, `short`, etc.) produce no-op (silently ignored).

### 2.3 Binding Arrays

Expose a C array. Accessible by 1-based index in Lua.

```cpp
float player_pos[3] = {0.0f, 0.0f, 0.0f};
int inventory[10] = {0};

Xray::bind_array("player_pos", player_pos, 3);
Xray::bind_array("inventory", inventory, 10);
```

From the Vault:

```lua
> player_pos
{0, 0, 0}
> player_pos[1] = 10.5
> player_pos[2] = 20.0
> player_pos
{10.5, 20, 0}

> #player_pos  -- __len metamethod
3

> for i, v in pairs(player_pos) do print(i, v) end
1       10.5
2       20
3       0
```

**Supported types:** `float[]`, `int[]`.

### 2.4 Struct Binding (Phase 3+, Post-MVP)

Not yet implemented in raw C API. Workaround for Phase 3:

```cpp
// Bind individual fields with prefix namespace
Xray::bind("player_hp", &player.hp);
Xray::bind("player_x",  &player.x);
Xray::bind("player_y",  &player.y);
```

Vault sees flat globals: `player_hp`, `player_x`, `player_y`.

Full struct binding (dot notation: `player.hp`) planned post-MVP via metatable with field offset table.

## 3. Lua Sandbox

### 3.1 Available Libraries

| Library | Status |
|---|---|
| `_G` (base) | ✓ `print`, `type`, `pairs`, `ipairs`, `next`, `select`, `tonumber`, `tostring`, `pcall`, `xpcall`, `error`, `assert`, `ipairs` |
| `math` | ✓ Full |
| `string` | ✓ Full |
| `table` | ✓ Full |
| `coroutine` | ✓ Full |
| `utf8` | ✓ Full |

### 3.2 Removed (nil'd out)

| Function | Reason |
|---|---|
| `dofile`, `loadfile`, `require`, `load` | File system access |
| `io.*` | File system access |
| `os.*` | OS commands, env, time manipulation |
| `package.*` | Module system |
| `debug.*` | Stack inspection, debug hooks |

### 3.3 Global Table Protection

`_G` has `__metatable = "locked"` — Vault cannot `getmetatable(_G)` or modify it via metatable tricks.

## 4. Execution Model

### 4.1 Thread Flow

```
Vault                          Network Thread                  Main Thread
  │                                  │                              │
  │──── repl_eval {"script":"..."} ──►                              │
  │                                  │── push to SPSC ─────────────►│
  │                                  │                              │── update()
  │                                  │                              │── lua_exec()
  │                                  │                              │── format result
  │                                  │◄── push to MPSC ────────────│
  │◄── repl_result {"output":"..."} ──│                              │
```

### 4.2 SPSC Queue (Commands)

| Property | Value |
|---|---|
| Type | Lock-free single-producer single-consumer |
| Capacity | 64 commands |
| Overflow | Drop-newest (busy main thread drops commands) |

### 4.3 MPSC Queue (Results)

Results use the **same** MPSC queue as log messages. The network thread interleaves logs and `repl_result` messages in order.

### 4.4 Timeout

Every `exec()` call sets `lua_sethook` with `LUA_MASKCOUNT` at 100,000 instructions. If exceeded:

```lua
> while true do end
error: timeout: exceeded 100000 instructions
```

The Lua state is **not** corrupted — `luaL_error` does a longjmp back to `pcall`, stack is cleaned up, and the REPL remains usable.

## 5. Protocol

### 5.1 REPL Command (Vault → Xbox)

```json
{
  "event": "repl_eval",
  "payload": {
    "script": "return 1 + 1"
  }
}
```

### 5.2 REPL Response (Xbox → Vault)

On success:

```json
{
  "event": "repl_result",
  "payload": {
    "id": 0,
    "success": true,
    "output": "2",
    "error": ""
  }
}
```

On error:

```json
{
  "event": "repl_result",
  "payload": {
    "id": 0,
    "success": false,
    "output": "",
    "error": "[string \"...\"]:1: attempt to perform arithmetic on global 'unknown' (a nil value)"
  }
}
```

### 5.3 Handshake Update

The `capabilities` array now includes `"repl"` when Lua is available:

```json
{
  "event": "handshake",
  "protocol_version": 1,
  "payload": {
    "app_name": "MyGame",
    "capabilities": ["log", "repl"]
  }
}
```

## 6. Complete Example

### C++ Side

```cpp
#include <xray/inspector.hpp>

struct Player {
    int hp = 100;
    float x = 0, y = 0;
};

int main()
{
    Player player;
    int score = 0;

    Xray::start("MyGame");

    Xray::bind("score", &score);
    Xray::bind("player_hp", &player.hp);
    Xray::bind_array("player_pos", &player.x, 2);

    while (running) {
        Xray::update();

        if (player.hp <= 0) {
            // die
        }

        // ... game loop
    }

    Xray::stop();
    return 0;
}
```

### Vault Session

```
Connected to MyGame (port 9000)
> score
0
> score = 9999
> player_hp = 50
> player_pos[1] = 100.0
> player_pos[2] = 200.0
> for i, v in pairs(player_pos) do print(i, v) end
1       100
2       200

> print("hello from xbox")
hello from xbox

> return 2 + 2
4

> type(math.pi)
number

> math.sin(math.pi / 2)
1
```

## 7. Limitations (Phase 3)

- **Struct bind:** flat namespace only (`player_hp` not `player.hp`)
- **String bind:** not yet supported
- **Custom usertypes:** no `new_usertype<T>` yet
- **ID correlation:** `repl_result.id` is always 0 (Vault → Xbox `id` is ignored)
- **Error classification:** heuristic (checks string prefixes) — Lua errors in user output may be misclassified
- **No Sol2:** all binding is raw C API; plan to add Sol2 as optional dependency post-MVP

## 8. Future (Post-MVP)

| Feature | Approach |
|---|---|
| Struct bind (dot notation) | Metatable with field offset table per type |
| String bind | Fixed-size char buffer via userdata |
| `bind_type<T>(name, fields...)` | Type registration with field descriptors |
| Sol2 integration | Optional `#define XB_INSPECTOR_USE_SOL2` |
| REPL history / id correlation | Parse Vault's `id` field, echo it back |
| Better error detection | Check `lua_type(L, -1)` after pcall for precise success/fail |
