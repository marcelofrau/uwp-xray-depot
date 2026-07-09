#include "tcp_listener.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cassert>
#include <thread>
#include <atomic>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace xray {

struct tcp_listener::impl {
    sock_config cfg;
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<uint16_t> port{0};
    SOCKET listen_sock{INVALID_SOCKET};
    SOCKET client_sock{INVALID_SOCKET};
    std::atomic<listener_state> current_state{listener_state::stopped};

    on_accept_fn on_accept;
    on_disconnect_fn on_disconnect;
    on_data_fn on_data;
    on_tick_fn on_tick;

    void run();
    bool try_bind(uint16_t port);
    void cleanup_socket(SOCKET& s);
    void close_client();
    void set_state(listener_state s);
};

tcp_listener::tcp_listener()
    : self_(new impl)
{
}

tcp_listener::~tcp_listener()
{
    stop();
    delete self_;
}

bool tcp_listener::start(const sock_config& cfg)
{
    if (self_->running.load()) {
        return false;
    }

    self_->cfg = cfg;

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return false;
    }

    self_->running = true;
    self_->worker = std::thread(&impl::run, self_);
    return true;
}

void tcp_listener::stop()
{
    if (!self_->running.load()) {
        return;
    }

    self_->running = false;

    if (self_->listen_sock != INVALID_SOCKET) {
        shutdown(self_->listen_sock, SD_BOTH);
        closesocket(self_->listen_sock);
        self_->listen_sock = INVALID_SOCKET;
    }

    self_->close_client();

    if (self_->worker.joinable()) {
        self_->worker.join();
    }

    WSACleanup();
    self_->set_state(listener_state::stopped);
}

bool tcp_listener::is_running() const
{
    return self_->running.load();
}

uint16_t tcp_listener::bound_port() const
{
    return self_->port.load();
}

listener_state tcp_listener::state() const
{
    return self_->current_state.load();
}

void tcp_listener::set_on_accept(on_accept_fn fn)
{
    self_->on_accept = std::move(fn);
}

void tcp_listener::set_on_disconnect(on_disconnect_fn fn)
{
    self_->on_disconnect = std::move(fn);
}

void tcp_listener::set_on_data(on_data_fn fn)
{
    self_->on_data = std::move(fn);
}

void tcp_listener::set_on_tick(on_tick_fn fn)
{
    self_->on_tick = std::move(fn);
}

bool tcp_listener::send(const char* data, int len)
{
    if (self_->client_sock == INVALID_SOCKET) {
        return false;
    }
    int n = ::send(self_->client_sock, data, len, 0);
    return n > 0;
}

void tcp_listener::impl::set_state(listener_state s)
{
    current_state.store(s, std::memory_order_release);
}

bool tcp_listener::impl::try_bind(uint16_t port_num)
{
    listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCKET) {
        return false;
    }

    u_long nonblock = 1;
    ioctlsocket(listen_sock, FIONBIO, &nonblock);

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR,
        reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_num);

    if (bind(listen_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        cleanup_socket(listen_sock);
        return false;
    }

    if (listen(listen_sock, 1) != 0) {
        cleanup_socket(listen_sock);
        return false;
    }

    port.store(port_num, std::memory_order_release);
    return true;
}

void tcp_listener::impl::cleanup_socket(SOCKET& s)
{
    if (s != INVALID_SOCKET) {
        shutdown(s, SD_BOTH);
        closesocket(s);
        s = INVALID_SOCKET;
    }
}

void tcp_listener::impl::close_client()
{
    if (client_sock != INVALID_SOCKET) {
        if (on_disconnect) {
            on_disconnect();
        }
        cleanup_socket(client_sock);
    }
}

void tcp_listener::impl::run()
{
    // Try ports in range
    uint16_t p;
    for (p = cfg.port_start; p <= cfg.port_end; ++p) {
        if (try_bind(p)) {
            break;
        }
    }

    if (p > cfg.port_end) {
        port.store(0, std::memory_order_release);
        set_state(listener_state::failed);
        // Retry loop
        while (running.load()) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(cfg.retry_interval_ms));
            for (p = cfg.port_start; p <= cfg.port_end; ++p) {
                if (try_bind(p)) {
                    goto listening;
                }
            }
        }
        return;
    }

listening:
    set_state(listener_state::listening);

    fd_set read_fds;
    timeval tv{};

    while (running.load()) {
        if (on_tick) {
            on_tick();
        }
        FD_ZERO(&read_fds);
        int max_fd = 0;

        if (listen_sock != INVALID_SOCKET) {
            FD_SET(listen_sock, &read_fds);
            max_fd = static_cast<int>(listen_sock);
        }
        if (client_sock != INVALID_SOCKET) {
            FD_SET(client_sock, &read_fds);
            if (client_sock > static_cast<SOCKET>(max_fd)) {
                max_fd = static_cast<int>(client_sock);
            }
        }

        tv.tv_sec = 0;
        tv.tv_usec = static_cast<long>(cfg.recv_timeout_ms) * 1000;

        int ret = select(max_fd + 1, &read_fds, nullptr, nullptr, &tv);
        if (ret == SOCKET_ERROR || !running.load()) {
            break;
        }

        if (ret == 0) {
            continue;
        }

        // New connection
        if (listen_sock != INVALID_SOCKET &&
            FD_ISSET(listen_sock, &read_fds)) {
            sockaddr_in client_addr{};
            int addr_len = sizeof(client_addr);
            SOCKET new_client = accept(listen_sock,
                reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
            if (new_client != INVALID_SOCKET) {
                close_client();
                client_sock = new_client;
                u_long nonblock = 1;
                ioctlsocket(client_sock, FIONBIO, &nonblock);
                set_state(listener_state::connected);
                if (on_accept) {
                    on_accept();
                }
            }
        }

        // Data from client
        if (client_sock != INVALID_SOCKET &&
            FD_ISSET(client_sock, &read_fds)) {
            char buf[4096];
            int n = recv(client_sock, buf, sizeof(buf), 0);
            if (n > 0) {
                if (on_data) {
                    on_data(buf, n);
                }
            } else if (n == 0 || WSAGetLastError() != WSAEWOULDBLOCK) {
                close_client();
                set_state(listener_state::listening);
            }
        }
    }

    cleanup_socket(listen_sock);
    close_client();
    set_state(listener_state::stopped);
}

} // namespace xray
