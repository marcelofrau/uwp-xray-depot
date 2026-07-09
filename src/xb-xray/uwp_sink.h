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
                SYSTEMTIME st;
                GetLocalTime(&st);
                char name[64];
                std::snprintf(name, sizeof(name), "xray-%04d-%02d-%02d-%02d-%02d-%02d.log",
                    st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
                log_file_ = dir + name;
                cleanup_old_logs();
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
            if (fopen_s(&f, log_file_.c_str(), "ab") == 0 && f) {
                fwrite(formatted.data(), 1, formatted.size() - 1, f);
                fflush(f);
                fclose(f);
            }
        }
    }

    void flush_() override
    {
        if (!active_) return;
        FILE* f = nullptr;
        if (fopen_s(&f, log_file_.c_str(), "ab") == 0 && f) {
            fflush(f);
            fclose(f);
        }
    }

private:
    std::string log_dir_;
    std::string log_file_;
    bool active_ = false;

    bool ensure_dir(const std::string& path)
    {
        if (path.size() >= 2 && path[1] == ':') {
            char root[4] = { path[0], ':', '\\', '\0' };
            if (GetDriveTypeA(root) == DRIVE_NO_ROOT_DIR) {
                return false;
            }
        }

        if (CreateDirectoryA(path.c_str(), nullptr)) {
            return true;
        }
        DWORD err = GetLastError();
        if (err == ERROR_ALREADY_EXISTS) {
            return true;
        }
        return false;
    }

    void cleanup_old_logs()
    {
        WIN32_FIND_DATAA ffd;
        std::string pattern = log_dir_ + "xray-*.log";
        HANDLE hFind = FindFirstFileA(pattern.c_str(), &ffd);
        if (hFind == INVALID_HANDLE_VALUE) return;

        FILETIME now_ft;
        GetSystemTimeAsFileTime(&now_ft);
        ULARGE_INTEGER now;
        now.LowPart = now_ft.dwLowDateTime;
        now.HighPart = now_ft.dwHighDateTime;

        do {
            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            ULARGE_INTEGER ft;
            ft.LowPart = ffd.ftCreationTime.dwLowDateTime;
            ft.HighPart = ffd.ftCreationTime.dwHighDateTime;
            if (now.QuadPart - ft.QuadPart > 7LL * 24 * 3600 * 10000000) {
                DeleteFileA((log_dir_ + ffd.cFileName).c_str());
            }
        } while (FindNextFileA(hFind, &ffd) != 0);
        FindClose(hFind);
    }
};

} // namespace xb
