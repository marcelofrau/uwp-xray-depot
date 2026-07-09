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

// windows.h defines ERROR macro → clashes with LogLevel::ERROR
#ifdef ERROR
#undef ERROR
#endif

namespace xb {

// ── Internal state ──
struct Inspector::impl {
    xray::tcp_listener listener;
    xray::mpsc_queue<std::string> log_queue{};
    std::shared_ptr<uwp_file_sink> file_sink;
    std::shared_ptr<uwp_net_sink> net_sink;
    std::shared_ptr<spdlog::logger> logger;
    std::string app_name;
    bool active = false;

    // Lua REPL
    lua_state lua;
    xray::spsc_queue<std::string> cmd_queue{};

    // Received data buffer (newline-delimited JSON)
    std::string recv_buf;

    // Push a repl_result JSON to the log queue (main → network thread)
    void send_repl_result(int id, bool success, const std::string& output, const std::string& err)
    {
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

    void drain_log_queue()
    {
        // Real log messages
        std::string* msg;
        while ((msg = log_queue.try_dequeue()) != nullptr) {
            listener.send(msg->c_str(), static_cast<int>(msg->size()));
            delete msg;
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
                    if (!script.empty()) {
                        cmd_queue.try_enqueue(std::move(script));
                    }
                }
            } catch (...) {
                // Ignore malformed JSON
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
                {"capabilities", {"log", "repl"}}
            }}
        };
        auto msg = json.dump() + "\n";
        listener.send(msg.c_str(), static_cast<int>(msg.size()));
    }
};

Inspector::impl* Inspector::self_ = nullptr;

// ── Public API ──
void Inspector::start(const char* app_name)
{
    if (self_) return;
    self_ = new impl();
    self_->app_name = app_name ? app_name : "xb-app";

    // Init spdlog
    self_->file_sink = std::make_shared<uwp_file_sink>(
        std::vector<std::string>{"D:\\logs\\"});
    self_->net_sink = std::make_shared<uwp_net_sink>(&self_->log_queue);

    self_->logger = std::make_shared<spdlog::logger>("xray",
        spdlog::sinks_init_list{self_->file_sink, self_->net_sink});
    self_->logger->set_level(spdlog::level::debug);
    self_->net_sink->set_level_filter(spdlog::level::info);
    spdlog::set_default_logger(self_->logger);
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] [%n] %v");

    // Set up tcp_listener callbacks
    self_->listener.set_on_accept([self = self_]() {
        self_->net_sink->set_connected(true);
        self_->send_handshake();
    });

    self_->listener.set_on_disconnect([self = self_]() {
        self_->net_sink->set_connected(false);
    });

    self_->listener.set_on_data([self = self_](
        const char* data, int len) {
        self_->handle_data(data, len);
    });

    self_->listener.set_on_tick([self = self_]() {
        self_->drain_log_queue();
    });

    // Start TCP listener
    xray::sock_config cfg;
    self_->listener.start(cfg);

    self_->active = true;

    // Init Lua REPL if available
    if (!self_->lua.valid()) {
        spdlog::warn("[xray] Lua state init failed");
    }
}

void Inspector::update()
{
    if (!self_ || !self_->lua.valid()) return;

    // Consume all queued REPL commands
    std::string script;
    while (self_->cmd_queue.try_dequeue(script)) {
        auto result = self_->lua.exec(script.c_str());

        // Determine if it's an error (starts with error pattern)
        bool is_err = (result.find("error:") == 0 ||
                       result.find("syntax error") != std::string::npos ||
                       result.find("runtime error") != std::string::npos ||
                       result.find("timeout:") == 0);

        // ID not passed from Vault in simple mode — use 0
        self_->send_repl_result(0, !is_err,
            is_err ? "" : result,
            is_err ? result : "");
    }
}

void Inspector::stop()
{
    if (!self_) return;
    self_->listener.stop();
    self_->active = false;
    spdlog::set_default_logger(nullptr);
    delete self_;
    self_ = nullptr;
}

void Inspector::log(LogLevel level, const char* tag, const char* fmt, ...)
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

void Inspector::log_info(const char* tag, const char* fmt, ...)
{
    if (!self_ || !fmt) return;
    va_list args;
    va_start(args, fmt);
    char buf[2048];
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    log(LogLevel::INFO, tag, "%s", buf);
}

void Inspector::log_warn(const char* tag, const char* fmt, ...)
{
    if (!self_ || !fmt) return;
    va_list args;
    va_start(args, fmt);
    char buf[2048];
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    log(LogLevel::WARN, tag, "%s", buf);
}

void Inspector::log_error(const char* tag, const char* fmt, ...)
{
    if (!self_ || !fmt) return;
    va_list args;
    va_start(args, fmt);
    char buf[2048];
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    log(LogLevel::ERROR, tag, "%s", buf);
}

void Inspector::log_info(const char* msg)
{
    log(LogLevel::INFO, nullptr, msg);
}

void Inspector::log_warn(const char* msg)
{
    log(LogLevel::WARN, nullptr, msg);
}

void Inspector::log_error(const char* msg)
{
    log(LogLevel::ERROR, nullptr, msg);
}

bool Inspector::is_connected()
{
    return self_ && self_->listener.state() == xray::listener_state::connected;
}

uint16_t Inspector::bound_port()
{
    return self_ ? self_->listener.bound_port() : 0;
}

void Inspector::bind_impl(const char* name, void* ptr, size_t size)
{
    if (!self_ || !self_->lua.valid() || !name || !ptr) return;
    if (size == sizeof(int))       self_->lua.bind_int(name, static_cast<int*>(ptr));
    else if (size == sizeof(float)) self_->lua.bind_float(name, static_cast<float*>(ptr));
    else if (size == sizeof(bool))  self_->lua.bind_bool(name, static_cast<bool*>(ptr));
}

void Inspector::bind_array_impl(const char* name, void* ptr, size_t elem_size, int len)
{
    if (!self_ || !self_->lua.valid() || !name || !ptr || len <= 0) return;
    if (elem_size == sizeof(float)) self_->lua.bind_f32a(name, static_cast<float*>(ptr), len);
    else if (elem_size == sizeof(int)) self_->lua.bind_i32a(name, static_cast<int*>(ptr), len);
}

} // namespace xb
