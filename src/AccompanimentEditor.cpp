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
    setSize(520, 600);

    titleLabel.setText("Metal Accompaniment", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel);

    versionLabel.setText(juce::String("v") + ProjectInfo::versionString, juce::dontSendNotification);
    versionLabel.setJustificationType(juce::Justification::centredRight);
    versionLabel.setFont(juce::FontOptions(12.0f));
    versionLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    versionLabel.setTooltip("Plugin version (CMake project VERSION). Rebuild after bumping it in CMakeLists.txt.");
    addAndMakeVisible(versionLabel);

    userPolicyHeading.setText("Accompaniment style", juce::dontSendNotification);
    userPolicyHeading.setFont(juce::FontOptions(12.0f));
    userPolicyHeading.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(userPolicyHeading);

    for (auto* l : { &genreLabel, &intensityLabel, &variationLabel, &structureBlendLabel, &generativeBassLabel })
    {
        l->setJustificationType(juce::Justification::centredLeft);
        l->setFont(juce::FontOptions(14.0f, juce::Font::bold));
    }

    genreCombo.setTooltip("Style preset for pattern policy (saved with project).");
    addAndMakeVisible(genreLabel);
    addAndMakeVisible(genreCombo);

    intensitySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    intensitySlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 18);
    intensitySlider.setRange(0.0, 1.0, 0.001);
    addAndMakeVisible(intensityLabel);
    addAndMakeVisible(intensitySlider);

    variationSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    variationSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 18);
    variationSlider.setRange(0.0, 1.0, 0.001);
    addAndMakeVisible(variationLabel);
    addAndMakeVisible(variationSlider);

    structureBlendSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    structureBlendSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 18);
    structureBlendSlider.setRange(0.0, 1.0, 0.001);
    addAndMakeVisible(structureBlendLabel);
    addAndMakeVisible(structureBlendSlider);

    generativeBassModeCombo.setTooltip("Auto: score-based. On: force when valid. Off: library bass only.");
    addAndMakeVisible(generativeBassLabel);
    addAndMakeVisible(generativeBassModeCombo);

    auto& apvts = audioProcessorRef.getApvts();
    genreAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, "genre", genreCombo);
    intensityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "intensity", intensitySlider);
    variationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "variation", variationSlider);
    structureBlendAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "structureBlend", structureBlendSlider);
    generativeBassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, "generativeBassMode", generativeBassModeCombo);

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

    helpCaptionLabel.setText(
        "MIDI outputs on drum (ch.10) and bass (ch.2) — route this track in your DAW; full steps in README.md and CONTRIBUTING.md.",
        juce::dontSendNotification);
    helpCaptionLabel.setFont(juce::FontOptions(12.0f));
    helpCaptionLabel.setJustificationType(juce::Justification::topLeft);
    helpCaptionLabel.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(helpCaptionLabel);

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
    g.setColour(juce::Colour(0xff323232));
    g.fillRect(userPolicyArea);
}

void AccompanimentEditor::resized()
{
    auto r = getLocalBounds().reduced(12);
    auto titleRow = r.removeFromTop(28);
    versionLabel.setBounds(titleRow.removeFromRight(88));
    titleLabel.setBounds(titleRow);
    r.removeFromTop(8);

    const int userTop = r.getY();
    userPolicyHeading.setBounds(r.removeFromTop(20));
    r.removeFromTop(8);

    auto row = r.removeFromTop(32);
    genreLabel.setBounds(row.removeFromLeft(140));
    genreCombo.setBounds(row);
    r.removeFromTop(8);

    row = r.removeFromTop(28);
    intensityLabel.setBounds(row.removeFromLeft(140));
    intensitySlider.setBounds(row);
    r.removeFromTop(8);

    row = r.removeFromTop(28);
    variationLabel.setBounds(row.removeFromLeft(140));
    variationSlider.setBounds(row);
    r.removeFromTop(8);

    row = r.removeFromTop(28);
    structureBlendLabel.setBounds(row.removeFromLeft(140));
    structureBlendSlider.setBounds(row);
    r.removeFromTop(8);

    row = r.removeFromTop(32);
    generativeBassLabel.setBounds(row.removeFromLeft(140));
    generativeBassModeCombo.setBounds(row);

    const int userBottom = r.getY();
    userPolicyArea = juce::Rectangle<int>(12, userTop, getWidth() - 24, juce::jmax(1, userBottom - userTop));

    r.removeFromTop(16);

    bpmLabel.setBounds(r.removeFromTop(24));
    stateLabel.setBounds(r.removeFromTop(24));
    patternLabel.setBounds(r.removeFromTop(24));
    rmsLabel.setBounds(r.removeFromTop(24));
    centroidLabel.setBounds(r.removeFromTop(24));
    hfFluxLabel.setBounds(r.removeFromTop(24));
    r.removeFromTop(8);
    debugPatternButton.setBounds(r.removeFromTop(32));
    r.removeFromTop(8);
    helpCaptionLabel.setBounds(r);
}
