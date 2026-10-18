// Signal Companion - The suite's three payloads: are they running, which version, start and
// restart. SPDX-License-Identifier: GPL-3.0-or-later

#include "signal/suite.hpp"

#include "core/save_file.hpp"
#include "signal/json.hpp"
#include "signal/version.hpp"

#include <chrono>
#include <thread>

namespace hui::signal
{

namespace
{

constexpr std::size_t kMaxPayload = 4u << 20;
constexpr float kPollSeconds = 3.0f;

const PayloadInfo kInfo[kPayloadCount] = {
    {"ps5-exporter", "Console statistics for Grafana and Home Assistant", kExporterPayload, 9100,
     "GET", "/", "GET", "/quit"},
    {"ps5-homeassistant", "The PS5 as a device in Home Assistant", kHomeAssistantPayload, 9160,
     "GET", "/status", "POST", "/quit"},
    {"ps5-signal", "Beeps and the light, for you and homebrew", kSignalPayload, 9150, "GET",
     "/status", "POST", "/quit"},
};

std::size_t index(Payload payload)
{
    return static_cast<std::size_t>(payload);
}

// A JSON string, quotes and backslashes escaped.
std::string quoted(const std::string &text)
{
    std::string out = "\"";
    for (const char c : text)
    {
        if (c == '"' || c == '\\')
            out += '\\';
        if (static_cast<unsigned char>(c) >= 32)
            out += c;
    }
    return out + "\"";
}

} // namespace

const PayloadInfo &payload_info(Payload payload)
{
    return kInfo[index(payload)];
}

std::string reported_version(Payload payload, const std::string &body)
{
    if (payload == Payload::exporter)
    {
        // "ps5-exporter 0.3.0\n..."
        constexpr std::string_view kPrefix = "ps5-exporter ";
        if (body.compare(0, kPrefix.size(), kPrefix) != 0)
            return {};
        const std::size_t end = body.find_first_of(" \r\n", kPrefix.size());
        return body.substr(kPrefix.size(),
                           end == std::string::npos ? std::string::npos : end - kPrefix.size());
    }
    JsonValue root;
    if (!parse_json(body, root) || !root.is_object())
        return {};
    return root["version"].is_string() ? root["version"].as_string() : std::string();
}

std::string broker_request(const BrokerConfig &config, const std::string &password)
{
    std::string body = "{\"host\":" + quoted(config.host) +
                       ",\"port\":" + std::to_string(config.port) +
                       ",\"username\":" + quoted(config.username) +
                       ",\"interval\":" + std::to_string(config.interval);
    if (!password.empty())
        body += ",\"password\":" + quoted(password);
    return body + "}";
}

Suite::Suite(TransportFactory factory) : factory_(std::move(factory))
{
    started_ = pthread_create(&thread_, nullptr, &Suite::thread_main, this) == 0;
}

Suite::~Suite()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    if (started_)
        pthread_join(thread_, nullptr);
}

void *Suite::thread_main(void *self)
{
    static_cast<Suite *>(self)->run();
    return nullptr;
}

void Suite::run()
{
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;)
    {
        wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
        if (stopping_)
            return;
        std::function<void()> job = std::move(jobs_.front());
        jobs_.pop_front();
        working_ = true;
        lock.unlock();
        job();
        lock.lock();
        working_ = false;
        if (jobs_.empty())
            idle_.notify_all();
    }
}

void Suite::queue(std::function<void()> job)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_.push_back(std::move(job));
    }
    wake_.notify_one();
}

void Suite::wait_idle()
{
    std::unique_lock<std::mutex> lock(mutex_);
    idle_.wait(lock, [this] { return jobs_.empty() && !working_; });
}

void Suite::update(float dt)
{
    since_poll_ += dt;
    if (since_poll_ >= kPollSeconds)
        refresh();
    std::lock_guard<std::mutex> lock(mutex_);
    shown_ = states_;
    broker_shown_ = broker_state_;
}

void Suite::refresh()
{
    since_poll_ = 0.0f;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (jobs_.size() > 2)
            return; // still busy with earlier ones
    }
    queue([this] { poll_all(); });
}

void Suite::poll_all()
{
    for (std::size_t i = 0; i < kPayloadCount; ++i)
        poll(static_cast<Payload>(i));
}

void Suite::poll(Payload payload)
{
    const PayloadInfo &info = payload_info(payload);
    const std::unique_ptr<Transport> transport = factory_(info.port);
    const Reply reply = transport->send(info.status_method, info.status_path, "");
    const bool running = reply.status > 0;
    const std::string version = running ? reported_version(payload, reply.body) : std::string();
    BrokerState broker;
    bool have_broker = false;
    if (payload == Payload::homeassistant && running)
    {
        JsonValue status;
        if (parse_json(reply.body, status) && status.is_object())
        {
            broker.state = status["state"].is_string() ? status["state"].as_string() : "";
            broker.error = status["error"].is_string() ? status["error"].as_string() : "";
        }
        const Reply config = transport->send("GET", "/config", "");
        JsonValue root;
        if (config.status == 200 && parse_json(config.body, root) && root.is_object())
        {
            broker.known = true;
            broker.config.host = root["host"].is_string() ? root["host"].as_string() : "";
            broker.config.port =
                root["port"].is_number() ? static_cast<int>(root["port"].as_number()) : 1883;
            broker.config.username =
                root["username"].is_string() ? root["username"].as_string() : "";
            broker.config.password_set =
                root["password_set"].is_bool() && root["password_set"].as_bool();
            broker.config.interval =
                root["interval"].is_number() ? static_cast<int>(root["interval"].as_number()) : 10;
        }
        have_broker = true;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    PayloadState &state = states_[index(payload)];
    state.checked = true;
    state.running = running;
    state.version = version;
    if (have_broker)
    {
        broker.saved = broker_state_.saved;
        broker.save_error = broker_state_.save_error;
        broker_state_ = broker;
    }
    else if (payload == Payload::homeassistant)
    {
        broker_state_.known = false;
    }
}

const std::string *Suite::elf(Payload payload) const
{
    const std::size_t i = index(payload);
    if (elf_found_[i] < 0)
    {
        std::string data;
        const bool found =
            save::read_file(assets_path() + "/payload/" + payload_info(payload).file.name, &data,
                            kMaxPayload) &&
            data.size() > 64 &&
            data.compare(0, 4,
                         "\x7f"
                         "ELF") == 0;
        elf_found_[i] = found ? 1 : 0;
        if (found)
            elves_[i] = std::move(data);
    }
    return elf_found_[i] == 1 ? &elves_[i] : nullptr;
}

bool Suite::bundled(Payload payload) const
{
    return elf(payload) != nullptr;
}

std::string Suite::bundled_version(Payload payload) const
{
    const std::string *data = elf(payload);
    return data ? payload_version(*data, payload_info(payload).file.marker) : std::string();
}

void Suite::start(Payload payload)
{
    launch(payload, false);
}

void Suite::restart(Payload payload)
{
    launch(payload, true);
}

void Suite::launch(Payload payload, bool restart)
{
    const std::string *data = elf(payload);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        PayloadState &state = states_[index(payload)];
        if (state.busy)
            return;
        state.busy = true;
        state.message.clear();
        state.failed = false;
    }
    queue(
        [this, payload, restart, data]
        {
            const PayloadInfo &info = payload_info(payload);
            std::string message;
            bool failed = false;
            if (data == nullptr)
            {
                message = std::string("This build does not include ") + info.file.name;
                failed = true;
            }
            else
            {
                const std::unique_ptr<Transport> transport = factory_(info.port);
                if (restart)
                {
                    transport->send(info.quit_method, info.quit_path, "");
                    // Gone within 5 seconds, or the port stays taken and the new one cannot start.
                    for (int i = 0;
                         i < 25 &&
                         transport->send(info.status_method, info.status_path, "").status > 0;
                         ++i)
                        std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
                if (transport->push(kLoaderPort, *data).status != 200)
                {
                    message = "The ELF loader on port 9021 did not take it";
                    failed = true;
                }
                else
                {
                    bool up = false;
                    for (int i = 0; i < 40 && !up; ++i)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(250));
                        up = transport->send(info.status_method, info.status_path, "").status > 0;
                    }
                    message = up ? std::string(info.title) + " runs"
                                 : std::string(info.title) + " did not answer";
                    failed = !up;
                }
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                PayloadState &state = states_[index(payload)];
                state.busy = false;
                state.message = message;
                state.failed = failed;
                state.serial++;
            }
            poll(payload);
        });
}

void Suite::save_broker(const BrokerConfig &config, const std::string &password)
{
    const std::string body = broker_request(config, password);
    queue(
        [this, body]
        {
            const std::unique_ptr<Transport> transport =
                factory_(payload_info(Payload::homeassistant).port);
            const Reply reply = transport->send("PUT", "/config", body);
            std::string error;
            if (reply.status != 200)
            {
                JsonValue root;
                error =
                    reply.status < 0 ? "ps5-homeassistant does not answer"
                    : parse_json(reply.body, root) && root.is_object() && root["error"].is_string()
                        ? root["error"].as_string()
                        : "Not saved";
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                broker_state_.saved++;
                broker_state_.save_error = error;
            }
            poll(Payload::homeassistant);
        });
}

namespace
{

std::unique_ptr<Suite> &suite_slot()
{
    static std::unique_ptr<Suite> slot;
    return slot;
}

} // namespace

Suite &suite()
{
    std::unique_ptr<Suite> &slot = suite_slot();
    if (!slot)
        slot = std::make_unique<Suite>([](int port) { return make_http_transport(port); });
    return *slot;
}

void install_suite(Suite::TransportFactory factory)
{
    suite_slot() = std::make_unique<Suite>(std::move(factory));
}

} // namespace hui::signal
