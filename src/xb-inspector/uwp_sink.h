#pragma once
#include <windows.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/common.h>
#include <cstdio>
#include <string>
#include <vector>
#include <mutex>


namespace xb {

class uwp_file_sink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    explicit uwp_file_sink(std::vector<std::string> candidates)
    {
        for (auto& dir : candidates) {
            if (ensure_dir(dir)) {
                log_dir_ = dir;
                log_file_ = dir + "xray.log";
                rotate();
                active_ = true;
                break;
            }
        }
    }

    bool active() const { return active_; }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);
        formatted.push_back('\0');

        // Always OutputDebugString (captured by VS2022, DebugView, etc.)
        OutputDebugStringA(formatted.data());

        // File logging best-effort (D:\logs\ on Xbox, none if unavailable)
        if (active_) {
            FILE* f = nullptr;
            if (fopen_s(&f, log_file_.c_str(), "a") == 0 && f) {
                fwrite(formatted.data(), 1, formatted.size() - 1, f);
                fclose(f);
            }
        }
    }

    void flush_() override
    {
        if (!active_) return;
        FILE* f = nullptr;
        if (fopen_s(&f, log_file_.c_str(), "a") == 0 && f) {
            fflush(f);
            fclose(f);
        }
    }

private:
    std::string log_dir_;
    std::string log_file_;
    bool active_ = false;
    int max_files_ = 5;

    bool ensure_dir(const std::string& path)
    {
        // Check if D: exists at all
        if (path.size() >= 2 && path[1] == ':') {
            char root[4] = { path[0], ':', '\\', '\0' };
            if (GetDriveTypeA(root) == DRIVE_NO_ROOT_DIR) {
                return false;
            }
        }

        // Try to create directory (fails if drive doesn't exist or no permission)
        if (CreateDirectoryA(path.c_str(), nullptr)) {
            return true;
        }
        DWORD err = GetLastError();
        if (err == ERROR_ALREADY_EXISTS) {
            return true;
        }
        // Path invalid, drive missing, no permission → skip file logging
        return false;
    }

    void rotate()
    {
        // Shift old logs: xray.4.log → delete, xray.3.log → xray.4.log, etc.
        for (int i = max_files_ - 1; i > 0; --i) {
            std::string old_name = log_dir_ + "xray." + std::to_string(i) + ".log";
            if (i == max_files_ - 1) {
                DeleteFileA(old_name.c_str());
            } else {
                std::string new_name = log_dir_ + "xray." + std::to_string(i + 1) + ".log";
                MoveFileExA(old_name.c_str(), new_name.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            }
        }
        // Rename current → xray.1.log
        std::string current = log_dir_ + "xray.log";
        std::string first = log_dir_ + "xray.1.log";
        MoveFileExA(current.c_str(), first.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
};

} // namespace xb
