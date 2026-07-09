# 06 — Threat Model & Security

## 1. Premise

**xb-xray** is a **development tool** designed exclusively for:

- Xbox Dev Mode (locked-down console for sideloading dev apps)
- Trusted local networks (LAN)
- Active debug sessions

**Must never be compiled into release/shipping builds.**

## 2. Attack Surface

| Attack | Vector | Impact | Mitigation |
|---|---|---|---|
| Remote code execution (RCE) | Unauthenticated Lua REPL | Attacker on LAN executes arbitrary code modifying Xbox process memory | `#ifdef XB_INSPECTOR_ENABLED` (compile-time). Outside Dev Mode, the symbol doesn't exist. |
| Log interception | TCP without TLS | Attacker on LAN reads app logs | Dev Mode = trusted LAN. Release = no inspector. |
| Command injection | TCP spoofing | Attacker impersonates Vault | Vault connects to Xbox (not reverse). Only accepts connections while app is open. |
| Public REPL leak | Release build with inspector enabled | End user gains remote REPL | `#ifdef` + CI that verifies absence of symbol in release builds. |

## 3. Conscious Decisions

### No authentication, no TLS

- Both would add implementation complexity and CPU overhead on Xbox
- There is nothing to protect in Dev Mode (debug logs)
- The LAN is considered trustworthy in the development scenario
- This is an explicit, documented decision, not an oversight

### No encryption

- Logs contain debug data (file paths, texture names, variable values)
- They should not contain passwords, tokens, or user data
- If the app needs to log sensitive data, it's the developer's responsibility to sanitize before logging

## 4. The Guard: `XB_INSPECTOR_ENABLED`

### Behavior with and without the flag

| | `XB_INSPECTOR_ENABLED` defined | `XB_INSPECTOR_ENABLED` absent |
|---|---|---|
| TCP listener | Active on port 9000-9009 | Not compiled |
| Lua VM | Loaded | Not compiled |
| Variable binding | Executes | No-op |
| spdlog file sink | Active | No-op |
| spdlog net sink | Active | No-op |
| CPU overhead | ~0.5% when idle (no connection) | Zero |
| Memory overhead | ~2.5MB (Lua state + queues) | Zero |
| Network surface | Port 9000-9009 open | None |

### CI Verification

A CI step should verify that `XB_INSPECTOR_ENABLED` is not present in release builds:

```powershell
dumpbin /symbols my_homebrew.exe | Select-String "XB_INSPECTOR_ENABLED"
if ($LASTEXITCODE -eq 0) { throw "FATAL: XB_INSPECTOR_ENABLED present in release build!" }
```

## 5. Accepted (Unmitigated) Risks

- **ARP spoofing:** An attacker on the same LAN could intercept TCP traffic. Not mitigated because it would require TLS (additional complexity). Improbable in Dev Mode.
- **Port scan:** Port 9000-9009 is open while the app runs. A scanner on the LAN can detect the app. Low impact (it's a game/homebrew, not a critical service).

## 6. Developer Best Practices

1. Always compile `XB_INSPECTOR_ENABLED` only in debug builds
2. Do not commit Xbox IPs/credentials for Device Portal access
3. Use `spdlog::set_level(spdlog::level::info)` for production logging; `debug` only in dev
4. Variables bound to Lua are **live pointers** — if the object is destroyed while Lua still holds a reference, the Xbox will crash. Ensure the binding is cleaned up before object destruction (rebind or lifetime management).
