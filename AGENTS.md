# AGENTS.md — uwp-xray-depot

## Repo type

**Depot** (not app). CMake import targets for XB-Inspector — native remote diagnostics for UWP homebrews on Xbox Dev Mode. Consumers `add_subdirectory()` + `target_link_libraries(... xb-inspector)`.

Sibling depot @ `F:\workspace\uwp-dep` — same pattern, mature w/ prebuilts.

## Current state

Phase 0 in progress. `external/`, `src/`, `scripts/`, `x64/` directories described in docs but not fully populated. Single commit (initial scaffold). No CI/tests yet.

## Architecture

| Component | Type | Location |
|---|---|---|
| `xb-inspector` | Interface lib (aggregator) | `x64/include/xray/inspector.hpp` |
| `xray-sock` | Prebuilt static lib | `x64/lib/xray-sock.lib` |
| `lua5.4` | Prebuilt static lib | `x64/lib/lua5.4.lib` |
| `spdlog` | Header-only (submodule) | `external/spdlog/include/` |
| `nlohmann_json` | Header-only (submodule) | `external/json/single_include/` |
| `safe_queue` | MPSC (logs) + SPSC (commands) | `src/xray-sock/safe_queue.h` |

Protocol: JSON over raw TCP, port 9000-9009, newline-delimited. Handshake → log stream ↔ REPL eval/result. Vault scans ports, Xbox is passive TCP server.

### CMake targets

```
xb-inspector → links spdlog, nlohmann_json, lua5.4, xray-sock
```

### Critical: `#ifdef XB_INSPECTOR_ENABLED`

All inspector code guarded by this define. **Must never be in release/shipping builds.** Verify:
```powershell
dumpbin /symbols my_homebrew.exe | Select-String "XB_INSPECTOR_ENABLED"
```

Lua-bound variables = live C++ pointers. `unbind()` before object destruction.

No TLS/auth — trusted LAN + Dev Mode only.

### Lua execution model

Lua runs on **main thread** at frame start (not network thread). SPSC queue consumed before `update()`. Lua state sandboxed — `io`, `os`, `dofile`, `loadfile`, `require` removed.

## Build

Prebuilts committed to `x64/`. Build scripts in `scripts/`:
- `build-lua.ps1` — MSVC compile Lua 5.4.7 → static lib
- `build-xray-sock.ps1` — CMake + cswinrt for WinRT sockets
- `build-all.ps1` — runs everything

Requires MSVC (Visual Studio C++ tools) with UWP x64 support.

## Phases (from docs/07-roadmap.md)

- **Phase 0** — Depot scaffold: submodules, Lua .lib, CMake targets, dirs
- **Phase 1** — xray-sock + inspector core: TCP, start/stop/log, handshake
- **Phase 2** — Logging: dual file+net sinks, backpressure
- **Phase 3** — Lua REPL: Sol2, bind, sandbox, SPSC→main
- **Phase 4** — Polish: CI, build-all.ps1, samples, cross-language guides

## Docs index

| File | Covers |
|---|---|
| `docs/00-architecture.md` | Overview, diagrams, ADRs |
| `docs/01-network-protocol.md` | JSON protocol, handshake, port scan |
| `docs/02-xbox-native-lib.md` | C++ API, threading, queues, lifecycle |
| `docs/03-logging.md` | spdlog + UWP sinks + backpressure |
| `docs/04-lua-repl.md` | Lua 5.4 + Sol2 bind + sandbox |
| `docs/05-xray-depot.md` | Depot structure, CMake, submodules |
| `docs/06-threat-model.md` | Security, `#ifdef XB_INSPECTOR_ENABLED` |
| `docs/07-roadmap.md` | Implementation phases 0-4 |
| `docs/08-flavors.md` | C++ vs C# vs Rust |
