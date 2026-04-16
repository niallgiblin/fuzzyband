#include "AccompanimentEditor.h"
#include "AccompanimentProcessor.h"
#include "analysis/StructureTagger.h"

static const char* stateName(int idx)
{
    switch (static_cast<StructureState>(idx))
    {
        case StructureState::SILENT:
            return "SILENT";
        case StructureState::VERSE:
            return "VERSE";
        case StructureState::CHORUS:
            return "CHORUS";
        case StructureState::BREAKDOWN:
            return "BREAKDOWN";
    }
    return "?";
}

AccompanimentEditor::AccompanimentEditor(AccompanimentProcessor& p)
    : AudioProcessorEditor(&p)
    , audioProcessorRef(p)
{
    setSize(500, 440);

    titleLabel.setText("Metal Accompaniment", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel);

    bpmLabel.setJustificationType(juce::Justification::centredLeft);
    stateLabel.setJustificationType(juce::Justification::centredLeft);
    patternLabel.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(bpmLabel);
    addAndMakeVisible(stateLabel);
    addAndMakeVisible(patternLabel);

    rmsLabel.setJustificationType(juce::Justification::centredLeft);
    centroidLabel.setJustificationType(juce::Justification::centredLeft);
    hfFluxLabel.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(rmsLabel);
    addAndMakeVisible(centroidLabel);
    addAndMakeVisible(hfFluxLabel);

    debugPatternButton.onClick = [this] { audioProcessorRef.bumpDebugPattern(); };
    addAndMakeVisible(debugPatternButton);

    helpLabel.setText(
        "This plug-in does not make sound by itself — it outputs MIDI (drums ch.10, bass ch.2).\n"
        "\n"
        "In Reaper: add a second track with a drum instrument; route MIDI from this track to that "
        "track (track routing / MIDI send).\n"
        "\n"
        "Play guitar here so tempo and structure update. While input is quiet, state stays SILENT "
        "and MIDI stops — use the button above for a 5s preview of the current pattern.\n"
        "\n"
        "Output Gain (Reaper / host parameters) only affects your guitar pass-through, not the drums.",
        juce::dontSendNotification);
    helpLabel.setFont(juce::FontOptions(13.0f));
    helpLabel.setJustificationType(juce::Justification::topLeft);
    helpLabel.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(helpLabel);

    startTimerHz(20);
}

AccompanimentEditor::~AccompanimentEditor() = default;

void AccompanimentEditor::timerCallback()
{
    const float bpm = audioProcessorRef.getDisplayBpm();
    bpmLabel.setText("BPM: " + juce::String(bpm, 1), juce::dontSendNotification);
    stateLabel.setText("State: " + juce::String(stateName(audioProcessorRef.getDisplayStateIndex())), juce::dontSendNotification);
    patternLabel.setText("Pattern: " + juce::String(audioProcessorRef.getDisplayPatternIndex()), juce::dontSendNotification);
    rmsLabel.setText("RMS: " + juce::String(audioProcessorRef.getDisplayRms(), 4), juce::dontSendNotification);
    centroidLabel.setText("Centroid: " + juce::String(audioProcessorRef.getDisplayCentroid(), 0) + " Hz", juce::dontSendNotification);
    hfFluxLabel.setText("HF Flux: " + juce::String(audioProcessorRef.getDisplayHfFlux(), 4), juce::dontSendNotification);
}

void AccompanimentEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::darkgrey.darker(0.2f));
}

void AccompanimentEditor::resized()
{
    auto r = getLocalBounds().reduced(12);
    titleLabel.setBounds(r.removeFromTop(28));
    r.removeFromTop(8);
    bpmLabel.setBounds(r.removeFromTop(22));
    stateLabel.setBounds(r.removeFromTop(22));
    patternLabel.setBounds(r.removeFromTop(22));
    rmsLabel.setBounds(r.removeFromTop(22));
    centroidLabel.setBounds(r.removeFromTop(22));
    hfFluxLabel.setBounds(r.removeFromTop(22));
    r.removeFromTop(12);
    debugPatternButton.setBounds(r.removeFromTop(32));
    r.removeFromTop(10);
    helpLabel.setBounds(r);
}
