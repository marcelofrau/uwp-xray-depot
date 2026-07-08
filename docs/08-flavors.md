# 08 — Flavors: C++ vs C# vs Rust

## 1. Strategy

**C++** is the primary and most mature flavor. The C# and Rust implementations are ports of the protocol and API, maintaining compatibility with the same network model and JSON messages.

```
                    ┌────── JSON Protocol ──────┐
                    │  (handshake, log, repl_*)  │
                    └──────────┬─────────────────┘
                               │
          ┌────────────────────┼────────────────────┐
          ▼                    ▼                    ▼
   ┌──────────┐        ┌──────────┐         ┌──────────┐
   │  C++     │        │  C#      │         │  Rust    │
   │  Sol2    │        │MoonSharp │         │  mlua    │
   │  Winsock │        │TcpListener│         │  tokio   │
   │  spdlog  │        │ Serilog? │         │  log     │
   └──────────┘        └──────────┘         └──────────┘
```

## 2. Comparison

### Struct Binding

| Operation | Sol2 (C++) | MoonSharp (C#) | mlua (Rust) |
|---|---|---|---|
| Field binding | 1 line: `"health", &Player::health` | Automatic via Reflection | Manual: implement `LuaUserData` trait |
| Function binding | `"reset", &Player::reset` | Automatic (public methods) | Manual: `add_function_method` |
| Runtime overhead | None (direct pointer) | Medium (Reflection per call) | Low (type-erased) |
| Binding errors | Compile-time (template) | Runtime (Reflection fails silently) | Compile-time (trait not implemented) |

### Network Management

| Operation | C++ (Winsock2) | C# (TcpListener) | Rust (tokio) |
|---|---|---|---|
| Non-blocking | `select()`/`WSAPoll()` | `async Task` + `AcceptAsync` | `tokio::net::TcpListener` |
| Port fallback | Manual loop | Manual loop | Manual loop |
| JSON parsing | nlohmann/json | System.Text.Json | serde_json |
| Threading | Manual (CreateThread) | Task.Run / Background | async/await + tokio |

### Logging

| Operation | C++ | C# | Rust |
|---|---|---|---|
| Library | spdlog | Microsoft.Extensions.Logging / Serilog | crate `log` + `env_logger` |
| Custom sink | Class inherits `base_sink` | `ILoggerProvider` | `Log::Log` trait |
| TCP forward | `uwp_net_sink` | Custom `ILogger` | Custom `log::Log` |

### Lua VM

| Operation | C++ | C# | Rust |
|---|---|---|---|
| Library | Sol2 + Lua 5.4 C API | MoonSharp (pure C#) / NLua (P/Invoke) | mlua (Lua 5.4 via bindings) |
| Sandbox | Remove globals manually | MoonSharp: `Corset` auto-magic | mlua: `scope` + safe environment |
| Preemptive yield | Non-trivial | Non-trivial | `mlua::Scope` + async |

## 3. Maintenance Effort

| Aspect | C++ (Sol2) | C# (MoonSharp) | Rust (mlua) |
|---|---|---|---|
| Library maturity | High (Sol2, spdlog mature) | High (MoonSharp mature, no recent updates) | Medium-high (mlua active, occasional API breakage) |
| Binding debugging | Compile-time (safest) | Runtime (fastest to write) | Compile-time (most verbose) |
| UWP build | MSBuild/CMake, no surprises | Native UWP .NET | Cross-compile tricky, needs `x86_64-uwp-windows-msvc` target |
| UWP community | Small but established | Large (C# is UWP's lingua franca) | Very small |
| Performance | Maximum | Good (Reflection only on bind, not hot path) | Near C++ |

## 4. Recommendation

| Scenario | Flavor |
|---|---|
| New C++ homebrew | C++ (first-class, richest documentation) |
| Existing C# homebrew | C# (MoonSharp, natural UWP integration) |
| Rust homebrew | Rust (mlua, but wait for C++ to stabilize first) |
| Quick REPL with minimal effort | C# (MoonSharp reflection is nearly magic) |

## 5. Expected API Parity

Not all flavors will have 100% API parity. The most significant difference is in the binding system:

```cpp
// C++ (exact, declarative)
xb::Inspector::bind_type<Player>("Player",
    "health", &Player::health,
    "name", &Player::name
);
xb::Inspector::bind("player", &g_player);
```

```csharp
// C# (implicit, reflection)
XbInspector.Bind("player", g_player);
// MoonSharp discovers properties via reflection automatically
```

```rust
// Rust (explicit, trait)
struct Player { health: i32, name: String }
impl LuaUserData for Player {
    fn add_fields<'lua, F: LuaUserDataFields<'lua, Self>>(fields: &mut F) {
        fields.add_field_method_get("health", |_, this| Ok(this.health));
        fields.add_field_method_set("health", |_, this, val| { this.health = val; Ok(()) });
        fields.add_field_method_get("name", |_, this| Ok(this.name.clone()));
        fields.add_field_method_set("name", |_, this, val| { this.name = val; Ok(()) });
    }
}
xb_inspector::bind("player", &g_player);
```

The high-level API (`start`, `stop`, `log`, `is_connected`) will be identical across all three flavors. The differences are in the binding system due to each language's capabilities.
