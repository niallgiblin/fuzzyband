#pragma once

/**
 * @file
 * @brief Plugin editor: user policy controls + diagnostic readouts (Phase 14).
 */

#include <JuceHeader.h>

class AccompanimentProcessor;
class SectionListEditor;  // defined in AccompanimentEditor.cpp (Phase 2 custom song form)

/**
 * @brief Warm Pacific-NW palette shared by the editor and the look-and-feel.
 *
 * Cream ink over deep forest greens, a moss accent and an amber emphasis —
 * a warmer, less flat-white/green scheme than the original.
 */
namespace FuzzybandPalette
{
    constexpr juce::uint32 ink        = 0xffeee9dc; // warm cream (primary text)
    constexpr juce::uint32 inkMuted   = 0xffa9b39c; // sage-grey (secondary/labels)
    constexpr juce::uint32 inkWarm    = 0xffd8d2c2; // warm off-white (data readouts)
    constexpr juce::uint32 moss       = 0xff8cc46a; // accent / brand
    constexpr juce::uint32 amber      = 0xffe0b06a; // status emphasis
    constexpr juce::uint32 sage       = 0xff6a9a50; // border / outline accent
}

/**
 * @brief Pacific NW moss/forest theme — semi-transparent controls over background image.
 */
class FuzzybandLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    FuzzybandLookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId,      juce::Colour(0x00000000));
        // Slider — semi-transparent tints
        setColour(juce::Slider::thumbColourId,                    juce::Colour(FuzzybandPalette::moss));
        setColour(juce::Slider::trackColourId,                    juce::Colour(0x882a4028));
        setColour(juce::Slider::backgroundColourId,               juce::Colour(0x00000000));
        setColour(juce::Slider::textBoxTextColourId,              juce::Colour(FuzzybandPalette::ink));
        setColour(juce::Slider::textBoxBackgroundColourId,        juce::Colour(0x00000000));
        setColour(juce::Slider::textBoxOutlineColourId,           juce::Colour(0x00000000));
        // ComboBox — semi-transparent dark green
        setColour(juce::ComboBox::backgroundColourId,             juce::Colour(0xcc0e1a0c));
        setColour(juce::ComboBox::textColourId,                   juce::Colour(FuzzybandPalette::ink));
        setColour(juce::ComboBox::outlineColourId,                juce::Colour(0x886a9a50));
        setColour(juce::ComboBox::arrowColourId,                  juce::Colour(FuzzybandPalette::moss));
        setColour(juce::ComboBox::focusedOutlineColourId,         juce::Colour(FuzzybandPalette::moss));
        // Label
        setColour(juce::Label::textColourId,                      juce::Colour(FuzzybandPalette::ink));
        // TextButton — semi-transparent
        setColour(juce::TextButton::buttonColourId,               juce::Colour(0xcc0e1a0c));
        setColour(juce::TextButton::buttonOnColourId,             juce::Colour(0xcc3a6030));
        setColour(juce::TextButton::textColourOffId,              juce::Colour(FuzzybandPalette::ink));
        setColour(juce::TextButton::textColourOnId,               juce::Colour(0xff9ade78));
        // PopupMenu (opaque — floats over DAW, must be readable)
        setColour(juce::PopupMenu::backgroundColourId,            juce::Colour(0xf01a2a18));
        setColour(juce::PopupMenu::textColourId,                  juce::Colour(FuzzybandPalette::ink));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff2a4028));
        setColour(juce::PopupMenu::highlightedTextColourId,       juce::Colour(0xff9ade78));
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float, float,
                          const juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearVertical)
        {
            LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, 0, 1, style, slider);
            return;
        }
        const bool isHoriz = (style == juce::Slider::LinearHorizontal);
        const float trackH = 3.0f;
        juce::Rectangle<float> track;
        if (isHoriz)
            track = { (float)x, (float)y + (float)height * 0.5f - trackH * 0.5f, (float)width, trackH };
        else
            track = { (float)x + (float)width * 0.5f - trackH * 0.5f, (float)y, trackH, (float)height };
        juce::Colour const trackColour = juce::Colour(0x882a4028);
        juce::Colour const fillColour = juce::Colour(FuzzybandPalette::moss);
        g.setColour(trackColour);
        g.fillRoundedRectangle(track, trackH * 0.5f);
        juce::Rectangle<float> filled;
        if (isHoriz)
            filled = { track.getX(), track.getY(), sliderPos - (float)x, trackH };
        else
            filled = { track.getX(), sliderPos, trackH, track.getBottom() - sliderPos };
        g.setColour(fillColour);
        g.fillRoundedRectangle(filled, trackH * 0.5f);
        const float thumbR = 6.0f;
        juce::Rectangle<float> thumb;
        if (isHoriz)
            thumb = { sliderPos - thumbR, (float)y + (float)height * 0.5f - thumbR, thumbR * 2.0f, thumbR * 2.0f };
        else
            thumb = { (float)x + (float)width * 0.5f - thumbR, sliderPos - thumbR, thumbR * 2.0f, thumbR * 2.0f };
        g.setColour(fillColour);
        g.fillEllipse(thumb);
        g.setColour(juce::Colour(0x66000000));
        g.drawEllipse(thumb.reduced(1.5f), 1.0f);
    }

    juce::Font getComboBoxFont(juce::ComboBox& box) override
    {
        if (tfAlegreyaMedium_) return juce::Font(juce::FontOptions(tfAlegreyaMedium_).withHeight(12.0f));
        return LookAndFeel_V4::getComboBoxFont(box);
    }

    juce::Font getTextButtonFont(juce::TextButton& b, int height) override
    {
        if (tfAlegreyaMedium_) return juce::Font(juce::FontOptions(tfAlegreyaMedium_).withHeight(juce::jmin(12.0f, (float) height * 0.5f)));
        return LookAndFeel_V4::getTextButtonFont(b, height);
    }

    // ── Bundled typefaces (installed once from the editor ctor) ──────────────
    void installFonts(juce::Typeface::Ptr aReg, juce::Typeface::Ptr aMed, juce::Typeface::Ptr aBold,
                      juce::Typeface::Ptr mReg, juce::Typeface::Ptr mMed)
    {
        tfAlegreyaRegular_ = std::move(aReg);
        tfAlegreyaMedium_  = std::move(aMed);
        tfAlegreyaBold_    = std::move(aBold);
        tfMonoRegular_     = std::move(mReg);
        tfMonoMedium_      = std::move(mMed);
    }

    /** Display font (Alegreya Sans bold) — titles and emphasis. */
    juce::Font displayFont(float h) const
    { return tfAlegreyaBold_ ? juce::Font(juce::FontOptions(tfAlegreyaBold_).withHeight(h)) : juce::Font(juce::FontOptions().withHeight(h)); }
    /** Label font (Alegreya Sans medium) — headings, field labels, buttons. */
    juce::Font labelFont(float h) const
    { return tfAlegreyaMedium_ ? juce::Font(juce::FontOptions(tfAlegreyaMedium_).withHeight(h)) : juce::Font(juce::FontOptions().withHeight(h)); }
    /** Body font (Alegreya Sans regular) — longer/prose text. */
    juce::Font bodyFont(float h) const
    { return tfAlegreyaRegular_ ? juce::Font(juce::FontOptions(tfAlegreyaRegular_).withHeight(h)) : juce::Font(juce::FontOptions().withHeight(h)); }
    /** Monospace (IBM Plex Mono) — numeric/status readouts, version. */
    juce::Font monoFont(float h) const
    { return tfMonoRegular_ ? juce::Font(juce::FontOptions(tfMonoRegular_).withHeight(h)) : juce::Font(juce::FontOptions().withHeight(h)); }

    void drawComboBox(juce::Graphics& g, int width, int height, bool,
                      int, int, int, int, juce::ComboBox&) override
    {
        auto bounds = juce::Rectangle<float>(0.0f, 0.0f, (float)width, (float)height);
        g.setColour(findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(findColour(juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

        // Do not draw the selected item here. ComboBox already has a Label
        // child that paints the text — drawing it again ghosts/doubles it.

        const float arrowX = (float)width - 16.0f;
        const float arrowY = (float)height * 0.5f;
        juce::Path arrow;
        arrow.addTriangle(arrowX, arrowY - 3.0f, arrowX + 7.0f, arrowY - 3.0f, arrowX + 3.5f, arrowY + 3.0f);
        g.setColour(findColour(juce::ComboBox::arrowColourId));
        g.fillPath(arrow);
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(8, 1, juce::jmax(1, box.getWidth() - 30), box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
        label.setJustificationType(juce::Justification::centredLeft);
        label.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        label.setColour(juce::Label::textColourId, findColour(juce::ComboBox::textColourId));
        label.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    }

    // ── Popup menu (ComboBox dropdown): clean, no big checkmark ─────────────
    // LookAndFeel_V4 draws a large tick glyph next to the selected item. For the
    // genre menu we want a sleeker marker: a slim moss accent bar on the left
    // edge of the selected row plus a soft fill, and a full fill on the row under
    // the cursor. No icon gutter, so the text sits close to the edge.
    juce::Font getPopupMenuFont() override
    {
        if (tfAlegreyaMedium_) return juce::Font(juce::FontOptions(tfAlegreyaMedium_).withHeight(13.0f));
        return LookAndFeel_V4::getPopupMenuFont();
    }

    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                           const bool isSeparator, const bool isActive, const bool isHighlighted,
                           const bool isTicked, const bool /*hasSubMenu*/,
                           const juce::String& text, const juce::String& /*shortcutKeyText*/,
                           const juce::Drawable* icon, const juce::Colour* const textColourToUse) override
    {
        if (isSeparator)
        {
            g.setColour(findColour(juce::PopupMenu::textColourId).withAlpha(0.2f));
            g.fillRect(area.getX() + 8, area.getCentreY(), juce::jmax(1, area.getWidth() - 16), 1);
            return;
        }

        const bool selected = isTicked || isActive;
        const juce::Colour textColour = (textColourToUse != nullptr)
            ? *textColourToUse : findColour(juce::PopupMenu::textColourId);

        // Row background: hover wins; a selected (but not hovered) row gets a
        // soft fill so the current genre stays legible in the list.
        if (isHighlighted)
            g.setColour(findColour(juce::PopupMenu::highlightedBackgroundColourId));
        else if (selected)
            g.setColour(findColour(juce::PopupMenu::highlightedBackgroundColourId).withAlpha(0.45f));
        else
            g.setColour(juce::Colours::transparentBlack);
        if (isHighlighted || selected)
            g.fillRect(area);

        // Selected marker: a slim moss accent bar on the left edge (no tick).
        if (selected)
        {
            g.setColour(juce::Colour(FuzzybandPalette::moss));
            g.fillRoundedRectangle((float)area.getX() + 3.0f, (float)area.getY() + 5.0f,
                                   3.0f, (float)area.getHeight() - 10.0f, 1.5f);
        }

        g.setFont(getPopupMenuFont());
        g.setColour(isHighlighted
            ? findColour(juce::PopupMenu::highlightedTextColourId)
            : textColour.withMultipliedAlpha(selected ? 1.0f : 0.85f));

        auto textArea = area.reduced(13, 0);
        if (icon != nullptr)
        {
            auto iconArea = textArea.removeFromLeft(area.getHeight() - 4).toFloat();
            icon->drawWithin(g, iconArea,
                             juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize,
                             1.0f);
            textArea.removeFromLeft(6);
        }
        g.drawFittedText(text, textArea, juce::Justification::centredLeft, 1);
    }

    void getIdealPopupMenuItemSize(const juce::String& text, const bool isSeparator,
                                   int standardMenuItemHeight, int& idealWidth, int& idealHeight) override
    {
        if (isSeparator)
        {
            idealWidth = 50;
            idealHeight = standardMenuItemHeight > 0 ? standardMenuItemHeight / 8 : 8;
            return;
        }
        const auto font = getPopupMenuFont();
        idealHeight = juce::roundToInt(font.getHeight() * 1.7f);

        // Measure the label with TextLayout (Font::getStringWidth() is deprecated
        // in JUCE 8). The extra height covers the accent bar + horizontal padding.
        juce::AttributedString astr(text);
        astr.setFont(font);
        juce::TextLayout layout;
        layout.createLayout(astr, 10000.0f);
        idealWidth = juce::roundToInt(layout.getWidth()) + idealHeight;
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour&, bool isHighlighted, bool isDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);

        // Sleek, uniform square icon buttons for the section rows (−/+ bar
        // stepper and × remove): near-transparent fill with a hairline outline
        // that brightens on hover/press.
        if (isSectionIconButton(button))
        {
            const bool active = isDown || isHighlighted;
            g.setColour(active ? juce::Colour(0x4a3a6030) : juce::Colour(0x240e1a0c));
            g.fillRoundedRectangle(bounds, 3.0f);
            g.setColour(active ? juce::Colour(0xff9ade78) : juce::Colour(0x6a6a9a50));
            g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
            return;
        }

        auto baseColour = findColour(button.getToggleState()
            ? juce::TextButton::buttonOnColourId
            : juce::TextButton::buttonColourId);
        if (isDown || isHighlighted)
            baseColour = baseColour.brighter(0.18f);
        g.setColour(baseColour);
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(juce::Colour(0x886a9a50));
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool, bool) override
    {
        const auto textColour = button.findColour(button.getToggleState()
                ? juce::TextButton::textColourOnId
                : juce::TextButton::textColourOffId)
            .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f);
        g.setColour(textColour);
        g.setFont(getTextButtonFont(button, button.getHeight()));

        // Draw a Path icon instead of a Unicode glyph — plugin fonts often
        // lack ▶, which then shows as mojibake (â¶ PLAY).
        if (button.getComponentID() == "play")
        {
            auto bounds = button.getLocalBounds().reduced(8, 4);
            auto icon = bounds.removeFromLeft(14).toFloat();
            const float cx = icon.getCentreX();
            const float cy = icon.getCentreY();
            if (button.getToggleState())
            {
                g.fillRect(cx - 5.0f, cy - 6.0f, 3.5f, 12.0f);
                g.fillRect(cx + 1.5f, cy - 6.0f, 3.5f, 12.0f);
            }
            else
            {
                juce::Path tri;
                tri.addTriangle(cx - 4.0f, cy - 6.0f, cx - 4.0f, cy + 6.0f, cx + 6.0f, cy);
                g.fillPath(tri);
            }
            g.drawFittedText("PLAY", bounds, juce::Justification::centredLeft, 1);
            return;
        }

        // Section-row −/+ and × icons are path-drawn so they stay crisp and
        // uniform regardless of the plugin font's glyph coverage.
        if (isSectionIconButton(button))
        {
            const auto b = button.getLocalBounds().toFloat();
            const float cx = b.getCentreX();
            const float cy = b.getCentreY();
            const float a = 3.0f;              // half arm-length
            const float s = 1.8f;              // stroke width
            g.setColour(textColour);
            const juce::String id = button.getComponentID();
            if (id == "sect-inc")
            {
                g.drawLine(cx - a, cy, cx + a, cy, s);
                g.drawLine(cx, cy - a, cx, cy + a, s);
            }
            else if (id == "sect-dec")
            {
                g.drawLine(cx - a, cy, cx + a, cy, s);
            }
            else if (id == "sect-remove")
            {
                g.drawLine(cx - a, cy - a, cx + a, cy + a, s);
                g.drawLine(cx - a, cy + a, cx + a, cy - a, s);
            }
            return;
        }

        g.drawFittedText(button.getButtonText(),
                         button.getLocalBounds().reduced(2),
                         juce::Justification::centred, 1);
    }

    juce::Button* createSliderButton(juce::Slider&, const bool isIncrement) override
    {
        // Tag the IncDecButtons −/+ so drawButtonBackground/drawButtonText can
        // style them as the same sleek square icons as the section remove button.
        auto* b = new juce::TextButton(isIncrement ? "+" : "-", juce::String());
        b->setComponentID(isIncrement ? "sect-inc" : "sect-dec");
        return b;
    }

private:
    static bool isSectionIconButton(const juce::Button& b)
    {
        const auto id = b.getComponentID();
        return id == "sect-inc" || id == "sect-dec" || id == "sect-remove";
    }

    juce::Typeface::Ptr tfAlegreyaRegular_;
    juce::Typeface::Ptr tfAlegreyaMedium_;
    juce::Typeface::Ptr tfAlegreyaBold_;
    juce::Typeface::Ptr tfMonoRegular_;
    juce::Typeface::Ptr tfMonoMedium_;
};

/**
 * @brief APVTS-backed controls, live diagnostics, and debug preview (PUI-02).
 */
class AccompanimentEditor final : public juce::AudioProcessorEditor,
                                  private juce::Timer
{
public:
    explicit AccompanimentEditor(AccompanimentProcessor&);
    ~AccompanimentEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    AccompanimentProcessor& audioProcessorRef;

    juce::Label titleLabel;
    juce::Label versionLabel;
    juce::Label userPolicyHeading;

    juce::Label genreLabel{ {}, "GENRE" };
    juce::ComboBox genreCombo;
    juce::Label swingLabel{ {}, "SWING" };
    juce::Slider swingSlider;

    juce::Label songSectionsLabel{ {}, "SECTIONS" };
    juce::Viewport songSectionsViewport;
    std::unique_ptr<SectionListEditor> sectionListEditor;  // editable custom form

    juce::Label lockBarsLabel{ {}, "LOCK (BARS)" };
    juce::Slider lockBarsSlider;
    juce::Label grooveStatusLabel;  // generative lock indicator

    // A5.2: post-lock transition grammar controls + live status.
    juce::Label transitionBarsLabel{ {}, "TRANSITION (BARS)" };
    juce::Slider transitionBarsSlider;
    juce::Label transitionSectionsLabel{ {}, "TRANSITION SECTIONS" };
    juce::Slider transitionSectionsSlider;
    juce::Label transitionStatusLabel;  // live "Section: VERSE - bar 3/8 - 5 left"

    // Bar-segment progress for the active section (Play / riff lock / transition).
    class SectionProgressComponent final : public juce::Component
    {
    public:
        SectionProgressComponent() = default;

        /** @brief Called from the editor timer with the latest phase progress. */
        void setProgress(int bar, int total, int remaining, float /*fraction*/) noexcept
        {
            bar_ = bar;
            total_ = juce::jmax(0, total);
            remaining_ = juce::jmax(0, remaining);
            repaint();
        }

        void paint(juce::Graphics&) override;

    private:
        int bar_ = 0;
        int total_ = 0;
        int remaining_ = 0;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SectionProgressComponent)
    };
    SectionProgressComponent sectionProgressComponent;

    // DAW-style input scope: live waveform + playhead.
    class ScopeComponent final : public juce::Component
    {
    public:
        ScopeComponent() = default;

        /** @brief Called from the editor timer with the latest scope data. */
        void setScopeData(const float* samples, int count, float playheadFraction,
                          int samplesPerBar) noexcept
        {
            for (int i = 0; i < count && i < static_cast<int>(kMaxSamples); ++i)
                samples_[static_cast<size_t>(i)] = samples[i];
            count_ = juce::jmin(count, static_cast<int>(kMaxSamples));
            playheadFraction_ = playheadFraction;
            samplesPerBar_ = juce::jmax(1, samplesPerBar);
            repaint();
        }

        void paint(juce::Graphics&) override;

    private:
        // Must hold a full bar at the tempos the plugin targets so the bar-aligned
        // scope (downbeat at the left, 1-2-3-4 notches) has all the samples it needs.
        // Keep in sync with AccompanimentProcessor::kScopeSize.
        static constexpr int kMaxSamples = 16384;
        std::array<float, kMaxSamples> samples_{};
        int count_ = 0;
        float playheadFraction_ = 0.0f;
        int samplesPerBar_ = 1;   // decimated samples per bar (bar-aligned render)
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScopeComponent)
    };
    ScopeComponent scopeComponent;

    juce::TextButton playButton{ "PLAY" };
    juce::TextButton recordRiffButton{ "Record riff" };
    juce::TextButton forgetRiffButton{ "Forget" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> genreAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> swingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lockBarsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> transitionBarsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> transitionSectionsAttachment;

    juce::Rectangle<int> userPolicyArea;

    juce::Label bpmLabel{ {}, "BPM: -" };
    juce::Label stateLabel{ {}, "State: -" };
    juce::Label patternLabel{ {}, "Pattern: -" };
    juce::Label styleLabel{ {}, "Style: -" };

    FuzzybandLookAndFeel lookAndFeel;

    juce::Image backgroundImage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AccompanimentEditor)
};
