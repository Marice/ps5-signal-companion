// Signal Companion - The suite's three payloads: are they running, which version, start and
// restart. SPDX-License-Identifier: GPL-3.0-or-later
//
// ps5-exporter (port 9100), ps5-homeassistant (9160) and ps5-signal (9150) each
// answer on loopback. A worker thread asks each of them every few seconds and
// carries out a start (the app's ELF to the loader on 9021) or a restart (quit,
// wait until it is gone, then start). It also reads and saves
// ps5-homeassistant's broker settings. Screens read the results each frame.
//
//   signal::Suite &suite = signal::suite();
//   suite.update(dt);
//   const signal::PayloadState &state = suite.state(signal::Payload::exporter);
//   if (!state.running) suite.start(signal::Payload::exporter);

#pragma once

#include "signal/autoload.hpp"
#include "signal/service.hpp"

#include <array>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <string>

namespace hui::signal
{

enum class Payload : unsigned char
{
    exporter,
    homeassistant,
    signal,
    count,
};

constexpr std::size_t kPayloadCount = static_cast<std::size_t>(Payload::count);

struct PayloadInfo
{
    const char *title; // "ps5-exporter"
    const char *purpose;
    AutoloadPayload file;
    int port;
    const char *status_method;
    const char *status_path;
    const char *quit_method;
    const char *quit_path;
};

const PayloadInfo &payload_info(Payload payload);

struct PayloadState
{
    bool checked = false; // asked at least once
    bool running = false; // it answered
    std::string version;  // as it reports itself, "" when it does not say
    bool busy = false;    // a start or restart is under way
    std::string message;  // the last start's outcome ("" while none)
    bool failed = false;  // ... and whether it failed
    unsigned serial = 0;  // counts every finished start or restart
};

// ps5-homeassistant's broker settings and connection, as its API reports them.
struct BrokerConfig
{
    std::string host;
    int port = 1883;
    std::string username;
    bool password_set = false;
    int interval = 10;
};

struct BrokerState
{
    bool known = false; // the config arrived
    BrokerConfig config;
    std::string state; // "connected", "connecting", "disconnected", "not configured"
    std::string error;
    unsigned saved = 0; // counts every save that answered
    std::string save_error;
};

// The version in an answer: signal's and homeassistant's /status JSON, or
// ps5-exporter's "/" text ("ps5-exporter 0.3.0"). "" when it does not say.
std::string reported_version(Payload payload, const std::string &body);
// The body of a PUT /config for ps5-homeassistant; an empty password keeps the stored one.
std::string broker_request(const BrokerConfig &config, const std::string &password);

class Suite
{
  public:
    using TransportFactory = std::function<std::unique_ptr<Transport>(int port)>;
    explicit Suite(TransportFactory factory);
    ~Suite();
    Suite(const Suite &) = delete;
    Suite &operator=(const Suite &) = delete;

    // Asks again every few seconds; call each frame.
    void update(float dt);
    void refresh();

    const PayloadState &state(Payload payload) const
    {
        return shown_[static_cast<std::size_t>(payload)];
    }
    const BrokerState &broker() const
    {
        return broker_shown_;
    }

    // Whether this build carries the payload, and its version ("" when it has no marker).
    bool bundled(Payload payload) const;
    std::string bundled_version(Payload payload) const;

    void start(Payload payload);
    void restart(Payload payload);
    void save_broker(const BrokerConfig &config, const std::string &password);

    // Blocks until the worker has nothing to do (tests).
    void wait_idle();

  private:
    static void *thread_main(void *self);
    void run();
    void queue(std::function<void()> job);
    void poll_all();
    void poll(Payload payload);
    void launch(Payload payload, bool restart);
    const std::string *elf(Payload payload) const;

    TransportFactory factory_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::deque<std::function<void()>> jobs_;
    bool working_ = false;
    bool stopping_ = false;
    bool started_ = false;
    pthread_t thread_{};
    // Written by the worker under mutex_, copied for the screens in update().
    std::array<PayloadState, kPayloadCount> states_{};
    BrokerState broker_state_;
    std::array<PayloadState, kPayloadCount> shown_{};
    BrokerState broker_shown_;
    float since_poll_ = 1000.0f;
    mutable std::array<int, kPayloadCount> elf_found_{-1, -1, -1};
    mutable std::array<std::string, kPayloadCount> elves_;
};

// The app's suite, made on first use.
Suite &suite();
// Replaces it, for tests.
void install_suite(Suite::TransportFactory factory);

} // namespace hui::signal
