// Signal Companion - Screen "Payloads": the suite's three payloads and the Home Assistant broker.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ps5-exporter, ps5-homeassistant and ps5-signal: whether each runs and in which
// version, start or restart the one in this app, and keep it in ps5-autoloader's
// list (in the suite's order). Below them, ps5-homeassistant's broker: its
// address, the console's user and password, typed with the on-screen keyboard.

#include "concepts/concepts.hpp"

#include "concepts/companion/look.hpp"
#include "core/tween.hpp"
#include "signal/autoload.hpp"
#include "signal/suite.hpp"
#include "signal/version.hpp"
#include "ui/components/form.hpp"
#include "ui/components/input_prompt.hpp"
#include "ui/components/toast.hpp"
#include "ui/widgets.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace hui::concepts
{

namespace
{

using gfx::Color;
using gfx::Rect;
using namespace companion;

constexpr Rect kForm{kMargin, kTop, 860.0f, kBottom - kTop};
constexpr Rect kSide{1010.0f, kTop + 10.0f, kRight - 1010.0f, 600.0f};

// Row ids: per payload a block of three, then the broker's rows.
constexpr int kStatusRow = 1;
constexpr int kStartRow = 2;
constexpr int kAutoloadRow = 3;
constexpr int kBlock = 10;
enum BrokerId
{
    kConnection = 100,
    kHost,
    kPort,
    kUser,
    kPassword,
    kPower,
};

constexpr const char *kPowerHelp =
    "Buttons in Home Assistant; power off ends the jailbreak until you run the exploit again";

int row_id(signal::Payload payload, int row)
{
    return (static_cast<int>(payload) + 1) * kBlock + row;
}

constexpr std::array<signal::Payload, 3> kOrder = {
    signal::Payload::exporter, signal::Payload::homeassistant, signal::Payload::signal};

constexpr const char *kTechniques[] = {
    ("Asks each payload on its loopback port every 3 seconds: ps5-exporter 9100, ps5-homeassistant "
     "9160, "
     "ps5-signal 9150"),
    "Start or Restart sends the payload in this app to etaHEN's ELF loader on port 9021",
    "ps5-autoloader's list keeps the order ps5-exporter, ps5-homeassistant, ps5-signal, 3 seconds "
    "apart",
    "The broker settings go to ps5-homeassistant's API; the password is never shown again",
};

constexpr ui::Hint kHints[] = {
    {ui::Button::dpad, "Move"},
    {ui::Button::cross, "Choose"},
};

constexpr app::TourStep kTour[] = {
    {1.2f, 0, Direction::none, "payloads"},
    {0.3f, 0, Direction::down},
    {0.3f, 0, Direction::down},
    {0.6f, 0, Direction::down, "payloads-rows"},
};

enum class Editing
{
    none,
    host,
    port,
    user,
    password,
};

class PayloadsScreen final : public app::Concept
{
  public:
    explicit PayloadsScreen(app::Context &context) : context_(context)
    {
        build_form();
        toasts_.style.theme = theme();
        toasts_.style.anchor = ui::ToastAnchor::bottom_right;
        toasts_.style.margin = 120.0f;
        prompt_.style.theme = theme();
        prompt_.style.frosted = false;
        prompt_.keyboard.style.bindings = ui::KeyboardBindings::standard();
    }

    const app::ConceptInfo &info() const override
    {
        static const app::ConceptInfo kInfo{
            "payloads",
            "Payloads",
            "The suite's payloads: run, update and start them with the console; the Home Assistant "
            "broker",
            "src/concepts/payloads.cpp",
            audio::SoundSet::paper,
            Color::rgb(0x36d67e),
            kTechniques,
        };
        return kInfo;
    }

    void enter() override
    {
        age_ = 0.0f;
        form_.enter();
        signal::suite().refresh();
        if (!refreshed_)
        {
            refreshed_ = true;
            refresh_autoload();
        }
    }

    void update(const InputFrame &input, float dt, app::Feedback &feedback) override
    {
        clock_ += dt;
        age_ += dt;
        signal::link().update(dt);
        signal::Suite &suite = signal::suite();
        suite.update(dt);
        restyle();
        show(suite);
        if (prompt_.is_open())
        {
            const ui::Event event = prompt_.handle(input, feedback);
            if (event == ui::Event::activated)
                finish_edit(suite, prompt_.text());
            else if (event == ui::Event::cancelled)
                editing_ = Editing::none;
        }
        else
        {
            const ui::Event event = form_.handle(input, feedback);
            if (event == ui::Event::activated)
                act(form_.changed_id(), suite, feedback);
            else if (event == ui::Event::changed)
                changed(form_.changed_id());
        }
        form_.update(dt);
        prompt_.update(dt);
        toasts_.update(dt, feedback);
    }

    void draw(app::Frame &frame) const override
    {
        const ui::Theme &look = theme();
        begin_frame(frame, look);
        ui::Canvas canvas{frame.scene, context_.fonts, 0, clock_};
        ui::Painter paint(frame.scene, context_.fonts, look);
        frame.scene.push_opacity(tween::cubic_out(age_ / 0.3f));
        draw_header(paint, "Payloads",
                    "Run, update and start the suite's payloads with the console", signal::link(),
                    clock_);
        form_.draw(canvas);
        draw_side(paint);
        draw_hints(frame.scene, context_.fonts, look, kHints);
        frame.scene.pop_opacity();
        ui::Canvas above{frame.overlay, context_.fonts, 0, clock_};
        prompt_.draw(above);
        toasts_.draw(above);
    }

    std::span<const app::TourStep> tour() const override
    {
        return kTour;
    }

  private:
    void build_form()
    {
        // Only where ps5-autoloader's list can be seen (etaHEN allows /data in the sandbox).
        autoload_found_ = signal::autoload_state().found;
        for (const signal::Payload payload : kOrder)
        {
            const signal::PayloadInfo &info = signal::payload_info(payload);
            form_.add_header(info.title);
            form_.add_value(row_id(payload, kStatusRow), "Status", "Checking").description =
                info.purpose;
            form_.add_action(row_id(payload, kStartRow), "Start").description =
                "Sends the one in this app to etaHEN's ELF loader (port 9021)";
            if (autoload_found_)
            {
                form_
                    .add_toggle(
                        row_id(payload, kAutoloadRow), "Start with ps5-autoloader",
                        signal::autoload_state(signal::kAutoloadDirectory, info.file).listed)
                    .description =
                    "In the order ps5-exporter, ps5-homeassistant, ps5-signal, 3 s apart";
            }
        }
        form_.add_header("Home Assistant");
        form_.add_value(kConnection, "Connection", "-").description =
            "ps5-homeassistant's link to the MQTT broker";
        form_.add_action(kHost, "Broker").description =
            "The address of the MQTT broker, such as Home Assistant's";
        form_.add_action(kPort, "Port").description =
            "Mosquitto's port, 1883 unless you changed it";
        form_.add_action(kUser, "User").description =
            "A Home Assistant user for the console, such as ps5";
        form_.add_action(kPassword, "Password").description =
            "That user's password; it is not shown again";
        form_.add_toggle(kPower, "Rest mode and power off").description = kPowerHelp;
        for (const int id : {kHost, kPort, kUser, kPassword})
            form_.row(id)->chevron = true;
    }

    void restyle()
    {
        if (styled_ && reduced_ == context_.settings.reduced_motion)
            return;
        styled_ = true;
        reduced_ = context_.settings.reduced_motion;
        ui::FormStyle style;
        style.theme = theme();
        style.reduced_motion = reduced_;
        style.label_ratio = 0.34f;
        style.compact = true;
        form_.style = style;
        form_.set_bounds(kForm);
        toasts_.style.reduced_motion = reduced_;
        prompt_.style.reduced_motion = reduced_;
    }

    // The rows from the suite's latest answers, and a toast when a start finished.
    void show(const signal::Suite &suite)
    {
        for (const signal::Payload payload : kOrder)
        {
            const signal::PayloadState &state = suite.state(payload);
            const std::string bundled = suite.bundled_version(payload);
            std::string text = !state.checked   ? "Checking"
                               : !state.running ? "Not running"
                                                : "Runs";
            if (state.running && !state.version.empty())
                text += " " + state.version;
            const bool older = state.running && !bundled.empty() &&
                               signal::compare_versions(state.version, bundled) < 0;
            if (older)
                text += ", app has " + bundled;
            if (state.busy)
                text = state.running ? "Restarting" : "Starting";
            form_.set_value_text(row_id(payload, kStatusRow), text);
            if (ui::FormRow *row = form_.row(row_id(payload, kStartRow)))
            {
                row->label = state.running ? "Restart" : "Start";
                row->text = older ? "Update" : "";
            }
            form_.set_disabled(row_id(payload, kStartRow), state.busy || !suite.bundled(payload));
            const std::size_t i = static_cast<std::size_t>(payload);
            if (state.serial != serial_[i])
            {
                serial_[i] = state.serial;
                toasts_.push(state.failed ? ui::StatusKind::warning : ui::StatusKind::success,
                             state.message);
            }
        }
        const signal::BrokerState &broker = suite.broker();
        const bool ha = suite.state(signal::Payload::homeassistant).running;
        form_.set_value_text(kConnection, !ha ? "ps5-homeassistant is not running"
                                          : broker.state.empty() ? "-"
                                          : broker.error.empty()
                                              ? broker.state
                                              : broker.state + ": " + broker.error);
        if (broker.known)
            broker_ = broker.config;
        char port[16];
        std::snprintf(port, sizeof(port), "%d", broker_.port);
        set_action_text(kHost, broker_.host.empty() ? "Not set" : broker_.host);
        set_action_text(kPort, port);
        set_action_text(kUser, broker_.username.empty() ? "None" : broker_.username);
        set_action_text(kPassword, broker_.password_set ? "Set" : "Not set");
        for (const int id : {kHost, kPort, kUser, kPassword})
            form_.set_disabled(id, !ha || !broker.known);
        // An older ps5-homeassistant ignores the field and would seem to accept it: off, and say
        // why.
        const bool power_known = ha && broker.known && broker_.power_supported;
        form_.set_disabled(kPower, !power_known);
        form_.set_toggle(kPower, power_known && broker_.power_actions);
        if (ui::FormRow *row = form_.row(kPower))
            row->description = ha && broker.known && !broker_.power_supported
                                   ? "Needs ps5-homeassistant 0.2.0 or later: restart it above"
                                   : kPowerHelp;
        if (broker.saved != saved_seen_)
        {
            saved_seen_ = broker.saved;
            if (broker.save_error.empty())
                toasts_.push(ui::StatusKind::success, "Broker saved",
                             "ps5-homeassistant connects again");
            else
                toasts_.push(ui::StatusKind::warning, "Not saved", broker.save_error);
        }
    }

    void set_action_text(int id, std::string text)
    {
        if (ui::FormRow *row = form_.row(id))
            row->text = std::move(text);
    }

    void act(int id, signal::Suite &suite, app::Feedback &feedback)
    {
        for (const signal::Payload payload : kOrder)
        {
            if (id == row_id(payload, kStartRow))
            {
                if (suite.state(payload).running)
                    suite.restart(payload);
                else
                    suite.start(payload);
                return;
            }
        }
        prompt_.style.password = false;
        prompt_.style.auto_capital = false;
        prompt_.style.allow_empty = false;
        prompt_.style.max_length = 0;
        switch (id)
        {
        case kHost:
            editing_ = Editing::host;
            prompt_.style.max_length = 253;
            prompt_.set_title("Broker address");
            prompt_.field.set_placeholder("192.168.1.10");
            prompt_.open(feedback, broker_.host);
            break;
        case kPort:
        {
            editing_ = Editing::port;
            prompt_.style.max_length = 5;
            prompt_.set_title("Broker port");
            prompt_.field.set_placeholder("1883");
            char port[16];
            std::snprintf(port, sizeof(port), "%d", broker_.port);
            prompt_.open(feedback, port);
            break;
        }
        case kUser:
            editing_ = Editing::user;
            prompt_.style.max_length = 64;
            prompt_.style.allow_empty = true;
            prompt_.set_title("User on the broker");
            prompt_.field.set_placeholder("ps5");
            prompt_.open(feedback, broker_.username);
            break;
        case kPassword:
            editing_ = Editing::password;
            prompt_.style.max_length = 128;
            prompt_.style.password = true;
            prompt_.set_title("Password of that user");
            prompt_.field.set_placeholder("");
            prompt_.open(feedback, "");
            break;
        default:
            break;
        }
    }

    // The new value, checked here as ps5-homeassistant checks it, then saved.
    void finish_edit(signal::Suite &suite, const std::string &text)
    {
        signal::BrokerConfig next = broker_;
        std::string password;
        const Editing editing = editing_;
        editing_ = Editing::none;
        const bool plain = text.find_first_of("\"\\") == std::string::npos;
        switch (editing)
        {
        case Editing::host:
            if (text.empty() ||
                text.find_first_not_of(
                    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") !=
                    std::string::npos)
            {
                toasts_.push(ui::StatusKind::warning, "Not saved",
                             "An address has letters, digits, dots and hyphens");
                return;
            }
            next.host = text;
            break;
        case Editing::port:
        {
            const long port = std::strtol(text.c_str(), nullptr, 10);
            if (port < 1 || port > 65535 ||
                text.find_first_not_of("0123456789") != std::string::npos)
            {
                toasts_.push(ui::StatusKind::warning, "Not saved", "A port is 1 to 65535");
                return;
            }
            next.port = static_cast<int>(port);
            break;
        }
        case Editing::user:
            if (!plain)
            {
                toasts_.push(ui::StatusKind::warning, "Not saved", "No quotes or backslashes");
                return;
            }
            next.username = text;
            break;
        case Editing::password:
            if (!plain)
            {
                toasts_.push(ui::StatusKind::warning, "Not saved", "No quotes or backslashes");
                return;
            }
            password = text;
            break;
        case Editing::none:
            return;
        }
        broker_ = next;
        suite.save_broker(next, password);
    }

    void changed(int id)
    {
        if (id == kPower)
        {
            // Shown at once; the next answer from ps5-homeassistant confirms it.
            broker_.power_actions = form_.toggle_value(kPower);
            signal::suite().save_power_actions(broker_.power_actions);
            return;
        }
        for (const signal::Payload payload : kOrder)
        {
            if (id != row_id(payload, kAutoloadRow))
                continue;
            const bool on = form_.toggle_value(id);
            const signal::AutoloadPayload &file = signal::payload_info(payload).file;
            const std::string error =
                on ? signal::autoload_add(signal::kAutoloadDirectory, file)
                   : signal::autoload_remove(signal::kAutoloadDirectory, file);
            const char *title = signal::payload_info(payload).title;
            if (!error.empty())
            {
                form_.set_toggle(id, !on);
                toasts_.push(ui::StatusKind::warning, "ps5-autoloader not changed", error);
                return;
            }
            toasts_.push(ui::StatusKind::success,
                         std::string(title) +
                             (on ? " starts with ps5-autoloader" : " taken out of ps5-autoloader"));
            return;
        }
    }

    // An older copy in ps5-autoloader's folder is replaced by the app's, once per visit.
    void refresh_autoload()
    {
        for (const signal::Payload payload : kOrder)
        {
            const signal::PayloadInfo &info = signal::payload_info(payload);
            const signal::AutoloadRefresh refresh =
                signal::autoload_refresh(signal::kAutoloadDirectory, info.file);
            if (refresh.updated)
                toasts_.push(ui::StatusKind::success,
                             std::string(info.title) + " updated in ps5-autoloader",
                             "Version " + refresh.to + " starts with the console from now on");
            else if (!refresh.error.empty())
                toasts_.push(ui::StatusKind::warning, std::string(info.title) + " not updated",
                             refresh.error);
        }
    }

    void draw_side(ui::Painter &paint) const
    {
        const ui::Theme &look = paint.theme();
        const Rect inside = paint.surface(kSide, 0.0f, look.surface, look.outline, 1.0f);
        const float x = inside.x + 36.0f;
        float y = inside.y + 56.0f;
        paint.label("THE SUITE", x, y, 19.0f, look.text_muted);
        y += 56.0f;
        const signal::Suite &suite = signal::suite();
        for (const signal::Payload payload : kOrder)
        {
            const signal::PayloadInfo &info = signal::payload_info(payload);
            const bool running = suite.state(payload).running;
            paint.fill({x, y - 14.0f, 14.0f, 14.0f}, 7.0f,
                       running ? Color::rgb(0x36d67e) : look.text_muted);
            paint.heading(info.title, x + 30.0f, y, 30.0f, look.text);
            y += 38.0f;
            paint.body(info.purpose, x + 30.0f, y, 21.0f, look.text_muted);
            y += 70.0f;
        }
        paint.body("ps5-homeassistant reads ps5-exporter and uses", x, y, 21.0f, look.text_muted);
        paint.body("ps5-signal, so ps5-autoloader starts them in this order.", x, y + 30.0f, 21.0f,
                   look.text_muted);
    }

    app::Context &context_;
    ui::Form form_;
    ui::ToastStack toasts_;
    ui::InputPrompt prompt_;
    Editing editing_ = Editing::none;
    signal::BrokerConfig broker_;
    std::array<unsigned, 3> serial_{};
    unsigned saved_seen_ = 0;
    float clock_ = 0.0f;
    float age_ = 0.0f;
    bool reduced_ = false;
    bool styled_ = false;
    bool refreshed_ = false;
    bool autoload_found_ = false;
};

} // namespace

std::unique_ptr<app::Concept> make_payloads(app::Context &context)
{
    return std::make_unique<PayloadsScreen>(context);
}

} // namespace hui::concepts
