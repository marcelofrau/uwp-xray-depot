# 01 — Network Protocol

## 1. Transport Layer

- **Protocol:** Raw TCP (not HTTP, not WebSocket)
- **Default port:** 9000
- **Fallback range:** 9000–9009 (inclusive)
- **Encoding:** UTF-8
- **Message delimiter:** `\n` (newline)
- **Non-blocking:** Yes, on both ends

Each message is a single JSON line terminated by `\n`. No length prefix — the parser reads until `\n`.

## 2. Protocol Versioning

Every connection begins with a handshake declaring `protocol_version`. Current value is `1`.

```
protocol_version: 1
```

If a breaking schema change is needed in the future, this number increments. The Vault may reject unknown versions.

## 3. Messages

### 3.1 Handshake (Xbox → Vault)

Sent immediately after the Vault connects. This is the first message in the stream.

```json
{
  "event": "handshake",
  "protocol_version": 1,
  "payload": {
    "app_name": "OpenBurningSuite",
    "app_id": "com.trespordez.openburningsuite",
    "version": "1.0.0",
    "language": "C++",
    "environment": "UWP_DevMode",
    "capabilities": ["log", "repl"]
  }
}
```

The Vault uses `app_name` for UI display and `capabilities` to determine whether the REPL is available (Lua may be compiled out).

### 3.2 Log (Xbox → Vault)

Sent on every log call in the app, in real time.

```json
{
  "event": "log",
  "payload": {
    "level": "WARN",
    "timestamp": "16:42:11.002",
    "tag": "AUDIO",
    "message": "Sample buffer underrun. Dropping frame.",
    "thread_id": 4712
  }
}
```

**`level`:** `DEBUG`, `INFO`, `WARN`, `ERROR`, `FATAL`

**`tag`:** Optional category defined by the app (e.g. `AUDIO`, `RENDER`, `INPUT`, `FS`). Falls back to `GENERAL` when empty.

**`thread_id`:** ID of the thread that emitted the log, useful for diagnosing concurrency issues.

### 3.3 REPL Command (Vault → Xbox)

Sent when the developer types into the REPL input and presses Enter.

```json
{
  "event": "repl_eval",
  "payload": {
    "id": 1,
    "script": "World.player.health = 100; return World.player.name"
  }
}
```

**`id`:** Sequential number incremented by the Vault. The Xbox echoes the same `id` in its response for correlation.

### 3.4 REPL Response (Xbox → Vault)

Result of the Lua script execution.

```json
{
  "event": "repl_result",
  "payload": {
    "id": 1,
    "success": true,
    "output": "Marcos",
    "error": ""
  }
}
```

On syntax or runtime error:

```json
{
  "event": "repl_result",
  "payload": {
    "id": 1,
    "success": false,
    "output": "",
    "error": "[string \"...\"]:1: attempt to index a nil value (global 'World')"
  }
}
```

### 3.5 Backpressure Warning (Xbox → Vault)

Automatically injected by the library when logs are dropped due to backpressure (Vault consuming too slowly).

```json
{
  "event": "log",
  "payload": {
    "level": "WARN",
    "timestamp": "16:42:11.002",
    "tag": "XB-INSPECTOR",
    "message": "142 logs dropped due to backpressure (queue full, drop-oldest)"
  }
}
```

## 4. Connection Lifecycle

```
Vault                          Xbox
  │                              │
  │────── TCP Connect ──────────►│ (port 9000-9009)
  │                              │
  │◄────── Handshake ────────────│ (app_name, protocol_version, capabilities)
  │                              │
  │◄────── Log stream ──────────►│ (bidirectional continuous flow)
  │────── repl_eval ────────────►│
  │◄────── repl_result ──────────│
  │                              │
  │        ...                   │
  │                              │
  │◄────── TCP Disconnect ───────│ (app closed or crashed)
  │                              │
  │   UI: "Waiting for app..."   │
```

## 5. Port Scan

The Vault does not auto-connect. The user clicks **Scan**.

```
for port in range(9000, 9010):
    try TCP connect(xbox_ip, port)  non-blocking
    if connect ok:
        read handshake
        add to found apps list
        continue scanning (may be multiple apps active)

if no ports open:
    show "No active Inspector found"
if 1 or more:
    show app selector in UI
```

The scan is async. Each connection attempt takes ~2ms on LAN (RST is instant on closed ports). A full 10-port scan completes in <50ms.

## 6. Disconnect Handling

| Event | Vault Behavior | Xbox Behavior |
|---|---|---|
| App closes normally | Closes connection, UI marks disconnected | Closes socket on Inspector destruction |
| App crashes | Socket drops, input disabled, shows "Waiting for app..." | Process dies, OS closes socket |
| Vault closes | — | Network thread detects closed connection, resumes dropping logs |
| Network drops | TCP timeout, shows disconnected | Keep-alive timeout, frees resources |
