#include "tcp_listener.h"
#include <Windows.h>
#include <ppltasks.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <string>
#include <spdlog/spdlog.h>

using namespace Windows::Networking;
using namespace Windows::Networking::Sockets;
using namespace Windows::Storage::Streams;
using namespace Windows::Foundation;
using namespace Concurrency;

namespace xray {

struct tcp_listener::impl {
    sock_config cfg;
    std::thread worker;
    bool running = false;
    std::atomic<bool> active{false};
    std::atomic<uint16_t> port{0};
    std::atomic<listener_state> current_state{listener_state::stopped};

    StreamSocketListener^ winrt_listener = nullptr;

    std::recursive_mutex client_mtx;
    StreamSocket^ client_sock = nullptr;
    DataWriter^ writer = nullptr;
    std::thread reader_thread;

    on_accept_fn on_accept;
    on_disconnect_fn on_disconnect;
    on_data_fn on_data;
    on_tick_fn on_tick;

    void run();
    bool try_bind(uint16_t port_num);
    void close_client(bool fire_disconnect = true);
    void set_state(listener_state s);

    static std::string to_utf8(Platform::String^ s);
    static Platform::String^ to_plat(const char* s, int len);
};

tcp_listener::tcp_listener() : self_(new impl) {}

tcp_listener::~tcp_listener() { stop(); delete self_; }

bool tcp_listener::start(const sock_config& cfg)
{
    if (self_->running) return false;
    self_->running = true;
    self_->active = true;
    self_->cfg = cfg;
    self_->worker = std::thread(&impl::run, self_);
    return true;
}

void tcp_listener::stop()
{
    if (!self_->running) return;
    self_->running = false;
    self_->active = false;

    // Close listener
    if (self_->winrt_listener) {
        try { delete self_->winrt_listener; }
        catch (Platform::Exception^) {}
        self_->winrt_listener = nullptr;
    }

    // Close + join reader
    self_->close_client(true);

    if (self_->worker.joinable())
        self_->worker.join();

    self_->set_state(listener_state::stopped);
}

bool tcp_listener::is_running() const { return self_->running; }
uint16_t tcp_listener::bound_port() const { return self_->port.load(); }
listener_state tcp_listener::state() const { return self_->current_state.load(); }

void tcp_listener::set_on_accept(on_accept_fn fn) { self_->on_accept = std::move(fn); }
void tcp_listener::set_on_disconnect(on_disconnect_fn fn) { self_->on_disconnect = std::move(fn); }
void tcp_listener::set_on_data(on_data_fn fn) { self_->on_data = std::move(fn); }
void tcp_listener::set_on_tick(on_tick_fn fn) { self_->on_tick = std::move(fn); }

bool tcp_listener::send(const char* data, int len)
{
    std::lock_guard<std::recursive_mutex> lock(self_->client_mtx);
    if (!self_->writer) return false;
    try {
        self_->writer->WriteBytes(
            ref new Platform::Array<unsigned char>((unsigned char*)data, len));
        create_task(self_->writer->StoreAsync()).get();
        return true;
    }
    catch (Platform::Exception^) { return false; }
}

void tcp_listener::impl::set_state(listener_state s)
{
    listener_state old = current_state.exchange(s, std::memory_order_release);
    if (old != s) {
        const char* names[] = {"stopped","listening","connected","failed"};
        spdlog::info("[tcp] state {} -> {}", names[(int)old], names[(int)s]);
    }
}

void tcp_listener::impl::close_client(bool fire_disconnect)
{
    StreamSocket^ old_sock = nullptr;
    DataWriter^ old_writer = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lock(client_mtx);
        old_sock = client_sock;
        old_writer = writer;
        client_sock = nullptr;
        writer = nullptr;
    }

    if (old_sock) {
        if (fire_disconnect && on_disconnect)
            on_disconnect();
        try { delete old_sock; }
        catch (Platform::Exception^) {}
        delete old_writer;
    }

    if (reader_thread.joinable())
        reader_thread.join();
}

bool tcp_listener::impl::try_bind(uint16_t port_num)
{
    try {
        auto listener = ref new StreamSocketListener();

        listener->ConnectionReceived += ref new TypedEventHandler<
            StreamSocketListener^, StreamSocketListenerConnectionReceivedEventArgs^>(
            [this](StreamSocketListener^ sender,
                   StreamSocketListenerConnectionReceivedEventArgs^ args) {
                auto socket = args->Socket;
                spdlog::info("[tcp] ConnectionReceived! localPort={}",
                    to_utf8(socket->Information->LocalPort));

                close_client(false);

                {
                    std::lock_guard<std::recursive_mutex> lock(client_mtx);
                    client_sock = socket;
                    writer = ref new DataWriter(socket->OutputStream);
                    writer->UnicodeEncoding = UnicodeEncoding::Utf8;
                }

                set_state(listener_state::connected);
                if (on_accept) on_accept();

                reader_thread = std::thread([this, socket]() {
                    try {
                        auto istream = socket->InputStream;
                        auto buf = ref new Buffer(4096);

                        while (active.load()) {
                            buf->Length = 0;
                            IBuffer^ result;
                            try {
                                result = create_task(istream->ReadAsync(
                                    buf, 4096, InputStreamOptions::Partial)).get();
                            }
                            catch (Platform::Exception^) { break; }

                            uint32_t count = result->Length;
                            if (count == 0) break;

                            auto dr = DataReader::FromBuffer(result);
                            dr->UnicodeEncoding = UnicodeEncoding::Utf8;
                            auto plat = dr->ReadString(count);
                            std::string utf8 = to_utf8(plat);
                            if (on_data && active.load())
                                on_data(utf8.c_str(), (int)utf8.size());
                        }
                    }
                    catch (Platform::Exception^) {}

                    {
                        std::lock_guard<std::recursive_mutex> lock(client_mtx);
                        if (client_sock == socket) {
                            client_sock = nullptr;
                            writer = nullptr;
                        }
                        else { return; }
                    }
                    if (on_disconnect && active.load())
                        on_disconnect();
                    if (active.load())
                        set_state(listener_state::listening);
                });
            });

        auto portStr = ref new Platform::String(
            std::to_wstring(port_num).c_str());
        create_task(listener->BindServiceNameAsync(portStr, SocketProtectionLevel::PlainSocket)).wait();

        winrt_listener = listener;
        port.store(port_num, std::memory_order_release);
        spdlog::info("[tcp] bind OK on port {}  localPort={}",
            port_num,
            to_utf8(listener->Information->LocalPort));
        return true;
    }
    catch (Platform::Exception^ e) {
        wchar_t buf[512];
        swprintf_s(buf, L"[tcp] BindServiceNameAsync failed: hr=0x%08X msg=%ls\n",
            e->HResult, e->Message->Data());
        OutputDebugStringW(buf);
        return false;
    }
}

void tcp_listener::impl::run()
{
    uint16_t p;
    for (p = cfg.port_start; p <= cfg.port_end; ++p) {
        if (try_bind(p)) break;
    }

    if (p > cfg.port_end) {
        port.store(0, std::memory_order_release);
        set_state(listener_state::failed);
        while (active.load()) {
            Sleep(cfg.retry_interval_ms);
            for (p = cfg.port_start; p <= cfg.port_end; ++p) {
                if (try_bind(p)) goto listening;
            }
        }
        return;
    }

listening:
    set_state(listener_state::listening);

    {
        int tick = 0;
        while (active.load()) {
            Sleep(cfg.recv_timeout_ms);
            if (on_tick) on_tick();
            if (++tick % 10 == 0 && current_state.load() == listener_state::listening)
                spdlog::info("[tcp] listening, waiting for connections...");
        }
    }

    if (winrt_listener) {
        try { delete winrt_listener; }
        catch (Platform::Exception^) {}
        winrt_listener = nullptr;
    }
    set_state(listener_state::stopped);
}

std::string tcp_listener::impl::to_utf8(Platform::String^ s)
{
    if (!s || s->IsEmpty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, s->Data(), s->Length(),
        nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s->Data(), s->Length(),
        &result[0], len, nullptr, nullptr);
    return result;
}

Platform::String^ tcp_listener::impl::to_plat(const char* s, int len)
{
    if (!s || len <= 0) return ref new Platform::String();
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s, len, nullptr, 0);
    if (wlen <= 0) return ref new Platform::String();
    auto arr = ref new Platform::Array<wchar_t>(wlen);
    MultiByteToWideChar(CP_UTF8, 0, s, len, arr->Data, wlen);
    return ref new Platform::String(arr->Data, wlen);
}

} // namespace xray
