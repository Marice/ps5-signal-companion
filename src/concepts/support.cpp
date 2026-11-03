// Signal Companion - Screen "Support": a Ko-fi QR code, for those who like what Signal Companion
// does. SPDX-License-Identifier: GPL-3.0-or-later
//
// The code is a PNG in assets/support. The app hands this screen a loader for it (GL work, which
// screens leave to the app; see set_support_qr), used once when the screen is first drawn. The
// screen needs nothing from ps5-signal, so it shows while the service is away.

#include "concepts/concepts.hpp"

#include "concepts/companion/look.hpp"
#include "core/tween.hpp"
#include "ui/widgets.hpp"

#include <string>

namespace hui::concepts
{

namespace
{

using gfx::Color;
using gfx::Rect;
using namespace companion;

constexpr const char *kLink = "ko-fi.com/H5L629ZHNX";

constexpr const char *kTechniques[] = {
    "The QR code is a PNG in the app's assets, decoded once with stb_image into a texture",
    "It leads to ko-fi.com/H5L629ZHNX; nothing is sent from the console",
    "Needs nothing from ps5-signal: it shows while the service is away",
};

constexpr app::TourStep kTour[] = {
    {1.2f, 0, Direction::none, "support"},
};

// Set by the app: how to turn the PNG into a texture, and where the PNG is.
std::uint32_t (*g_load_png)(const std::string &path) = nullptr;
std::string g_qr_path;

class SupportScreen final : public app::Concept
{
  public:
    explicit SupportScreen(app::Context &context) : context_(context)
    {
    }

    const app::ConceptInfo &info() const override
    {
        static const app::ConceptInfo kInfo{
            "support",
            "Support",
            "Buy me a coffee or a beer",
            "src/concepts/support.cpp",
            audio::SoundSet::paper,
            Color::rgb(0xff5e5b),
            kTechniques,
        };
        return kInfo;
    }

    void enter() override
    {
        age_ = 0.0f;
    }

    void update(const InputFrame & /*input*/, float dt, app::Feedback & /*feedback*/) override
    {
        clock_ += dt;
        age_ += dt;
        signal::link().update(dt);
    }

    void draw(app::Frame &frame) const override
    {
        const ui::Theme &look = theme();
        begin_frame(frame, look);
        ui::Painter paint(frame.scene, context_.fonts, look);
        frame.scene.push_opacity(tween::cubic_out(age_ / 0.3f));
        draw_header(paint, "Support", "Buy me a coffee or a beer", signal::link(), clock_);
        if (!tried_)
        {
            tried_ = true;
            texture_ = g_load_png != nullptr ? g_load_png(g_qr_path) : 0;
        }

        constexpr float kQrSize = 560.0f;
        const Rect qr{kMargin, kTop + 20.0f, kQrSize, kQrSize};
        if (texture_ != 0)
            frame.scene.image(texture_, qr, gfx::kFullUv, Color::rgb(0xffffff), 24.0f);
        else
            paint.fill(qr, 24.0f, look.surface);

        const float x = kMargin + kQrSize + 80.0f;
        const float width = kRight - x;
        float y = kTop + 120.0f;
        paint.heading("Enjoying Signal Companion?", x, y, 56.0f, paint.page_text());
        y += 80.0f;
        const ui::FontRef &font = paint.font(ui::FontRole::regular);
        for (const std::string &line :
             font.font->wrap("If you like what I make, you can buy me a coffee or a beer. Scan the "
                             "code with your phone. Thank you!",
                             30.0f, width))
        {
            paint.body(line, x, y, 30.0f, paint.page_text_muted());
            y += 42.0f;
        }
        y += 70.0f;
        paint.label("KO-FI", x, y, 20.0f, paint.page_text_muted());
        y += 52.0f;
        paint.body(kLink, x, y, 34.0f, look.accent);
        if (texture_ == 0)
        {
            y += 70.0f;
            paint.body("The code could not be loaded; the link above leads to the same page.", x, y,
                       22.0f, paint.page_text_muted());
        }
        draw_hints(frame.scene, context_.fonts, look, std::span<const ui::Hint>());
        frame.scene.pop_opacity();
    }

    std::span<const app::TourStep> tour() const override
    {
        return kTour;
    }

  private:
    app::Context &context_;
    float clock_ = 0.0f;
    float age_ = 0.0f;
    mutable bool tried_ = false;
    mutable std::uint32_t texture_ = 0;
};

} // namespace

void set_support_qr(std::uint32_t (*load_png)(const std::string &path), std::string path)
{
    g_load_png = load_png;
    g_qr_path = std::move(path);
}

std::unique_ptr<app::Concept> make_support(app::Context &context)
{
    return std::make_unique<SupportScreen>(context);
}

} // namespace hui::concepts
