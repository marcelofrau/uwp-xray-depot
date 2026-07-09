# 08 — Language Flavors

## 1. Current: C++ Only

xb-xray is currently implemented in C++20 with:

- **Header-only** public API (`xray/inspector.hpp`, `xray/xray-sock.hpp`)
- **Prebuilt static lib** (`xray-sock.lib`) built with MSVC for UWP x64
- **CMake import targets** for consumers
- **No C++/CX** anywhere — uses raw WinRT via cswinrt

## 2. C++ Usage Patterns

### Option A: Depot (recommended)

```cmake
add_subdirectory(path/to/uwp-xray-depot)
target_link_libraries(my_homebrew PRIVATE xb-xray)
target_compile_definitions(my_homebrew PRIVATE XB_INSPECTOR_ENABLED)
```

### Option B: Direct include + link

```cmake
target_include_directories(my_homebrew PRIVATE path/to/x64/include)
target_link_libraries(my_homebrew PRIVATE path/to/x64/lib/xray-sock.lib)
target_link_libraries(my_homebrew PRIVATE path/to/x64/lib/lua5.4.lib)
target_compile_definitions(my_homebrew PRIVATE XB_INSPECTOR_ENABLED)
```

### Option C: From source

```cmake
add_subdirectory(path/to/xray-sock/src)
target_link_libraries(my_homebrew PRIVATE xray-sock)
target_compile_definitions(my_homebrew PRIVATE XB_INSPECTOR_ENABLED)
```

## 3. Future Flavors

| Language | Status | Notes |
|---|---|---|
| C++ | ✅ Implemented | This depot |
| C++/WinRT | 🔜 Planned | Native UWP, awaitable |
| C# (WinUI) | 🔜 Planned | P/Invoke or .NET Native |
| Rust | 🔜 Planned | uwp crate + winapi |

All flavors share the same JSON protocol over TCP — the network layer is language-agnostic. The depot provides only the C++ binding. Other languages would talk to the same TCP endpoint.

## 4. C# Projection (Conceptual)

A C# binding would:

1. P/Invoke into `xray-sock.dll` (or .NET Native static link)
2. Wrap `Xray::start()` / `Xray::stop()` as an `IDisposable`
3. Expose `Xray.Log(string tag, string message)` as event
4. Marshal Lua eval results via `Task<string>`

Signature (conceptual):

```csharp
namespace Xray {
    public class Xray : IDisposable {
        public static void Start(string appName);
        public static void Stop();
        public static void Update();
        public static void Log(LogLevel level, string tag, string message);
        public static void Bind(string name, ref int value);
        public static void BindArray(string name, int[] array);
    }
}
```

## 5. Rust Projection (Conceptual)

A Rust crate would wrap the TCP protocol directly (no xray-sock dependency):

```rust
pub struct Xray {
    listener: TcpListener,
    lua: LuaState,
}

impl Xray {
    pub fn start(app_name: &str) -> io::Result<Self>;
    pub fn update(&mut self);
    pub fn log(&self, tag: &str, msg: &str);
    pub fn bind<T: LuaBind>(&mut self, name: &str, ptr: &mut T);
    pub fn stop(self);
}
```

## 6. Key Considerations

| Concern | C++ | C# | Rust |
|---|---|---|---|
| Binary size overhead | ~700KB | ~2MB | ~1MB |
| Lua integration | Raw Lua C API | P/Invoke Lua | `mlua` crate |
| TCP | Winsock2 | `System.Net.Sockets` | `std::net` |
| Thread model | Callbacks + queue | async/await | async/await |
| Dependencies | spdlog (header), json (header) | built-in JSON | `serde_json`, `log` |
| Platform target | UWP x64 | UWP x64 | UWP x64 |

## 7. Recommendation

Start with C++ (this depot) for platform compatibility and minimal dependencies. Add language projections based on demand.

The TCP JSON protocol (docs/NETWORK-PROTOCOL.md) is the language-agnostic contract. Any language that can open a TCP socket and parse JSON can write its own Vault implementation.
