# 05 — uwp-xray-depot Structure

## 1. Layout

```
uwp-xray-depot/
│
├── CMakeLists.txt              # Import targets for consumers
├── README.md
│
├── lic/                        # Dependency licenses
│   ├── SPDLOG-LICENSE.txt
│   ├── JSON-LICENSE.txt        # MIT (nlohmann/json)
│   └── LUA-LICENSE.txt
│
├── external/                   # Git submodules (source)
│   ├── lua/                    # https://github.com/lua/lua  v5.4.7
│   ├── spdlog/                 # https://github.com/gabime/spdlog  v1.x
│   └── json/                   # https://github.com/nlohmann/json  v3.11.x
│
├── src/                        | Custom artifact build
│   ├── CMakeLists.txt          # xray-sock.lib build
│   ├── xray-sock/
│   │   ├── tcp_listener.h
│   │   ├── tcp_listener.cpp
│   │   └── safe_queue.h        # MPSC + SPSC queues
│   └── xb-inspector/
│       └── uwp_sink.h          # Custom spdlog sink for UWP
│
├── scripts/                    # Build scripts
│   ├── build-lua.ps1           # Build lua5.4.lib (UWP x64)
│   ├── build-xray-sock.ps1     # Build xray-sock.lib
│   └── build-all.ps1           # Run everything
│
├── x64/                        # Prebuilts (output, committed)
│   ├── lib/
│   │   ├── lua5.4.lib
│   │   └── xray-sock.lib
│   └── include/
│       ├── lua/
│       │   ├── lua.h
│       │   ├── luaconf.h
│       │   ├── lualib.h
│       │   ├── lauxlib.h
│       │   └── lua.hpp
│       └── xray/
│           ├── inspector.hpp   # Public native lib API
│           └── xray-sock.hpp   # Socket wrapper API
│
└── docs/
    ├── 00-architecture.md
    ├── 01-network-protocol.md
    ├── 02-xbox-native-lib.md
    ├── 03-logging.md
    ├── 04-lua-repl.md
    ├── 05-xray-depot.md        # ← this document
    ├── 06-threat-model.md
    ├── 07-roadmap.md
    └── 08-flavors.md
```

## 2. Submodules

```bash
git submodule add https://github.com/lua/lua external/lua
git submodule add https://github.com/gabime/spdlog external/spdlog
git submodule add https://github.com/nlohmann/json external/json
```

Per-submodule Branch/Tag:

| Submodule | Tag/Branch | Version |
|---|---|---|
| `lua` | `v5.4.7` | 5.4.7 |
| `spdlog` | `v1.x` | ~1.14 |
| `json` | `v3.11.3` | 3.11.3 |
| `moodycamel` (future) | `master` | lock-free queue |

## 3. CMakeLists.txt

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

# ── xb-inspector (interface: headers + dependencies) ──
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

target_link_libraries(my_homebrew PRIVATE
    xb-inspector spdlog nlohmann_json lua5.4 xray-sock
)

# And in code:
#include <xray/inspector.hpp>

int main() {
    xb::Inspector::start();
    xb::Inspector::bind_type<Player>("Player", "health", &Player::health);
    xb::Inspector::bind("player", &g_player);
    // ...
}
```

## 5. Building Artifacts

### Lua 5.4 (.lib)

```powershell
# scripts/build-lua.ps1
cd external/lua
cl /c /O2 /MD /DWIN32 /D_CRT_SECURE_NO_WARNINGS /Fo../../x64/lib/lua.obj src/*.c
lib /out:../../x64/lib/lua5.4.lib ../../x64/lib/lua.obj
copy src\lua.h ../../x64/include\lua\
copy src\luaconf.h ../../x64/include\lua\
copy src\lualib.h ../../x64/include\lua\
copy src\lauxlib.h ../../x64/include\lua\
copy src\lua.hpp ../../x64/include\lua\
```

### xray-sock (.lib)

Built via the internal `src/CMakeLists.txt` which compiles with `cswinrt` for WinRT projections (UWP socket APIs).

### nlohmann/json + spdlog

Header-only — no build needed. Headers are served directly from submodules.

## 6. Licenses

| Component | License | Obligation |
|---|---|---|
| spdlog | MIT | Keep notice |
| nlohmann/json | MIT | Keep notice |
| Lua 5.4 | MIT | Keep notice |
| xray-sock | MIT | — |
| xb-inspector | MIT | — |
