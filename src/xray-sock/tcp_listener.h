#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace xray {

struct sock_config {
    uint16_t port_start = 9000;
    uint16_t port_end = 9009;
    uint32_t recv_timeout_ms = 100;
    uint32_t retry_interval_ms = 30000;
};

enum class listener_state {
    stopped,
    listening,
    connected,
    failed
};

using on_accept_fn = std::function<void()>;
using on_disconnect_fn = std::function<void()>;
using on_data_fn = std::function<void(const char* data, int len)>;
using on_tick_fn = std::function<void()>;

class tcp_listener {
public:
    tcp_listener();
    ~tcp_listener();

    tcp_listener(const tcp_listener&) = delete;
    tcp_listener& operator=(const tcp_listener&) = delete;

    bool start(const sock_config& cfg);
    void stop();
    bool is_running() const;
    uint16_t bound_port() const;
    listener_state state() const;

    void set_on_accept(on_accept_fn fn);
    void set_on_disconnect(on_disconnect_fn fn);
    void set_on_data(on_data_fn fn);
    void set_on_tick(on_tick_fn fn);
    bool send(const char* data, int len);

private:
    struct impl;
    impl* self_;
};

} // namespace xray
