## uwp-xray-depot

Dependency depot for **XB-Inspector**: a native remote diagnostics library for UWP homebrews on Xbox Dev Mode.

Real-time logs + remote Lua REPL + TCP Inspector — no dependency on Visual Studio Debugger or Xbox Device Portal.

### Components

| Component | Description |
|---|---|
| `xb-inspector` | Public C++ API: `Inspector::start/stop/log/bind` |
| `xray-sock` | Non-blocking TCP listener with port fallback 9000-9009 |
| `spdlog` | Header-only logging with UWP file + TCP forward sinks |
| `nlohmann/json` | Header-only JSON serialization |
| `Lua 5.4` | Embedded interpreter for remote REPL |
| `Sol2` | Header-only C++ ↔ Lua binding |

### Usage

```cmake
add_subdirectory(path/to/uwp-xray-depot)
target_link_libraries(my_app PRIVATE xb-inspector)
```

```cpp
#include <xray/inspector.hpp>

int main() {
    xb::Inspector::start();
    xb::Inspector::log_info("Hello from Xbox!");
    xb::Inspector::bind("my_var", &my_var);
    // ...
}
```

### Docs

| Document | Covers |
|---|---|
| `docs/00-architecture.md` | Overview, diagrams, architectural decisions |
| `docs/01-network-protocol.md` | JSON protocol, handshake, port scan |
| `docs/02-xbox-native-lib.md` | C++ API, threading, queues, lifecycle |
| `docs/03-logging.md` | spdlog + UWP sinks + backpressure |
| `docs/04-lua-repl.md` | Lua 5.4 + Sol2 bind + sandbox |
| `docs/05-xray-depot.md` | Depot structure, CMake, submodules |
| `docs/06-threat-model.md` | Security, `#ifdef XB_INSPECTOR_ENABLED` |
| `docs/07-roadmap.md` | Implementation phases 0-4 |
| `docs/08-flavors.md` | C++ vs C# vs Rust |

### Licenses

`lic/` — SPDLOG (MIT), JSON (MIT), LUA (MIT).
