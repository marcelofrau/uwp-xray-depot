#pragma once
#include <windows.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/common.h>
#include <nlohmann/json.hpp>
#include <xray/safe_queue.h>
#include <xray/xray-sock.hpp>
#include <chrono>
#include <string>
#include <mutex>

namespace xb {

class uwp_net_sink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    explicit uwp_net_sink(xray::mpsc_queue<std::string>* queue)
        : queue_(queue)
    {
    }

    void set_connected(bool connected)
    {
        connected_ = connected;
    }

    void set_level_filter(spdlog::level::level_enum lv)
    {
        level_filter_ = lv;
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        if (!connected_) return;
        // Per-sink level filter: skip DEBUG/trace over net, only INFO+
        if (msg.level < level_filter_) return;

        using clock = std::chrono::system_clock;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            msg.time.time_since_epoch()).count();
        auto sec = ms / 1000;
        auto msec = ms % 1000;
        auto t = static_cast<time_t>(sec);
        tm local{};
        localtime_s(&local, &t);

        char timestamp[16];
        std::snprintf(timestamp, sizeof(timestamp), "%02d:%02d:%02d.%03d",
            local.tm_hour, local.tm_min, local.tm_sec, static_cast<int>(msec));

        std::string tag;
        if (msg.logger_name.size() > 0)
            tag = std::string(msg.logger_name.data(), msg.logger_name.size());
        auto json = nlohmann::json{
            {"event", "log"},
            {"payload", {
                {"level", level_string(msg.level)},
                {"timestamp", timestamp},
                {"tag", tag},
                {"message", std::string(msg.payload.data(), msg.payload.size())},
                {"thread_id", static_cast<uint32_t>(GetCurrentThreadId())}
            }}
        };

        auto serialized = json.dump() + "\n";
        auto* item = new std::string(std::move(serialized));
        if (!queue_->try_enqueue(item)) {
            delete item;
        }
    }

    void flush_() override {}

private:
    xray::mpsc_queue<std::string>* queue_;
    bool connected_ = false;
    spdlog::level::level_enum level_filter_ = spdlog::level::info;

    static const char* level_string(spdlog::level::level_enum lv)
    {
        switch (lv) {
            case spdlog::level::debug: return "DEBUG";
            case spdlog::level::info:  return "INFO";
            case spdlog::level::warn:  return "WARN";
            case spdlog::level::err:   return "ERROR";
            case spdlog::level::critical: return "FATAL";
            default: return "UNKNOWN";
        }
    }

};

} // namespace xb
