// Signal Companion - What the three screens share: theme, header, hints and the light bar.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "concepts/companion/look.hpp"

#include "ui/widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace hui::concepts::companion
{

namespace
{

using gfx::Color;
using gfx::Rect;

// The three LEDs as they look lit, from the console's own blue, white and orange.
constexpr std::uint32_t kBlue = 0x2a64ff;
constexpr std::uint32_t kWhite = 0xffffff;
constexpr std::uint32_t kAmber = 0xff9419;

// Lines of running text wrapped to a width; returns the baseline after the last.
float paragraph(ui::Painter &paint, std::string_view text, float x, float y, float size,
                float width, Color color, float leading = 1.4f)
{
    const ui::FontRef &font = paint.font(ui::FontRole::regular);
    for (const std::string &line : font.font->wrap(text, size, width))
    {
        paint.body(line, x, y, size, color);
        y += size * leading;
    }
    return y;
}

} // namespace

const ui::Theme &theme()
{
    static const ui::Theme kTheme = []
    {
        for (const ui::Theme &candidate : ui::themes())
        {
            if (std::string_view(candidate.id) == "brutal")
                return candidate;
        }
        return ui::default_theme();
    }();
    return kTheme;
}

void begin_frame(app::Frame &frame, const ui::Theme &look)
{
    frame.backdrop = look.backdrop;
    frame.scene.rounded_rect({0, 0, gfx::kVirtualWidth, gfx::kVirtualHeight}, 0, look.page);
}

void draw_header(ui::Painter &paint, const char *title, const char *summary,
                 const signal::Link &link, float clock)
{
    const ui::Theme &look = paint.theme();
    const Color ink = paint.page_text();
    const Color quiet = paint.page_text_muted();
    paint.label("SIGNAL COMPANION", kMargin, 104.0f, 19.0f, quiet);
    paint.heading(title, kMargin - 2.0f, 172.0f, 58.0f, ink);
    paint.body(summary, kMargin, 214.0f, 23.0f, quiet);

    // The service's state as a loud sticker: green, pink or a pulsing grey.
    const char *state = "CHECKING";
    Color fill = look.surface_high;
    switch (link.state())
    {
    case signal::LinkState::online:
        state = "SERVICE RUNNING";
        fill = look.accent;
        break;
    case signal::LinkState::offline:
        state = "SERVICE NOT RUNNING";
        fill = look.primary;
        break;
    case signal::LinkState::checking:
        fill = gfx::mix(look.surface_high, look.surface, 0.5f + 0.5f * std::sin(clock * 5.0f));
        break;
    }
    const float width = paint.label_width(state, 22.0f) + 74.0f;
    const Rect sticker{kRight - width, 92.0f, width, 58.0f};
    const Rect inside = paint.surface(sticker, 0.0f, fill, look.outline, 1.0f);
    paint.fill({inside.x + 22.0f, inside.cy() - 7.0f, 14.0f, 14.0f}, 0.0f, look.text);
    paint.label(state, inside.x + 50.0f, inside.cy() + 8.0f, 22.0f, look.text);
    if (link.online() && !link.status().version.empty())
    {
        char text[64];
        std::snprintf(text, sizeof(text), "ps5-signal %s on 127.0.0.1:%d",
                      link.status().version.c_str(), signal::kPort);
        paint.body(text, kRight, 196.0f, 21.0f, quiet, gfx::Align::right);
    }
}

void draw_hints(gfx::DrawList &list, const ui::Fonts &fonts, const ui::Theme &look,
                std::span<const ui::Hint> hints)
{
    ui::Hint row[10];
    int count = 0;
    for (const ui::Hint &hint : hints)
    {
        if (count < 9)
            row[count++] = hint;
    }
    row[count++] = {ui::Button::l1, "Screen", ui::Button::r1};
    ui::GlyphStyle glyphs = look.dark ? ui::GlyphStyle::dark() : ui::GlyphStyle::light();
    glyphs.label = look.text;
    ui::draw_hints(list, fonts, glyphs, row, count, kRight, true);
}

Color led_color(const signal::Mix &mix)
{
    const float blue = static_cast<float>(std::clamp(mix.blue, 0, 255)) / 255.0f;
    const float white = static_cast<float>(std::clamp(mix.white, 0, 255)) / 255.0f;
    const float amber = static_cast<float>(std::clamp(mix.amber, 0, 255)) / 255.0f;
    const Color b = Color::rgb(kBlue);
    const Color w = Color::rgb(kWhite);
    const Color a = Color::rgb(kAmber);
    Color sum{b.r * blue + w.r * white + a.r * amber, b.g * blue + w.g * white + a.g * amber,
              b.b * blue + w.b * white + a.b * amber, 1.0f};
    const float peak = std::max({sum.r, sum.g, sum.b, 1.0f});
    sum = {sum.r / peak, sum.g / peak, sum.b / peak, 1.0f};
    // A dark LED reads as the dull plastic of the strip.
    const float lit = std::max({blue, white, amber});
    return gfx::mix(Color::rgb(0x3a3d44), sum, std::sqrt(lit));
}

void draw_light_bar(gfx::DrawList &list, const Rect &r, const signal::Mix &mix, float clock)
{
    const Color light = led_color(mix);
    const float lit = static_cast<float>(std::max({mix.blue, mix.white, mix.amber})) / 255.0f;
    // The console's black core between two white covers, seen from the front.
    list.rounded_rect(r, 0.0f, Color::rgb(0x000000));
    const Rect core = r.inset(10.0f);
    list.rounded_rect(core, 0.0f, Color::rgb(0x15171c));
    const Rect cover_left{core.x, core.y, core.w * 0.16f, core.h};
    const Rect cover_right{core.x + core.w * 0.84f, core.y, core.w * 0.16f, core.h};
    list.gradient_rect_h(cover_left, 0.0f, Color::rgb(0xf4f5f7), Color::rgb(0xd6d9df));
    list.gradient_rect_h(cover_right, 0.0f, Color::rgb(0xd6d9df), Color::rgb(0xf4f5f7));

    // The strip: a slow breath so a still picture still reads as light.
    const float breath = 0.88f + 0.12f * std::sin(clock * 1.6f);
    const Rect strip{core.x + core.w * 0.2f, core.cy() - 9.0f, core.w * 0.6f, 18.0f};
    if (lit > 0.01f)
        list.glow(strip, 9.0f, 70.0f * lit, light.with_alpha(0.75f * lit * breath));
    list.rounded_rect(strip, 9.0f, light);
    if (lit > 0.01f)
        list.rounded_rect(strip.inset(5.0f), 4.0f,
                          gfx::mix(light, Color::rgb(0xffffff), 0.55f).with_alpha(lit));
}

void draw_offline_card(ui::Painter &paint, const Rect &r, const signal::Link &link)
{
    const ui::Theme &look = paint.theme();
    const Rect inside = paint.surface(r, 0.0f, look.surface, look.outline, 1.0f);
    const float x = inside.x + 36.0f;
    const float width = inside.w - 72.0f;
    float y = inside.y + 70.0f;
    if (link.state() == signal::LinkState::checking)
    {
        paint.heading("Looking for ps5-signal", x, y, 36.0f, look.text);
        paragraph(paint, "Asking 127.0.0.1:9150 whether the service runs.", x, y + 52.0f, 24.0f,
                  width, look.text_muted);
        return;
    }
    const signal::StartState start = link.start_state();
    if (start == signal::StartState::sending || start == signal::StartState::waiting)
    {
        paint.heading("Starting ps5-signal", x, y, 36.0f, look.text);
        paragraph(paint,
                  start == signal::StartState::sending
                      ? "Sending ps5-signal.elf to etaHEN's ELF loader on port 9021."
                      : "Sent. Waiting for the service to answer; you hear its startup tune.",
                  x, y + 52.0f, 24.0f, width, look.text_muted);
        return;
    }
    paint.heading("Start ps5-signal first", x, y, 36.0f, look.text);
    y = paragraph(paint,
                  "This app sets the light and the startup signal through the ps5-signal "
                  "service. It does not answer on this console yet.",
                  x, y + 52.0f, 24.0f, width, look.text_muted);
    if (start == signal::StartState::failed)
        y = paragraph(paint, link.start_error(), x, y + 6.0f, 24.0f, width, look.danger);
    const char *const kBundled[] = {
        "Press X on \"Start ps5-signal\": the app sends it to etaHEN's ELF loader.",
        "Or send ps5-signal.elf yourself (port 9021), or let your autoloader load it.",
        "This screen checks again every 3 seconds.",
    };
    const char *const kManual[] = {
        ("Send ps5-signal.elf to your ELF loader (etaHEN: port 9021), or let your autoloader "
         "load it at start-up."),
        "Wait for its startup tune.",
        "This screen checks again every 3 seconds, or press X on \"Check again\".",
    };
    y += 18.0f;
    int number = 1;
    for (const char *step : link.can_start() ? kBundled : kManual)
    {
        const Rect badge{x, y - 28.0f, 40.0f, 40.0f};
        paint.surface(badge, 0.0f, look.surface_high, look.outline, 1.0f, 3.0f);
        char text[4];
        std::snprintf(text, sizeof(text), "%d", number++);
        paint.label(text, badge.cx(), badge.cy() + 8.0f, 22.0f, look.text, gfx::Align::center);
        y = paragraph(paint, step, x + 62.0f, y, 24.0f, width - 62.0f, look.text) + 14.0f;
    }
}

std::string mix_text(const signal::Mix &mix)
{
    char text[64];
    std::snprintf(text, sizeof(text), "Blue %d  White %d  Amber %d", mix.blue, mix.white,
                  mix.amber);
    return text;
}

std::string reply_problem(const signal::Reply &reply)
{
    if (reply.status >= 200 && reply.status < 300)
        return {};
    switch (reply.status)
    {
    case -1:
        return "The service did not answer";
    case 409:
        return "Something of that kind is playing: wait for it, or stop it";
    case 429:
        return "Over the service's per-minute limit: wait a moment";
    case 503:
        return "This console does not offer that";
    default:
        break;
    }
    std::string reason = signal::error_text(reply.body);
    if (reason.empty())
    {
        char text[48];
        std::snprintf(text, sizeof(text), "The service answered %d", reply.status);
        reason = text;
    }
    return reason;
}

} // namespace hui::concepts::companion
