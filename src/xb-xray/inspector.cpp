#include <xray/inspector.hpp>
#include <xray/safe_queue.h>
#include <xray/xray-sock.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "uwp_sink.h"
#include "uwp_net_sink.h"
#include "lua_state.hpp"
#include <string>
#include <thread>
#include <mutex>
#include <chrono>
#include <cstdio>
#include <cstdarg>
#include <windows.h>

// windows.h defines ERROR macro → clashes with LogLevel::ERROR
#ifdef ERROR
#undef ERROR
#endif

namespace xb {

// ── Internal state ──
struct Xray::impl {
    xray::tcp_listener listener;
    xray::mpsc_queue<std::string> log_queue{};
    std::shared_ptr<uwp_file_sink> file_sink;
    std::shared_ptr<uwp_net_sink> net_sink;
    std::shared_ptr<spdlog::logger> logger;
    std::string app_name;
    bool active = false;

    // Terminate callback
    void (*on_terminate)() = nullptr;

    // Pause/Continue callbacks
    void (*on_pause)() = nullptr;
    void (*on_continue)() = nullptr;

    // Lua REPL
    lua_state lua;
    xray::spsc_queue<std::string> cmd_queue{};

    // Received data buffer (newline-delimited JSON)
    std::string recv_buf;


    // Push a repl_result JSON to the log queue (main → network thread)
    void send_repl_result(int id, bool success, const std::string& output, const std::string& err)
    {
#ifdef XRAY_VERBOSE_LOG
        spdlog::debug("[repl] enqueue result id={} success={} out_len={} err_len={}",
            id, success, output.size(), err.size());
#endif
        auto json = nlohmann::json{
            {"event", "repl_result"},
            {"payload", {
                {"id", id},
                {"success", success},
                {"output", output},
                {"error", err}
            }}
        };
        auto msg = json.dump() + "\n";
        auto* item = new std::string(std::move(msg));
        if (!log_queue.try_enqueue(item)) {
            delete item;
        }
    }

    void send_command_response(const std::string& command, bool success, const std::string& message)
    {
        auto json = nlohmann::json{
            {"event", "command_result"},
            {"payload", {
                {"command", command},
                {"success", success},
                {"message", message}
            }}
        };
        auto msg = json.dump() + "\n";
        auto* item = new std::string(std::move(msg));
        if (!log_queue.try_enqueue(item)) {
            delete item;
        }
    }

    void drain_log_queue()
    {
        // Real log messages
        int count = 0;
        std::string* msg;
        while ((msg = log_queue.try_dequeue()) != nullptr) {
            listener.send(msg->c_str(), static_cast<int>(msg->size()));
            delete msg;
            count++;
        }
        if (count > 0) {
#ifdef XRAY_VERBOSE_LOG
            spdlog::debug("[xray] drained {} items", count);
#endif
        }

        // Synthetic warning for backpressure drops
        auto drops = log_queue.drop_count();
        if (drops > 0) {
            log_queue.reset_drop_count();
            auto now = std::chrono::system_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
            time_t sec = ms / 1000;
            tm local{};
            localtime_s(&local, &sec);
            char ts[16];
            std::snprintf(ts, sizeof(ts), "%02d:%02d:%02d.%03d",
                local.tm_hour, local.tm_min, local.tm_sec,
                static_cast<int>(ms % 1000));

            auto json = nlohmann::json{
                {"event", "log"},
                {"payload", {
                    {"level", "WARN"},
                    {"timestamp", ts},
                    {"tag", "xray"},
                    {"message", "Net queue full, dropped " + std::to_string(drops) + " messages"},
                    {"thread_id", 0}
                }}
            };
            auto warn = json.dump() + "\n";
            listener.send(warn.c_str(), static_cast<int>(warn.size()));
        }
    }

    void handle_data(const char* data, int len)
    {
#ifdef XRAY_VERBOSE_LOG
        spdlog::debug("[tcp] handle_data len={}", len);
#endif
        recv_buf.append(data, len);

        for (;;) {
            auto nl = recv_buf.find('\n');
            if (nl == std::string::npos) break;
            std::string line = recv_buf.substr(0, nl);
            recv_buf.erase(0, nl + 1);

            if (line.empty()) continue;

            try {
                auto json = nlohmann::json::parse(line);
                auto it = json.find("event");
                if (it == json.end()) continue;
                std::string ev = *it;

                if (ev == "repl_eval") {
                    auto& payload = json["payload"];
                    std::string script = payload.value("script", "");
                    if (!script.empty())
                        cmd_queue.try_enqueue(std::move(script));
                } else if (ev == "terminate") {
                    if (on_terminate) on_terminate();
                } else if (ev == "help") {
                    send_command_response("help", true,
                        "Available commands:\n"
                        "  repl_eval     - Execute Lua code (payload: {id, script})\n"
                        "  terminate     - Shut down the application\n"
                        "  help          - Show this help\n"
                        "  list_bindings - Show all bound variables with types and values");
                } else if (ev == "list_bindings") {
                    std::string listing = lua.list_globals();
                    send_command_response("list_bindings", true, listing);
                } else {
                    send_command_response(ev, false,
                        "Unknown command: " + ev + ". Available: repl_eval, terminate, help");
                }
            } catch (nlohmann::json::parse_error& e) {
                spdlog::info("[tcp] json parse_error: {}  line=\"{}\"", e.what(), line);
            } catch (std::exception& e) {
                spdlog::info("[tcp] exception: {}  line=\"{}\"", e.what(), line);
            } catch (...) {
                spdlog::info("[tcp] unknown exception  line=\"{}\"", line);
            }
        }
    }

    void send_handshake()
    {
        auto json = nlohmann::json{
            {"event", "handshake"},
            {"protocol_version", 1},
            {"payload", {
                {"app_name", app_name},
                {"capabilities", {"log", "repl", "commands"}}
            }}
        };
        auto msg = json.dump() + "\n";
        listener.send(msg.c_str(), static_cast<int>(msg.size()));
    }
};

Xray::impl* Xray::self_ = nullptr;

// ── Configurable log path (default: D:\logs\) ──
static std::string s_log_path = "D:\\logs\\";

// ── Auto-bound environment info ──
struct EnvInfo {
    char device_family[64];
    char log_path[260];
    char build_config[16];
    int bound_port;
    bool connected;
    bool xbox;
    int pid;
    double uptime_sec;
};
static EnvInfo s_env{};
static auto s_env_start_time = std::chrono::steady_clock::now();

// ── Crash dump ──
static void* s_mdwd_fn = nullptr;  // Resolved MiniDumpWriteDump ptr

static LONG WINAPI crash_handler(EXCEPTION_POINTERS* ep)
{
    OutputDebugStringA("[xray] CRASH: writing minidump...\n");
    if (!s_mdwd_fn) {
        OutputDebugStringA("[xray] CRASH: MiniDumpWriteDump not resolved, skipping\n");
        return EXCEPTION_CONTINUE_SEARCH;
    }

    auto fn = (BOOL (WINAPI*)(HANDLE,DWORD,HANDLE,DWORD,void*,void*,void*))s_mdwd_fn;
    std::string dump_path = s_log_path + "xray-crash.dmp";

    int wlen = MultiByteToWideChar(CP_UTF8, 0, dump_path.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return EXCEPTION_CONTINUE_SEARCH;
    std::wstring wpath((size_t)wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, dump_path.c_str(), -1, &wpath[0], wlen);

    HANDLE hFile = CreateFile2FromAppW(wpath.c_str(),
        GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        OutputDebugStringA("[xray] CRASH: failed to create dump file\n");
        return EXCEPTION_CONTINUE_SEARCH;
    }

    struct {
        DWORD threadId;
        EXCEPTION_POINTERS* exceptionPointers;
        BOOL clientPointers;
    } mei = { GetCurrentThreadId(), ep, FALSE };

    fn(GetCurrentProcess(), GetCurrentProcessId(), hFile,
        0, &mei, nullptr, nullptr);  // MiniDumpNormal = 0
    CloseHandle(hFile);

    OutputDebugStringA("[xray] CRASH: minidump written\n");
    return EXCEPTION_CONTINUE_SEARCH;
}

void Xray::set_log_path(const char* path)
{
    if (path) {
        s_log_path = path;
        strncpy_s(s_env.log_path, path, sizeof(s_env.log_path) - 1);
        s_env.log_path[sizeof(s_env.log_path) - 1] = '\0';
    }
}

// ── Public API ──
void Xray::start(const char* app_name)
{
    if (self_) return;
    self_ = new impl();
    self_->app_name = app_name ? app_name : "xb-app";

    // Init spdlog
    self_->file_sink = std::make_shared<uwp_file_sink>(
        std::vector<std::string>{s_log_path, "D:\\logs\\"});
    self_->net_sink = std::make_shared<uwp_net_sink>(&self_->log_queue);

    self_->logger = std::make_shared<spdlog::logger>("xray",
        spdlog::sinks_init_list{self_->file_sink, self_->net_sink});
    self_->logger->set_level(spdlog::level::debug);
    self_->net_sink->set_level_filter(spdlog::level::info);
    spdlog::set_default_logger(self_->logger);
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");

    // Resolve MiniDumpWriteDump for crash dumps
    if (!s_mdwd_fn) {
        HMODULE h = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (h) {
            s_mdwd_fn = (void*)GetProcAddress(h, "MiniDumpWriteDump");
            if (s_mdwd_fn) {
                SetUnhandledExceptionFilter(crash_handler);
                spdlog::info("[xray] crash dump handler registered");
            }
            // Keep h loaded; FreeLibrary would unmap function resolution
        }
    }

    // Set up tcp_listener callbacks
    self_->listener.set_on_accept([self = self_]() {
        self_->drain_log_queue();
        self_->net_sink->set_connected(true);
        s_env.connected = true;
        self_->send_handshake();
    });

    self_->listener.set_on_disconnect([self = self_]() {
        self_->net_sink->set_connected(false);
        s_env.connected = false;
    });

    self_->listener.set_on_data([self = self_](
        const char* data, int len) {
        self_->drain_log_queue();
        self_->handle_data(data, len);
    });

    self_->listener.set_on_tick([self = self_]() {
        self_->drain_log_queue();
    });

    // Start TCP listener
    xray::sock_config cfg;
    self_->listener.start(cfg);

    // Wait for worker thread to finish initial bind loop
    while (self_->listener.state() == xray::listener_state::stopped) {
        Sleep(1);
    }

    self_->active = true;

    // Init Lua REPL if available
    if (!self_->lua.valid()) {
        spdlog::warn("Lua state init failed");
    } else {
        self_->lua.set_on_terminate(self_->on_terminate);
        self_->lua.set_on_pause(self_->on_pause);
        self_->lua.set_on_continue(self_->on_continue);

        // Populate env info
        s_env.pid = GetCurrentProcessId();
        s_env.bound_port = self_->listener.bound_port();
        s_env.connected = false;
        strncpy_s(s_env.log_path, s_log_path.c_str(), sizeof(s_env.log_path) - 1);
        s_env.log_path[sizeof(s_env.log_path) - 1] = '\0';
#ifdef _DEBUG
        strncpy_s(s_env.build_config, "Debug", sizeof(s_env.build_config) - 1);
#else
        strncpy_s(s_env.build_config, "Release", sizeof(s_env.build_config) - 1);
#endif
        s_env.build_config[sizeof(s_env.build_config) - 1] = '\0';
        s_env.uptime_sec = 0.0;

        // Query device family via C++/CX
        s_env.device_family[0] = '\0';
        auto family = Windows::System::Profile::AnalyticsInfo::VersionInfo->DeviceFamily;
        const wchar_t* w = family->Data();
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s_env.device_family,
            (int)sizeof(s_env.device_family), nullptr, nullptr);
        s_env.xbox = (strcmp(s_env.device_family, "Windows.Xbox") == 0);

        // Auto-bind env struct
        static const xb::struct_field env_fields[] = {
            xb::field("device_family", &EnvInfo::device_family),
            xb::field("log_path",      &EnvInfo::log_path),
            xb::field("build_config",  &EnvInfo::build_config),
            xb::field("bound_port",    &EnvInfo::bound_port),
            xb::field("connected",     &EnvInfo::connected),
            xb::field("xbox",          &EnvInfo::xbox),
            xb::field("pid",           &EnvInfo::pid),
            xb::field("uptime_sec",    &EnvInfo::uptime_sec),
        };
        self_->lua.bind_struct("env", &s_env, env_fields,
            sizeof(env_fields) / sizeof(env_fields[0]));

        spdlog::info("[xray] env bound: device_family={}, xbox={}, build={}, pid={}",
            s_env.device_family, s_env.xbox ? "true" : "false",
            s_env.build_config, s_env.pid);
    }
}

void Xray::update()
{
    if (!self_) return;
    if (!self_->lua.valid()) {
        static int warn = 0;
        if (++warn % 60 == 0)
            spdlog::info("[repl] update() SKIP lua invalid");
        return;
    }

    // Periodic heartbeat
#ifdef XRAY_VERBOSE_LOG
    static int frame = 0;
    if (++frame % 60 == 0)
        spdlog::info("[repl] update() alive, cmd_queue_empty={}", self_->cmd_queue.empty());
#endif

    // Consume all queued REPL commands
    int count = 0;
    std::string script;
    while (self_->cmd_queue.try_dequeue(script)) {
        spdlog::info("[repl] exec: \"{}\"", script);
        auto result = self_->lua.exec(script.c_str());
        spdlog::info("[repl] exec result len={} is_err={}",
            result.size(),
            result.find("[string \"") == 0 || result.find("error:") == 0);
        count++;

        // Determine if it's an error (result from exec error path)
        bool is_err = (result.find("[string \"") == 0 ||
                       result.find("error:") == 0 ||
                       result.find("timeout:") == 0);

        // ID not passed from Vault in simple mode — use 0
        self_->send_repl_result(0, !is_err,
            is_err ? "" : result,
            is_err ? result : "");
    }
#ifdef XRAY_VERBOSE_LOG
    if (count > 0) {
        spdlog::debug("[repl] processed {} commands", count);
    }
#endif

    // Refresh dynamic env fields
    s_env.uptime_sec = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - s_env_start_time).count();
}

void Xray::flush()
{
    if (!self_) return;
    self_->drain_log_queue();
}

void Xray::stop()
{
    if (!self_) return;
    self_->listener.stop();
    self_->active = false;
    spdlog::set_default_logger(nullptr);
    delete self_;
    self_ = nullptr;
}

void Xray::log(LogLevel level, const char* tag, const char* fmt, ...)
{
    if (!self_ || !fmt) return;
    va_list args;
    va_start(args, fmt);
    char buf[2048];
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    auto lv = static_cast<spdlog::level::level_enum>(level);
    if (tag && tag[0]) {
        self_->logger->log(lv, "[{}] {}", tag, buf);
    } else {
        self_->logger->log(lv, "{}", buf);
    }
}

void Xray::log_info(const char* tag, const char* fmt, ...)
{
    if (!self_ || !fmt) return;
    va_list args;
    va_start(args, fmt);
    char buf[2048];
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    log(LogLevel::INFO, tag, "%s", buf);
}

void Xray::log_warn(const char* tag, const char* fmt, ...)
{
    if (!self_ || !fmt) return;
    va_list args;
    va_start(args, fmt);
    char buf[2048];
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    log(LogLevel::WARN, tag, "%s", buf);
}

void Xray::log_error(const char* tag, const char* fmt, ...)
{
    if (!self_ || !fmt) return;
    va_list args;
    va_start(args, fmt);
    char buf[2048];
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    log(LogLevel::ERROR, tag, "%s", buf);
}

void Xray::log_info(const char* msg)
{
    log(LogLevel::INFO, nullptr, msg);
}

void Xray::log_warn(const char* msg)
{
    log(LogLevel::WARN, nullptr, msg);
}

void Xray::log_error(const char* msg)
{
    log(LogLevel::ERROR, nullptr, msg);
}

bool Xray::is_connected()
{
    return self_ && self_->listener.state() == xray::listener_state::connected;
}

uint16_t Xray::bound_port()
{
    return self_ ? self_->listener.bound_port() : 0;
}

void Xray::set_on_terminate(void (*fn)())
{
    if (self_) {
        self_->on_terminate = fn;
        if (self_->lua.valid())
            self_->lua.set_on_terminate(fn);
    }
}

void Xray::set_on_pause(void (*fn)())
{
    if (self_) {
        self_->on_pause = fn;
        if (self_->lua.valid())
            self_->lua.set_on_pause(fn);
    }
}

void Xray::set_on_continue(void (*fn)())
{
    if (self_) {
        self_->on_continue = fn;
        if (self_->lua.valid())
            self_->lua.set_on_continue(fn);
    }
}

void Xray::bind_impl(const char* name, void* ptr, size_t size)
{
    if (!self_ || !self_->lua.valid() || !name || !ptr) return;
    if (size == sizeof(int))        self_->lua.bind_int(name, static_cast<int*>(ptr));
    else if (size == sizeof(float)) self_->lua.bind_float(name, static_cast<float*>(ptr));
    else if (size == sizeof(double)) self_->lua.bind_double(name, static_cast<double*>(ptr));
    else if (size == sizeof(bool))  self_->lua.bind_bool(name, static_cast<bool*>(ptr));
}

void Xray::bind_array_impl(const char* name, void* ptr, size_t elem_size, int len)
{
    if (!self_ || !self_->lua.valid() || !name || !ptr || len <= 0) return;
    if (elem_size == sizeof(float)) self_->lua.bind_f32a(name, static_cast<float*>(ptr), len);
    else if (elem_size == sizeof(int)) self_->lua.bind_i32a(name, static_cast<int*>(ptr), len);
}

void Xray::bind_string(const char* name, char* buf, size_t len)
{
    if (!self_ || !self_->lua.valid() || !name || !buf || len == 0) return;
    self_->lua.bind_string(name, buf, len);
}

void Xray::bind_struct(const char* name, void* base,
                            const struct_field* fields, int count)
{
    if (!self_ || !self_->lua.valid() || !name || !base || !fields || count <= 0) return;
    self_->lua.bind_struct(name, base, fields, count);
}

} // namespace xb
