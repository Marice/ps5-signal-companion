// Signal Companion - The suite's three payloads: are they running, which version, start and
// restart. SPDX-License-Identifier: GPL-3.0-or-later

#include "signal/suite.hpp"

#include "core/save_file.hpp"
#include "pldmgr/pldmgr.hpp"
#include "pldmgr/repo_status.hpp"
#include "pldmgr/suite.hpp"
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
constexpr int kPldmgrPort = 8084;
// pldmgr is asked about the catalogue every this many polls (and after each start).
constexpr int kCatalogueEvery = 10;

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

// The catalogue's id for a payload: the suite's ids are in the same order as Payload.
const char *catalogue_id(Payload payload)
{
    return pldmgr::kSuiteIds[index(payload)];
}

const pldmgr::RepoPayload *catalogue_payload(const pldmgr::RepoSource &suite, Payload payload)
{
    for (const pldmgr::RepoPayload &p : suite.payloads)
        if (p.key() == catalogue_id(payload))
            return &p;
    return nullptr;
}

// pldmgr's copy of a suite payload: from the catalogue first, else from another repository.
const pldmgr::Payload *installed_payload(const pldmgr::PayloadList &list, Payload payload)
{
    const pldmgr::Payload *any = nullptr;
    for (const pldmgr::Payload &p : list.items)
    {
        if (p.kind != "repo" || p.id != catalogue_id(payload))
            continue;
        if (pldmgr::is_suite_url(p.source_url))
            return &p;
        if (any == nullptr)
            any = &p;
    }
    return any;
}

// Adds the catalogue to pldmgr's repositories, and lets pldmgr use more than its official one
// (PayLoader's ensure_suite_source).
void ensure_catalogue(pldmgr::Pldmgr &api)
{
    std::vector<pldmgr::Source> sources;
    pldmgr::Config config;
    if (!api.sources(sources).ok || !api.config(config).ok)
        return;
    bool listed = false;
    for (const pldmgr::Source &source : sources)
        listed = listed || pldmgr::is_suite_url(source.url);
    if (!listed)
    {
        std::string name;
        api.add_source(pldmgr::kSuiteUrl, name);
    }
    if (!config.multi_sources)
    {
        pldmgr::ConfigChange change;
        change.multi_sources = true;
        api.save_config(change);
    }
}

} // namespace

std::string plain_version(std::string_view version)
{
    if (!version.empty() && (version.front() == 'v' || version.front() == 'V'))
        version.remove_prefix(1);
    return std::string(version);
}

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

std::string power_request(bool on)
{
    return on ? "{\"power_actions\":true}" : "{\"power_actions\":false}";
}

Suite::Suite(TransportFactory factory, PldmgrFactory pldmgr)
    : factory_(std::move(factory)), pldmgr_(std::move(pldmgr))
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
    catalogue_shown_ = catalogue_state_;
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
    if (polls_++ % kCatalogueEvery == 0)
        poll_catalogue();
    for (std::size_t i = 0; i < kPayloadCount; ++i)
        poll(static_cast<Payload>(i));
}

void Suite::poll_catalogue()
{
    if (!pldmgr_)
        return;
    pldmgr::Pldmgr api(pldmgr_());
    CatalogueState catalogue;
    catalogue.checked = true;
    std::string version;
    bool supported = false;
    std::array<std::string, kPayloadCount> listed{}, installed{};
    if (api.version(version, supported).ok && supported)
    {
        catalogue.pldmgr = true;
        if (!source_checked_)
        {
            ensure_catalogue(api);
            source_checked_ = true;
        }
        std::vector<pldmgr::RepoSource> sources;
        pldmgr::PayloadList have;
        const pldmgr::RepoSource *suite =
            api.repository(false, sources).ok ? pldmgr::suite_source(sources) : nullptr;
        const bool have_list = api.payloads(have).ok;
        catalogue.listed = suite != nullptr;
        for (std::size_t i = 0; i < kPayloadCount; ++i)
        {
            const Payload payload = static_cast<Payload>(i);
            const pldmgr::RepoPayload *p = suite ? catalogue_payload(*suite, payload) : nullptr;
            const pldmgr::Payload *copy = have_list ? installed_payload(have, payload) : nullptr;
            listed[i] = p ? plain_version(p->version) : std::string();
            installed[i] = copy ? plain_version(copy->version) : std::string();
        }
    }
    else
    {
        source_checked_ = false; // a pldmgr that comes back may have lost the source
    }
    std::lock_guard<std::mutex> lock(mutex_);
    catalogue_state_ = catalogue;
    for (std::size_t i = 0; i < kPayloadCount; ++i)
    {
        states_[i].catalogue_version = listed[i];
        states_[i].pldmgr_version = installed[i];
    }
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
            broker.config.power_supported = root["power_actions"].is_bool();
            broker.config.power_actions =
                broker.config.power_supported && root["power_actions"].as_bool();
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
            std::string fallback;
            const std::unique_ptr<Transport> transport = factory_(info.port);
            if (launch_from_catalogue(payload, restart, *transport, message, failed, fallback))
            {
                // pldmgr installed and started the catalogue's copy (or said why not).
            }
            else if (data == nullptr)
            {
                message = std::string("This build does not include ") + info.file.name;
                failed = true;
            }
            else
            {
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
                    if (up && !fallback.empty())
                        message += " (the app's copy: " + fallback + ")";
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
            poll_catalogue();
            poll(payload);
        });
}

bool Suite::launch_from_catalogue(Payload payload, bool restart, Transport &transport,
                                  std::string &message, bool &failed, std::string &fallback)
{
    if (!pldmgr_)
        return false;
    pldmgr::Pldmgr api(pldmgr_());
    std::string version;
    bool supported = false;
    if (!api.version(version, supported).ok || !supported)
        return false; // no pldmgr: the app's copy, as before
    std::vector<pldmgr::RepoSource> sources;
    pldmgr::PayloadList have;
    const pldmgr::RepoSource *suite =
        api.repository(false, sources).ok ? pldmgr::suite_source(sources) : nullptr;
    const pldmgr::RepoPayload *wanted = suite ? catalogue_payload(*suite, payload) : nullptr;
    if (wanted == nullptr || !api.payloads(have).ok)
    {
        fallback = "pldmgr does not list the Signal suite";
        return false;
    }
    const PayloadInfo &info = payload_info(payload);
    const pldmgr::RepoStatus status = pldmgr::repo_status(*wanted, suite->url, have);
    std::string slot = status.installed ? status.installed->slot : std::string();
    if (status.kind != pldmgr::RepoStatus::Kind::installed)
    {
        pldmgr::InstallResult result;
        const pldmgr::Outcome done =
            api.install(*wanted, suite->url, pldmgr::InstallMode::normal, "", result);
        if (done.ok && result.conflict)
        {
            // pldmgr's own question; PayLoader can answer it. Nothing is replaced from here.
            message = std::string(info.title) + " is installed in pldmgr from another repository";
            failed = true;
            return true;
        }
        if (!done.ok || result.slot.empty())
        {
            fallback = "pldmgr could not install " + plain_version(wanted->version) +
                       (done.error.empty() ? std::string() : ": " + done.error);
            return false;
        }
        slot = result.slot;
    }
    if (restart)
    {
        transport.send(info.quit_method, info.quit_path, "");
        for (int i = 0; i < 25 && transport.send(info.status_method, info.status_path, "").status > 0;
             ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    const pldmgr::Outcome started = api.launch(slot);
    if (!started.ok)
    {
        message = std::string(info.title) + " did not start through pldmgr" +
                  (started.error.empty() ? std::string() : ": " + started.error);
        failed = true;
        return true;
    }
    bool up = false;
    for (int i = 0; i < 40 && !up; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        up = transport.send(info.status_method, info.status_path, "").status > 0;
    }
    message = up ? std::string(info.title) + " " + plain_version(wanted->version) +
                       " runs, from the Signal suite catalogue"
                 : std::string(info.title) + " did not answer";
    failed = !up;
    return true;
}

void Suite::save_broker(const BrokerConfig &config, const std::string &password)
{
    put_config(broker_request(config, password));
}

void Suite::save_power_actions(bool on)
{
    put_config(power_request(on));
}

void Suite::put_config(std::string body)
{
    queue(
        [this, body = std::move(body)]
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
        slot = std::make_unique<Suite>([](int port) { return make_http_transport(port); },
                                       [] { return pldmgr::make_loopback_transport(kPldmgrPort); });
    return *slot;
}

void install_suite(Suite::TransportFactory factory, Suite::PldmgrFactory pldmgr)
{
    suite_slot() = std::make_unique<Suite>(std::move(factory), std::move(pldmgr));
}

} // namespace hui::signal
