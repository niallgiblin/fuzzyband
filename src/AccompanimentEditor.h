#pragma once

/**
 * @file
 * @brief Plugin editor: user policy controls + diagnostic readouts (Phase 14).
 */

#include <JuceHeader.h>

class AccompanimentProcessor;

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
    juce::Label userPolicyHeading;

    juce::Label genreLabel{ {}, "Genre" };
    juce::ComboBox genreCombo;
    juce::Label intensityLabel{ {}, "Intensity" };
    juce::Slider intensitySlider;
    juce::Label variationLabel{ {}, "Variation" };
    juce::Slider variationSlider;
    juce::Label structureBlendLabel{ {}, "Structure: blend ML vs rules" };
    juce::Slider structureBlendSlider;
    juce::Label generativeBassLabel{ {}, "Generative bass" };
    juce::ComboBox generativeBassModeCombo;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> genreAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> intensityAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> variationAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> structureBlendAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> generativeBassAttachment;

    juce::Rectangle<int> userPolicyArea;

    juce::Label bpmLabel;
    juce::Label stateLabel;
    juce::Label patternLabel;
    juce::Label rmsLabel;
    juce::Label centroidLabel;
    juce::Label hfFluxLabel;

    juce::TextButton debugPatternButton{ "Next pattern (preview 5s)" };
    juce::Label helpCaptionLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AccompanimentEditor)
};
