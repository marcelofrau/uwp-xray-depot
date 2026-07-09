# 05 — uwp-xray-depot Structure

## 1. Layout

```
uwp-xray-depot/
│
├── CMakeLists.txt              # Import targets for consumers
├── README.md
│
├── .github/workflows/build.yml # CI pipeline
│
├── lic/                        # Dependency licenses
│   ├── SPDLOG-LICENSE.txt
│   ├── JSON-LICENSE.txt        # MIT (nlohmann/json)
│   └── LUA-LICENSE.txt
│
├── external/                   # Git submodules (source)
│   ├── lua/                    # https://github.com/lua/lua  v5.4.7
│   ├── spdlog/                 # https://github.com/gabime/spdlog  v1.14.1
│   └── json/                   # https://github.com/nlohmann/json  v3.11.3
│
├── src/                        # Library source
│   ├── CMakeLists.txt          # xray-sock.lib build
│   ├── xray-sock/
│   │   ├── tcp_listener.h
│   │   ├── tcp_listener.cpp
│   │   └── safe_queue.h        # MPSC + SPSC lock-free queues
│   └── xb-inspector/
│       ├── inspector.cpp       # Inspector implementation
│       ├── lua_state.hpp       # Lua 5.4 wrapper + sandbox + binding
│       ├── uwp_sink.h          # spdlog file + OutputDebugString
│       └── uwp_net_sink.h      # spdlog TCP sink (JSON → MPSC)
│
├── scripts/                    # Build scripts
│   ├── build-all.ps1           # Run everything
│   ├── build-lua.ps1           # Build lua5.4.lib (UWP x64)
│   └── build-xray-sock.ps1     # Build xray-sock.lib
│
├── samples/
│   └── cpp-sample/             # Minimal consumer project
│       ├── CMakeLists.txt
│       └── main.cpp
│
├── x64/                        # Prebuilts (output, committed)
│   ├── lib/
│   │   ├── lua5.4.lib          # ~1 MB
│   │   └── xray-sock.lib       # ~9.5 MB (includes spdlog+json expanded)
│   └── include/
│       ├── lua/
│       │   ├── lua.h, luaconf.h, lualib.h, lauxlib.h, lua.hpp
│       └── xray/
│           ├── inspector.hpp   # Public API
│           ├── xray-sock.hpp   # TCP listener API
│           └── safe_queue.h    # Public queue headers
│
└── docs/
    ├── ARCHITECTURE.md         # Overview, components, ADRs
    ├── NETWORK-PROTOCOL.md     # JSON messages, handshake, port scan
    ├── CPP-API.md              # Full C++ API, thread model
    ├── LOGGING.md              # Sinks, backpressure, rotation
    ├── LUA-REPL.md             # Lua binding, sandbox, exec model
    ├── DEPOT-STRUCTURE.md      # ← This document
    ├── SECURITY.md             # Security, #ifdef guard
    ├── ROADMAP.md              # Phases 0-4
    ├── LANGUAGE-BINDINGS.md    # C++ vs C# vs Rust
    └── QUICKSTART.md           # 5-min setup guide
```

## 2. Submodules

```bash
git submodule add https://github.com/lua/lua      external/lua
git submodule add https://github.com/gabime/spdlog external/spdlog
git submodule add https://github.com/nlohmann/json external/json
```

| Submodule | Tag | Version |
|---|---|---|
| `lua` | `v5.4.7` | 5.4.7 |
| `spdlog` | `v1.14.1` | 1.14.1 |
| `json` | `v3.11.3` | 3.11.3 |

## 3. Root CMakeLists.txt — Depot Import Targets

```cmake
cmake_minimum_required(VERSION 3.15)
project(uwpXrayDepot NONE)

set(XRAY_BASE "${CMAKE_CURRENT_LIST_DIR}/x64")

# ── spdlog (header-only) ──
add_library(spdlog INTERFACE IMPORTED GLOBAL)
set_target_properties(spdlog PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES
        "${CMAKE_CURRENT_LIST_DIR}/external/spdlog/include"
)

# ── nlohmann_json (header-only) ──
add_library(nlohmann_json INTERFACE IMPORTED GLOBAL)
set_target_properties(nlohmann_json PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES
        "${CMAKE_CURRENT_LIST_DIR}/external/json/single_include"
)

# ── Lua 5.4 (prebuilt static lib) ──
add_library(lua5.4 STATIC IMPORTED GLOBAL)
set_target_properties(lua5.4 PROPERTIES
    IMPORTED_LOCATION "${XRAY_BASE}/lib/lua5.4.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${XRAY_BASE}/include"
)

# ── xray-sock (prebuilt static lib) ──
add_library(xray-sock STATIC IMPORTED GLOBAL)
set_target_properties(xray-sock PROPERTIES
    IMPORTED_LOCATION "${XRAY_BASE}/lib/xray-sock.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${XRAY_BASE}/include"
)

# ── xb-inspector (interface: headers + deps) ──
add_library(xb-inspector INTERFACE IMPORTED GLOBAL)
set_target_properties(xb-inspector PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${XRAY_BASE}/include"
    INTERFACE_LINK_LIBRARIES "spdlog;nlohmann_json;lua5.4;xray-sock"
)
```

## 4. Consumption by Projects

```cmake
# In the homebrew CMakeLists.txt:
add_subdirectory(path/to/uwp-xray-depot)

target_link_libraries(my_homebrew PRIVATE xb-inspector)
target_compile_definitions(my_homebrew PRIVATE XB_INSPECTOR_ENABLED)
```

```cpp
#define XB_INSPECTOR_ENABLED
#include <xray/inspector.hpp>

int main() {
    xb::Inspector::start("MyGame");
    xb::Inspector::bind("health", &health);
    xb::Inspector::bind_array("pos", pos, 3);

    while (running) {
        xb::Inspector::update();
        // game logic ...
    }

    xb::Inspector::stop();
}
```

## 5. Building Artifacts

### Lua 5.4 (.lib)

See `scripts/build-lua.ps1`. Compiles Lua 5.4.7 C sources with MSVC for UWP x64.

### xray-sock (.lib)

```powershell
# scripts/build-xray-sock.ps1
cmake -S src -B build/xray-sock -A x64
cmake --build build/xray-sock --config Release
copy build/xray-sock/Release/xray-sock.lib x64/lib/
```

Builds `tcp_listener.cpp` + `inspector.cpp` + links `lua5.4.lib` + `ws2_32`.

### One-command build

```powershell
.\scripts\build-all.ps1
```

### nlohmann/json + spdlog

Header-only — no build needed. Served directly from submodules.

## 6. Licenses

| Component | License | Obligation |
|---|---|---|
| spdlog | MIT | Keep notice |
| nlohmann/json | MIT | Keep notice |
| Lua 5.4 | MIT | Keep notice |
| xray-sock | MIT | — |
| xb-inspector | MIT | — |
