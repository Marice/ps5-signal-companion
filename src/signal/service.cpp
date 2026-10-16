// Signal Companion - Talking to the ps5-signal service without blocking a frame.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "signal/service.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace hui::signal
{

namespace
{

constexpr int kTimeoutMs = 1500;
constexpr std::size_t kMaxAnswer = 16 * 1024; // the service's answers are a few hundred bytes
constexpr float kAskOnline = 2.0f;            // seconds between status checks
constexpr float kAskOffline = 3.0f;

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

struct Endpoint
{
    const char *method;
    const char *path;
};

constexpr Endpoint kEndpoints[] = {
    {"GET", "/status"}, {"GET", "/config"}, {"PUT", "/config"}, {"POST", "/color"},
    {"POST", "/beep"},  {"POST", "/led"},   {"POST", "/stop"},
};
static_assert(std::size(kEndpoints) == static_cast<std::size_t>(Call::count));

std::size_t index(Call call)
{
    return static_cast<std::size_t>(call);
}

class HttpTransport final : public Transport
{
  public:
    explicit HttpTransport(int port) : port_(port)
    {
    }

    Reply send(std::string_view method, std::string_view path, std::string_view body) override
    {
        Reply reply;
        const int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
            return reply;
        timeval timeout{};
        timeout.tv_sec = kTimeoutMs / 1000;
        timeout.tv_usec = (kTimeoutMs % 1000) * 1000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
        const int on = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<std::uint16_t>(port_));
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0)
        {
            close(fd);
            return reply;
        }

        std::string request;
        request.reserve(160 + body.size());
        request.append(method).append(" ").append(path).append(" HTTP/1.1\r\n");
        request += "Host: 127.0.0.1\r\nConnection: close\r\n";
        if (!body.empty() || method != "GET")
        {
            char length[64];
            std::snprintf(length, sizeof(length),
                          "Content-Type: application/json\r\nContent-Length: %zu\r\n", body.size());
            request += length;
        }
        request += "\r\n";
        request.append(body);

        std::size_t sent = 0;
        while (sent < request.size())
        {
            const ssize_t n = ::send(fd, request.data() + sent, request.size() - sent, kSendFlags);
            if (n <= 0)
            {
                close(fd);
                return reply;
            }
            sent += static_cast<std::size_t>(n);
        }

        std::string answer;
        char buffer[2048];
        while (answer.size() < kMaxAnswer)
        {
            const ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
            if (n <= 0)
                break;
            answer.append(buffer, static_cast<std::size_t>(n));
        }
        close(fd);

        // "HTTP/1.1 200 OK\r\n...\r\n\r\n{body}"; the service closes after one answer.
        int status = 0;
        if (std::sscanf(answer.c_str(), "HTTP/1.%*d %d", &status) != 1 || status < 100)
            return reply;
        const std::size_t head = answer.find("\r\n\r\n");
        reply.status = status;
        if (head != std::string::npos)
            reply.body = answer.substr(head + 4);
        return reply;
    }

  private:
    int port_;
};

} // namespace

std::unique_ptr<Transport> make_http_transport(int port)
{
    return std::make_unique<HttpTransport>(port);
}

// ---- Service ------------------------------------------------------------------

Service::Service(std::unique_ptr<Transport> transport) : transport_(std::move(transport))
{
    // Never name threads: pthread_setname_np hangs on the console.
    started_ = pthread_create(&thread_, nullptr, &Service::thread_main, this) == 0;
}

Service::~Service()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    if (started_)
        pthread_join(thread_, nullptr);
}

void Service::send(Call call, std::string body)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::optional<std::string> &slot = queued_[index(call)];
        if (!slot.has_value())
            order_.push_back(call);
        slot = std::move(body);
    }
    wake_.notify_one();
}

unsigned Service::serial(Call call) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return outcomes_[index(call)].serial;
}

Outcome Service::outcome(Call call) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return outcomes_[index(call)];
}

bool Service::pending(Call call) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queued_[index(call)].has_value() || sending_ == call;
}

void Service::wait_idle()
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (!started_)
        return;
    idle_.wait(lock, [this] { return order_.empty() && !sending_.has_value(); });
}

void *Service::thread_main(void *self)
{
    static_cast<Service *>(self)->run();
    return nullptr;
}

void Service::run()
{
    std::unique_lock<std::mutex> lock(mutex_);
    while (true)
    {
        wake_.wait(lock, [this] { return stopping_ || !order_.empty(); });
        if (stopping_)
            break;
        const Call call = order_.front();
        order_.pop_front();
        std::string body = std::move(*queued_[index(call)]);
        queued_[index(call)].reset();
        sending_ = call;
        lock.unlock();

        const Endpoint &endpoint = kEndpoints[index(call)];
        Reply reply = transport_->send(endpoint.method, endpoint.path, body);

        lock.lock();
        Outcome &outcome = outcomes_[index(call)];
        outcome.reply = std::move(reply);
        ++outcome.serial;
        sending_.reset();
        if (order_.empty())
            idle_.notify_all();
    }
    sending_.reset();
    idle_.notify_all();
}

// ---- Link ---------------------------------------------------------------------

Link::Link(std::unique_ptr<Transport> transport) : service_(std::move(transport))
{
}

void Link::refresh()
{
    since_ask_ = 0.0f;
    started_ = true;
    if (!service_.pending(Call::status))
        service_.send(Call::status);
    if (!has_config_ && !service_.pending(Call::config))
        service_.send(Call::config);
}

void Link::update(float dt)
{
    if (!started_)
        refresh();
    age_ += dt;
    since_ask_ += dt;

    const Outcome status = service_.outcome(Call::status);
    if (status.serial != status_seen_)
    {
        status_seen_ = status.serial;
        age_ = 0.0f;
        Status parsed;
        const bool was_online = online();
        if (status.reply.status == 200 && parse_status(status.reply.body, parsed))
        {
            status_ = std::move(parsed);
            state_ = LinkState::online;
            // Back after a restart of the service: its config may have changed.
            if (!was_online && has_config_ && !service_.pending(Call::config))
                service_.send(Call::config);
        }
        else
        {
            state_ = LinkState::offline;
        }
    }

    const Outcome config = service_.outcome(Call::config);
    if (config.serial != config_seen_)
    {
        config_seen_ = config.serial;
        if (config.reply.status == 200)
        {
            Config parsed;
            std::string error;
            if (parse_config(config.reply.body, parsed, error))
            {
                config_ = std::move(parsed);
                config_error_.clear();
                has_config_ = true;
                ++config_serial_;
            }
            else
            {
                config_error_ = error;
            }
        }
        else if (config.reply.status > 0)
        {
            config_error_ = error_text(config.reply.body);
        }
    }

    // A saved config is what the service holds now: read it back.
    const unsigned saved = service_.serial(Call::save_config);
    if (saved != saved_seen_)
    {
        saved_seen_ = saved;
        service_.send(Call::config);
        service_.send(Call::status);
    }

    if (since_ask_ >= (online() ? kAskOnline : kAskOffline))
        refresh();
}

namespace
{

std::unique_ptr<Link> &instance()
{
    static std::unique_ptr<Link> kLink;
    return kLink;
}

} // namespace

Link &link()
{
    std::unique_ptr<Link> &current = instance();
    if (!current)
        current = std::make_unique<Link>(make_http_transport());
    return *current;
}

void install_link(std::unique_ptr<Transport> transport)
{
    instance().reset();
    instance() = std::make_unique<Link>(std::move(transport));
}

} // namespace hui::signal
