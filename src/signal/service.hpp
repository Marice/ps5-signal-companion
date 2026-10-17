// Signal Companion - Talking to the ps5-signal service without blocking a frame.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Requests go to http://127.0.0.1:9150 on a worker thread. A screen queues a
// call and reads its outcome in a later frame:
//
//   const unsigned before = link.service().serial(signal::Call::beep);
//   link.service().send(signal::Call::beep, signal::beep_request(steps));
//   ...
//   if (link.service().serial(signal::Call::beep) != before)   // it answered
//       show(link.service().outcome(signal::Call::beep).reply);
//
// Link keeps what every screen shares: whether the service runs, its last
// status and its config.json.

#pragma once

#include "signal/model.hpp"

#include <array>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <pthread.h>
#include <string>
#include <string_view>

namespace hui::signal
{

constexpr int kPort = 9150;
constexpr int kLoaderPort = 9021; // etaHEN's ELF loader

// One HTTP answer. status -1: no answer, the service does not run.
struct Reply
{
    int status = -1;
    std::string body;
};

class Transport
{
  public:
    virtual ~Transport() = default;
    // Blocks until the answer, a refusal or a time-out. Called on the worker.
    virtual Reply send(std::string_view method, std::string_view path, std::string_view body) = 0;
    // Sends raw bytes to a loopback port and closes: how an ELF loader takes a
    // payload. status 200 when every byte went out, -1 otherwise.
    virtual Reply push(int port, std::string_view data) = 0;
};

// HTTP/1.1 over a loopback socket to the service's port.
std::unique_ptr<Transport> make_http_transport(int port = kPort);

enum class Call : unsigned char
{
    status,      // GET /status
    config,      // GET /config
    save_config, // PUT /config
    color,       // POST /color
    beep,        // POST /beep
    led,         // POST /led
    stop,        // POST /stop
    launch,      // ps5-signal.elf to the ELF loader (kLoaderPort)
    quit,        // POST /quit
    beeper,      // POST /beeper
    count,
};

struct Outcome
{
    unsigned serial = 0; // how many calls of this kind have answered
    Reply reply;
};

class Service
{
  public:
    explicit Service(std::unique_ptr<Transport> transport);
    ~Service();
    Service(const Service &) = delete;
    Service &operator=(const Service &) = delete;

    // Queues a call. A call of the same kind still waiting is replaced, so a
    // slider that moves every frame sends only its latest value.
    void send(Call call, std::string body = {});
    unsigned serial(Call call) const;
    Outcome outcome(Call call) const;
    // Queued or being sent.
    bool pending(Call call) const;
    // Blocks until nothing is queued or being sent (tests and shutdown).
    void wait_idle();

  private:
    static void *thread_main(void *self);
    void run();

    std::unique_ptr<Transport> transport_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::deque<Call> order_;
    std::array<std::optional<std::string>, static_cast<std::size_t>(Call::count)> queued_;
    std::array<Outcome, static_cast<std::size_t>(Call::count)> outcomes_;
    std::optional<Call> sending_;
    bool stopping_ = false;
    bool started_ = false;
    pthread_t thread_{};
};

enum class LinkState : unsigned char
{
    checking, // the first answer has not come yet
    online,
    offline,
};

enum class StartState : unsigned char
{
    idle,
    stopping, // the running service was asked to quit, for a restart
    sending,  // the ELF goes to the loader
    waiting,  // sent; waiting for the service to answer
    failed,   // start_error() says why
};

class Link
{
  public:
    explicit Link(std::unique_ptr<Transport> transport);

    // Reads finished calls and asks for the status again now and then. Every
    // screen calls it each frame; calling it twice in a frame is harmless.
    void update(float dt);
    // Asks for the status (and the config, when it is not known) now.
    void refresh();

    LinkState state() const
    {
        return state_;
    }
    bool online() const
    {
        return state_ == LinkState::online;
    }
    const Status &status() const
    {
        return status_;
    }
    // config.json as the service has it; false until it arrived.
    bool has_config() const
    {
        return has_config_;
    }
    const Config &config() const
    {
        return config_;
    }
    // Why the service's config.json could not be read, when it could not.
    const std::string &config_error() const
    {
        return config_error_;
    }
    // Counts up whenever a new config arrived, so screens reload their copy.
    unsigned config_serial() const
    {
        return config_serial_;
    }
    // Seconds since the last status answer or refusal.
    float age() const
    {
        return age_;
    }

    Service &service()
    {
        return service_;
    }

    // ---- starting the service from the app ----
    // True when this build carries ps5-signal.elf (assets/payload/).
    bool can_start() const;
    // Sends it to the ELF loader, then waits for the service to answer.
    void start_service();
    // Stops the running service (POST /quit) and starts the one in this app.
    void restart_service();
    // Sending, waiting or stopping for a restart.
    bool starting() const
    {
        return start_state_ == StartState::stopping || start_state_ == StartState::sending ||
               start_state_ == StartState::waiting;
    }
    StartState start_state() const
    {
        return start_state_;
    }
    const std::string &start_error() const
    {
        return start_error_;
    }

  private:
    void update_start(float dt);

    Service service_;
    LinkState state_ = LinkState::checking;
    Status status_;
    Config config_;
    std::string config_error_;
    bool has_config_ = false;
    unsigned config_serial_ = 0;
    unsigned status_seen_ = 0;
    unsigned config_seen_ = 0;
    unsigned saved_seen_ = 0;
    float age_ = 0.0f;
    float since_ask_ = 0.0f;
    bool started_ = false;
    StartState start_state_ = StartState::idle;
    std::string start_error_;
    unsigned launch_seen_ = 0;
    unsigned quit_seen_ = 0;
    float start_age_ = 0.0f;
    mutable int payload_ = -1; // -1 not looked for yet, 0 missing, 1 present
    mutable std::string elf_;
};

// Where the app's files are: its read-only assets and a writable folder.
// main() sets them; the defaults suit the console.
void set_paths(std::string assets, std::string data);
const std::string &assets_path();
const std::string &data_path();

// The app's link to the service, made on first use.
Link &link();
// Replaces it, for tests: a fake transport instead of the network.
void install_link(std::unique_ptr<Transport> transport);

} // namespace hui::signal
