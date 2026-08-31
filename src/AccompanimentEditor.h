#pragma once

/**
 * @file
 * @brief Plugin editor: user policy controls + diagnostic readouts (Phase 14).
 */

#include <JuceHeader.h>

class AccompanimentProcessor;
class SectionListEditor;  // defined in AccompanimentEditor.cpp (Phase 2 custom song form)

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
        setColour(juce::Slider::thumbColourId,                    juce::Colour(0xff7ab860));
        setColour(juce::Slider::trackColourId,                    juce::Colour(0x882a4028));
        setColour(juce::Slider::backgroundColourId,               juce::Colour(0x00000000));
        setColour(juce::Slider::textBoxTextColourId,              juce::Colour(0xffd8ecd0));
        setColour(juce::Slider::textBoxBackgroundColourId,        juce::Colour(0x00000000));
        setColour(juce::Slider::textBoxOutlineColourId,           juce::Colour(0x00000000));
        // ComboBox — semi-transparent dark green
        setColour(juce::ComboBox::backgroundColourId,             juce::Colour(0xcc0e1a0c));
        setColour(juce::ComboBox::textColourId,                   juce::Colour(0xffd8ecd0));
        setColour(juce::ComboBox::outlineColourId,                juce::Colour(0x886a9a50));
        setColour(juce::ComboBox::arrowColourId,                  juce::Colour(0xff7ab860));
        setColour(juce::ComboBox::focusedOutlineColourId,         juce::Colour(0xff7ab860));
        // Label
        setColour(juce::Label::textColourId,                      juce::Colour(0xffd8ecd0));
        // TextButton — semi-transparent
        setColour(juce::TextButton::buttonColourId,               juce::Colour(0xcc0e1a0c));
        setColour(juce::TextButton::buttonOnColourId,             juce::Colour(0xcc3a6030));
        setColour(juce::TextButton::textColourOffId,              juce::Colour(0xffd8ecd0));
        setColour(juce::TextButton::textColourOnId,               juce::Colour(0xff9ade78));
        // PopupMenu (opaque — floats over DAW, must be readable)
        setColour(juce::PopupMenu::backgroundColourId,            juce::Colour(0xf01a2a18));
        setColour(juce::PopupMenu::textColourId,                  juce::Colour(0xffd8ecd0));
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
        g.setColour(juce::Colour(0x882a4028));
        g.fillRoundedRectangle(track, trackH * 0.5f);
        juce::Rectangle<float> filled;
        if (isHoriz)
            filled = { track.getX(), track.getY(), sliderPos - (float)x, trackH };
        else
            filled = { track.getX(), sliderPos, trackH, track.getBottom() - sliderPos };
        g.setColour(juce::Colour(0xff7ab860));
        g.fillRoundedRectangle(filled, trackH * 0.5f);
        const float thumbR = 6.0f;
        juce::Rectangle<float> thumb;
        if (isHoriz)
            thumb = { sliderPos - thumbR, (float)y + (float)height * 0.5f - thumbR, thumbR * 2.0f, thumbR * 2.0f };
        else
            thumb = { (float)x + (float)width * 0.5f - thumbR, sliderPos - thumbR, thumbR * 2.0f, thumbR * 2.0f };
        g.setColour(juce::Colour(0xff7ab860));
        g.fillEllipse(thumb);
        g.setColour(juce::Colour(0x66000000));
        g.drawEllipse(thumb.reduced(1.5f), 1.0f);
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override
    {
        return juce::FontOptions(13.0f);
    }

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

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour&, bool isHighlighted, bool isDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
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

        g.drawFittedText(button.getButtonText(),
                         button.getLocalBounds().reduced(2),
                         juce::Justification::centred, 1);
    }
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

    juce::Label genreLabel{ {}, "Genre" };
    juce::ComboBox genreCombo;
    juce::Label swingLabel{ {}, "Swing" };
    juce::Slider swingSlider;

    juce::Label songSectionsLabel{ {}, "Sections" };
    juce::Viewport songSectionsViewport;
    std::unique_ptr<SectionListEditor> sectionListEditor;  // editable custom form
    juce::Label sectionLabel;

    juce::Label lockBarsLabel{ {}, "Lock (bars)" };
    juce::Slider lockBarsSlider;
    juce::Label grooveStatusLabel;  // generative lock indicator

    // A5.2: post-lock transition grammar controls + live status.
    juce::Label transitionBarsLabel{ {}, "Transition (bars)" };
    juce::Slider transitionBarsSlider;
    juce::Label transitionSectionsLabel{ {}, "Transition sections" };
    juce::Slider transitionSectionsSlider;
    juce::Label transitionStatusLabel;  // live "Section B · CHORUS · 5/8 bars"

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

    juce::Label bpmLabel;
    juce::Label stateLabel;
    juce::Label patternLabel;
    juce::Label styleLabel;

    FuzzybandLookAndFeel lookAndFeel;

    juce::Image backgroundImage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AccompanimentEditor)
};
