# 07 — Implementation Roadmap

## Phase 0 — Depot Scaffold

**Goal:** Repository structure, submodules, Lua prebuilt, CMake targets.

| Task | Deliverable |
|---|---|
| Create directory structure (docs/, external/, src/, x64/) | Repository with final layout |
| Add git submodules (lua, spdlog, json) | `.gitmodules` configured |
| Build Lua 5.4.7 for UWP x64 (static lib) | `x64/lib/lua5.4.lib` + headers in `x64/include/lua/` |
| CMakeLists.txt with import targets | `lua5.4`, `spdlog`, `nlohmann_json` working |
| Copy licenses to `lic/` | SPDLOG-LICENSE.txt, JSON-LICENSE.txt, LUA-LICENSE.txt |
| Initial README.md | Description, consumption guide, badges |
| Documentation: ARCHITECTURE, DEPOT-STRUCTURE, SECURITY, ROADMAP | 4 docs |

**Success criteria:** An external CMake project can `add_subdirectory(uwp-xray-depot)` and link against `lua5.4` and `spdlog`.

---

## Phase 1 — xray-sock + xb-inspector Core

**Goal:** Functional TCP listener, `Inspector::start/stop/log` API, Vault connection.

| Task | Deliverable |
|---|---|
| Implement `safe_queue.h` (MPSC + SPSC) | Header-only, lock-free |
| Implement `xray-sock` (non-blocking TCP listener, port fallback 9000-9009) | `x64/lib/xray-sock.lib` |
| Implement `uwp_sink.h` (spdlog UWP sink: file + OutputDebugString) | Sink header |
| Implement `uwp_net_sink.h` (spdlog TCP sink: enqueue to MPSC, network thread sends) | Sink header |
| Implement `Inspector::start/stop/log/is_connected` | Minimal working API |
| Implement JSON handshake on accept | Vault receives `app_name`, `protocol_version` |
| Build xray-sock (.lib) | `x64/lib/xray-sock.lib` + `x64/include/xray/xray-sock.hpp` |
| Documentation: 01-network-protocol, 02-xbox-native-lib, 03-logging | 3 docs |

**Success criteria:** Test UWP app calls `Inspector::start()` and `Inspector::log_info("hello")`. Vault (via telnet/netcat) connects on port 9000, receives handshake + real-time logs.

---

## Phase 2 — spdlog + TCP Forward Integrated

**Goal:** Complete logging with backpressure, file + TCP, working with and without Vault.

| Task | Deliverable |
|---|---|
| Integrate `uwp_file_sink` and `uwp_net_sink` into same spdlog logger | Dual logging |
| Implement backpressure policy (drop-oldest + synthetic warning) | MPSC queue with controlled overflow |
| Configure log levels per sink (DEBUG file, INFO net) | Per-sink filtering |
| Test: app runs 5 min without Vault → logs in file. Connect Vault → real-time logs. | Stability |
| Test: slow Vault → backpressure triggers without crashing app | Robustness |
| Documentation: update docs based on learnings | - |

**Success criteria:** Same app from Phase 1, now with dual logging and backpressure. Disconnecting Vault doesn't affect app. Reconnecting resumes stream.

---

## Phase 3 — Lua REPL

**Goal:** Lua 5.4 embedded, Sol2 binding, functional REPL via Vault.

| Task | Deliverable |
|---|---|
| Integrate Sol2 header in external/ | Submodule or copy |
| Create `lua_state.hpp` (lua_State management, sandbox, pretty print) | Safe REPL |
| Implement `Inspector::bind<T>(name, ptr)` | Macro wrapping Sol2 |
| Implement `Inspector::bind_type<T>(name, fields...)` | Usertype registration via Sol2 |
| Implement SPSC consumption on main thread (frame start) | Command queue |
| Implement safe execution (lua_pcall, timeout, stack cleanup) | Protection against infinite loops/crashes |
| Integrate result into MPSC → network thread sends `repl_result` | Closed loop |
| Test: Vault sends `return 1+1` → Xbox responds `2` | Basic REPL |
| Test: bound variable → modify from Vault → reflect in C++ | Binding works |
| Test: syntax error script → error response | Error handling |
| Test: infinite loop → timeout → no crash | Safety |
| Documentation: 04-lua-repl, 08-flavors | 2 docs |

**Success criteria:** App exposes `player.health`, Vault types `player.health = 999`, the value changes in C++ and the game reacts.

---

## Phase 4 — Polish & Cross-Language

**Goal:** CI, build scripts, C# and Rust support (documentation + examples).

| Task | Deliverable |
|---|---|
| Complete build-all.ps1 script | One-command build |
| CI pipeline (GitHub Actions) | Build + `#ifdef` verification in release |
| C++ UWP consumption example | `samples/cpp-sample/` |
| C# port guide (MoonSharp) | Section in LANGUAGE-BINDINGS.md + examples |
| Rust port guide (mlua) | Section in LANGUAGE-BINDINGS.md + examples |
| README with badges, quickstart, component table | Finalized |

---

## Visual Summary

```
Phase 0     Phase 1        Phase 2      Phase 3        Phase 4
──────     ──────        ──────      ──────        ──────
Scaffold   TCP + API     Log+Backpr  Lua REPL      CI + Cross

[structure] [start/stop]  [file log]  [Sol2 bind]  [build-all.ps1]
[submods]  [handshake]   [net log]   [SPSC→main]  [CI pipeline]
[Lua .lib] [port fallbk] [drop-oldst] [sandbox]    [C# guide]
[CMake]    [log INFO]    [warn synth] [repl_result] [Rust guide]
```

## Phase Dependencies

```
Phase 0 ──► Phase 1 ──► Phase 2 ──► Phase 3 ──► Phase 4
   │            │
   │            └──────► Phase 3 (needs TCP for REPL to work)
   │
   └───────────────────► Phase 4 (needs structure for CI)
```
