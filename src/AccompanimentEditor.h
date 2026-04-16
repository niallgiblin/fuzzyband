#pragma once

#include <JuceHeader.h>

class AccompanimentProcessor;

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
    juce::Label bpmLabel;
    juce::Label stateLabel;
    juce::Label patternLabel;
    juce::Label rmsLabel;
    juce::Label centroidLabel;
    juce::Label hfFluxLabel;
    juce::TextButton debugPatternButton{ "Next pattern (preview 5s)" };
    juce::Label helpLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AccompanimentEditor)
};
