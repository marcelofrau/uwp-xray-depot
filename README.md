# uwp-xray-depot

[![build](https://github.com/marcelofrau/uwp-xray-depot/actions/workflows/build.yml/badge.svg)](https://github.com/marcelofrau/uwp-xray-depot/actions/workflows/build.yml)

**Native remote diagnostics for UWP homebrews on Xbox Dev Mode.**

CMake import depot — `add_subdirectory()` + `target_link_libraries(... xb-inspector)` gives you:

- **Real-time log stream** over TCP (spdlog → file + OutputDebugString + network)
- **Lua 5.4 REPL** — inspect and modify live C++ variables from a remote terminal
- **Port range** 9000–9009, non-blocking, zero-dependency on Microsoft debug tooling

## Quick Start

```cmake
# CMakeLists.txt
add_subdirectory(path/to/uwp-xray-depot)
target_link_libraries(my_game PRIVATE xb-inspector)
target_compile_definitions(my_game PRIVATE XB_INSPECTOR_ENABLED)
```

```cpp
#define XB_INSPECTOR_ENABLED
#include <xray/inspector.hpp>

int health = 100;
float pos[3] = {0,0,0};

int main() {
    xb::Inspector::start("MyGame");

    xb::Inspector::bind("health", &health);
    xb::Inspector::bind_array("pos", pos, 3);

    while (running) {
        xb::Inspector::update();   // consume REPL commands
        // ... game logic ...
    }

    xb::Inspector::stop();
    return 0;
}
```

Connect via netcat: `nc <xbox-ip> 9000` — receive handshake + live logs.
Or use [XB Homebrew Vault](https://github.com/marcelofrau/xb-homebrew-vault) for the full GUI.

## Component Table

| Library | Type | Path | Role |
|---|---|---|---|
| `xb-inspector` | Interface (aggregator) | `x64/include/xray/inspector.hpp` | Public API: start/stop/log/bind/update |
| `xray-sock` | Prebuilt static lib | `x64/lib/xray-sock.lib` | TCP listener, safe queues |
| `lua5.4` | Prebuilt static lib | `x64/lib/lua5.4.lib` | Lua interpreter |
| `spdlog` | Header-only (submodule) | `external/spdlog/include/` | Logging |
| `nlohmann_json` | Header-only (submodule) | `external/json/single_include/` | JSON protocol |

## CMake Targets

```
xb-inspector ──►links──► spdlog, nlohmann_json, lua5.4, xray-sock
```

Consumer links only `xb-inspector`. The rest resolve transitively.

## Build Prerequisites

- Visual Studio 2022 with **UWP** and **x64** C++ tools
- PowerShell 7+

```powershell
# One-command build (Lua 5.4 + xray-sock)
.\scripts\build-all.ps1

# Or step by step
.\scripts\build-lua.ps1
.\scripts\build-xray-sock.ps1
```

## API Overview

| Function | Purpose | Thread |
|---|---|---|
| `start(app_name)` | Init spdlog, spawn network thread, bind port 9000 | Any |
| `stop()` | Join network thread, flush logs, destroy Lua state | Any |
| `update()` | Consume and execute REPL commands (call at frame start) | Main |
| `log(level, tag, msg)` | Log via spdlog (file + TCP) | Any |
| `bind("name", &var)` | Expose int/float/double/bool to Lua | Any (before update) |
| `bind_array("name", arr, len)` | Expose int[]/float[] to Lua | Any (before update) |
| `bind_string("name", buf, size)` | Expose char[] buffer to Lua | Any (before update) |
| `bind_struct("name", &s, fields, n)` | Expose POD struct with dot notation | Any (before update) |
| `set_on_terminate(fn)` | Callback on `terminate` command | Any (before start) |
| `is_connected()` | True if Vault is connected | Any |
| `bound_port()` | Actual port (9000–9009) | Any |

## Project Structure

```
├── CMakeLists.txt              # Depot import targets
├── external/                   # Git submodules
│   ├── lua/                    #   Lua 5.4.7
│   ├── spdlog/                 #   v1.14.1
│   └── json/                   #   v3.11.3
├── src/
│   ├── xray-sock/              # TCP listener + safe_queue
│   └── xb-inspector/           # Inspector + sinks + lua_state
├── x64/
│   ├── lib/                    # Prebuilt .lib files
│   └── include/xray/           # Public headers
│       ├── inspector.hpp       #   Main API
│       ├── struct_field.hpp    #   Field descriptors for bind_struct
│       └── ...
├── samples/cpp-sample/          # Minimal consumer project
├── scripts/                    # Build scripts
├── lic/                        # Third-party licenses
└── docs/                       # Full documentation
```

## Documentation

| Doc | Topics |
|---|---|
| [QUICKSTART](docs/QUICKSTART.md) | 5-minute setup guide |
| [ARCHITECTURE](docs/ARCHITECTURE.md) | Overview, components, data flow, ADRs |
| [NETWORK-PROTOCOL](docs/NETWORK-PROTOCOL.md) | JSON messages, handshake, port scan |
| [CPP-API](docs/CPP-API.md) | Full C++ API, thread model, lifecycle |
| [LOGGING](docs/LOGGING.md) | spdlog config, sinks, backpressure, rotation |
| [LUA-REPL](docs/LUA-REPL.md) | Lua binding, sandbox, exec model, examples |
| [DEPOT-STRUCTURE](docs/DEPOT-STRUCTURE.md) | Depot structure, CMake, consumption guide |
| [SECURITY](docs/SECURITY.md) | Security, XB_INSPECTOR_ENABLED guard |
| [ROADMAP](docs/ROADMAP.md) | Implementation phases 0–4 |
| [LANGUAGE-BINDINGS](docs/LANGUAGE-BINDINGS.md) | C++ vs C# vs Rust comparison |

## Security

- All inspector code is guarded by `#ifdef XB_INSPECTOR_ENABLED`
- **Must never be in release/shipping builds**
- Verify: `dumpbin /symbols my_homebrew.exe | Select-String "XB_INSPECTOR_ENABLED"`
- No TLS/auth — trusted LAN + Dev Mode only
- Lua sandbox removes `io`, `os`, `dofile`, `loadfile`, `require`, `debug`

## Known Limitations

- Dynamic unbinding — variables live for the Inspector lifetime
- `repl_result.id` — always 0 (Vault ID not echoed)
- Custom usertypes — no `new_usertype<T>` yet (raw Lua C API only)
- Sol2 — planned post-MVP

## License

MIT. See `lic/` for third-party licenses.
