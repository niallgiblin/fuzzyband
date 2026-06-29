#pragma once

/**
 * @file
 * @brief Plugin editor: user policy controls + diagnostic readouts (Phase 14).
 */

#include <JuceHeader.h>

class AccompanimentProcessor;

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

    void drawComboBox(juce::Graphics& g, int width, int height, bool,
                      int, int, int, int, juce::ComboBox&) override
    {
        auto bounds = juce::Rectangle<float>(0.0f, 0.0f, (float)width, (float)height);
        g.setColour(findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(findColour(juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);
        const float arrowX = (float)width - 16.0f;
        const float arrowY = (float)height * 0.5f;
        juce::Path arrow;
        arrow.addTriangle(arrowX, arrowY - 3.0f, arrowX + 7.0f, arrowY - 3.0f, arrowX + 3.5f, arrowY + 3.0f);
        g.setColour(findColour(juce::ComboBox::arrowColourId));
        g.fillPath(arrow);
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

    juce::Label bpmSliderLabel{ {}, "Tempo (BPM)" };
    juce::Slider bpmSlider;

    juce::Label songFormLabel{ {}, "Song form" };
    juce::ComboBox songFormCombo;
    juce::Label sectionLabel;
    juce::ToggleButton loopToggle{ "Loop" };

    juce::TextButton playButton{ "▶ PLAY" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bpmAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> songFormAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> loopAttachment;

    juce::Rectangle<int> userPolicyArea;

    juce::Label bpmLabel;
    juce::Label stateLabel;
    juce::Label patternLabel;
    juce::Label rmsLabel;
    juce::Label centroidLabel;
    juce::Label hfFluxLabel;

    juce::Label inferenceLabel;

    juce::Label helpCaptionLabel;

    FuzzybandLookAndFeel lookAndFeel;

    juce::Image backgroundImage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AccompanimentEditor)
};
