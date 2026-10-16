// Signal Companion - Test double: a ps5-signal service in memory instead of on the network.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "signal/model.hpp"
#include "signal/service.hpp"

#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace hui::testing
{

// Answers like the service: the status, a config.json it keeps, 202 for
// sequences. Records every request as "METHOD /path body". Called on the
// Service's worker thread, hence the lock.
class FakeSignal final : public signal::Transport
{
  public:
    FakeSignal() : config_(signal::config_json(signal::default_config()))
    {
    }

    static std::string status_json(bool hold, const signal::Mix &held)
    {
        char held_json[96] = "null";
        if (hold)
            std::snprintf(held_json, sizeof(held_json), R"({"blue":%d,"white":%d,"amber":%d})",
                          held.blue, held.white, held.amber);
        char text[512];
        std::snprintf(text, sizeof(text),
                      R"({"service":"ps5-signal","version":"0.1.0",)"
                      R"("beep":{"playing":false,"muted":false,"volume":2},)"
                      R"("led":{"playing":false,"available":true,"level":1,)"
                      R"("color":{"available":true,"blue":%d,"white":%d,"amber":%d,"held":%s}}})",
                      hold ? held.blue : 255, hold ? held.white : 0, hold ? held.amber : 0,
                      held_json);
        return text;
    }

    signal::Reply send(std::string_view method, std::string_view path,
                       std::string_view body) override
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (hold_)
        {
            held_ = true;
            changed_.notify_all();
            changed_.wait(lock, [this] { return !hold_; });
        }
        requests_.push_back(std::string(method) + " " + std::string(path) + " " +
                            std::string(body));
        if (!running_)
            return {};
        if (path == "/status")
        {
            signal::Config config;
            std::string error;
            signal::parse_config(config_, config, error);
            return {200, status_json(config.hold, config.held)};
        }
        if (path == "/config" && method == "GET")
            return {200, config_};
        if (path == "/config")
        {
            signal::Config config;
            std::string error;
            if (!signal::parse_config(body, config, error))
                return {400, "{\"error\":\"" + error + "\"}"};
            config_ = std::string(body);
            saved_ = config_;
            return {200, R"({"saved":true})"};
        }
        if (path == "/beep" || path == "/led")
            return {202, R"({"playing":true})"};
        return {200, R"({"ok":true})"};
    }

    signal::Reply push(int port, std::string_view data) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        requests_.push_back("PUSH " + std::to_string(port) + " " + std::to_string(data.size()));
        if (!loader_)
            return {};
        running_ = start_runs_;
        return {200, {}};
    }

    // The ELF loader answers; a pushed payload starts the service when runs.
    void set_loader(bool loader, bool runs)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loader_ = loader;
        start_runs_ = runs;
    }

    void set_running(bool running)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = running;
    }
    void hold_requests(bool hold)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        hold_ = hold;
        changed_.notify_all();
    }
    void wait_held()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait(lock, [this] { return held_; });
    }
    std::vector<std::string> requests() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return requests_;
    }
    std::string saved() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return saved_;
    }

  private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<std::string> requests_;
    std::string config_;
    std::string saved_;
    bool running_ = true;
    bool hold_ = false;
    bool loader_ = true;
    bool start_runs_ = true;
    bool held_ = false;
};

} // namespace hui::testing
