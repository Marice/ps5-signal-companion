// Signal Companion - What the three screens share: theme, header, hints and the light bar.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/concept.hpp"
#include "signal/model.hpp"
#include "signal/service.hpp"
#include "ui/components/component.hpp"
#include "ui/glyphs.hpp"
#include "ui/theme.hpp"

#include <span>

namespace hui::concepts::companion
{

constexpr float kMargin = 96.0f;
constexpr float kRight = gfx::kVirtualWidth - kMargin;
constexpr float kTop = 290.0f;    // where a screen's content starts
constexpr float kBottom = 940.0f; // ... and ends, above the hints

// The app's one look: neo-brutalism ("02 · Brutal" of the kit).
const ui::Theme &theme();

// Paints the page and returns the canvas everything else draws on.
void begin_frame(app::Frame &frame, const ui::Theme &theme);

// "SIGNAL COMPANION", the screen's title and summary, and on the right
// whether the service answers.
void draw_header(ui::Painter &paint, const char *title, const char *summary,
                 const signal::Link &link, float clock);

// The hint row at the bottom right.
void draw_hints(gfx::DrawList &list, const ui::Fonts &fonts, const ui::Theme &theme,
                std::span<const ui::Hint> hints);

// The empty part of a channel's bar: neutral, so no channel seems lit.
inline gfx::Color empty_channel()
{
    return gfx::Color::rgb(0xe8dfc0);
}

// How the console's light looks with these channels (additive, approximate).
gfx::Color led_color(const signal::Mix &mix);

// A slab of the console with its light strip lit in this mix.
void draw_light_bar(gfx::DrawList &list, const gfx::Rect &r, const signal::Mix &mix, float clock);

// Explains how to start the service, for screens that need it.
void draw_offline_card(ui::Painter &paint, const gfx::Rect &r, const signal::Link &link);

// "Blue 255  White 0  Amber 40".
std::string mix_text(const signal::Mix &mix);

// What an answer means for the player: "" for success, else a short reason.
std::string reply_problem(const signal::Reply &reply);

// One call a screen waits for. start() queues it; done() turns true once,
// in the frame its answer is there.
struct Request
{
    signal::Call call = signal::Call::status;
    unsigned before = 0;
    bool waiting = false;

    void start(signal::Link &link, signal::Call kind, std::string body = {})
    {
        call = kind;
        before = link.service().serial(kind);
        waiting = true;
        link.service().send(kind, std::move(body));
    }
    bool done(signal::Link &link, signal::Reply &reply)
    {
        if (!waiting || link.service().serial(call) == before || link.service().pending(call))
            return false;
        waiting = false;
        reply = link.service().outcome(call).reply;
        return true;
    }
};

} // namespace hui::concepts::companion
