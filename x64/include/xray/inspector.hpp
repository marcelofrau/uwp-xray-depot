#pragma once

#ifdef XB_INSPECTOR_ENABLED

#include <cstdint>
#include <cstdarg>
#include <xray/struct_field.hpp>

#ifdef ERROR
#undef ERROR
#endif

namespace xb {

enum class LogLevel : int {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3,
    FATAL = 4
};

class Xray {
public:
    static void start(const char* app_name = nullptr);
    static void set_log_path(const char* path);
    static void set_device_family(const char* family);
    static void stop();

    static void log(LogLevel level, const char* tag, const char* fmt, ...);

    static void log_info(const char* msg);
    static void log_warn(const char* msg);
    static void log_error(const char* msg);

    static void log_info(const char* tag, const char* fmt, ...);
    static void log_warn(const char* tag, const char* fmt, ...);
    static void log_error(const char* tag, const char* fmt, ...);

    // Lua REPL — call once per frame at start
    static void update();

    // Flush log queue (send pending logs+REPL results over TCP)
    static void flush();

    // Bind a scalar variable (int, float, bool)
    template<typename T>
    static void bind(const char* name, T* ptr)
    {
        bind_impl(name, ptr, sizeof(T));
    }

    // Bind an array
    template<typename T>
    static void bind_array(const char* name, T* ptr, int len)
    {
        bind_array_impl(name, ptr, sizeof(T), len);
    }

    // Bind a fixed-size char buffer as read/write string
    static void bind_string(const char* name, char* buf, size_t len);

    // Bind a POD struct by field array
    static void bind_struct(const char* name, void* base,
                            const struct_field* fields, int count);

    static bool is_connected();
    static uint16_t bound_port();

    // Terminate callback — called on "terminate" command from client
    static void set_on_terminate(void (*fn)());

    // Pause/Continue callbacks — called on "pause()" / "continue()" REPL commands
    static void set_on_pause(void (*fn)());
    static void set_on_continue(void (*fn)());

private:
    struct impl;
    static impl* self_;

    // Type-erased dispatch (templates above forward here)
    static void bind_impl(const char* name, void* ptr, size_t size);
    static void bind_array_impl(const char* name, void* ptr, size_t elem_size, int len);
};

} // namespace xb

#define XRAY_LOG(level, tag, ...) \
    xb::Xray::log(level, tag, __VA_ARGS__)
#define XRAY_INFO(tag, ...) \
    xb::Xray::log_info(tag, __VA_ARGS__)
#define XRAY_WARN(tag, ...) \
    xb::Xray::log_warn(tag, __VA_ARGS__)
#define XRAY_ERROR(tag, ...) \
    xb::Xray::log_error(tag, __VA_ARGS__)

#else // !XB_INSPECTOR_ENABLED

#define XRAY_LOG(level, tag, ...)
#define XRAY_INFO(tag, ...)
#define XRAY_WARN(tag, ...)
#define XRAY_ERROR(tag, ...)

#endif // XB_INSPECTOR_ENABLED
