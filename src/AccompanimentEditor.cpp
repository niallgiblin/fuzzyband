#include "AccompanimentEditor.h"
#include "AccompanimentProcessor.h"
#include "analysis/StructureTagger.h"

static const char* stateName(int idx)
{
    switch (static_cast<StructureState>(idx))
    {
        case StructureState::SILENT:
            return "SILENT";
        case StructureState::SOFT:
            return "SOFT";
        case StructureState::LOUD:
            return "LOUD";
    }
    return "?";
}

AccompanimentEditor::AccompanimentEditor(AccompanimentProcessor& p)
    : AudioProcessorEditor(&p)
    , audioProcessorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    setSize(520, 600);

    titleLabel.setText("fuzzyband", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc8d8c0));
    addAndMakeVisible(titleLabel);

    versionLabel.setText(juce::String("v") + ProjectInfo::versionString, juce::dontSendNotification);
    versionLabel.setJustificationType(juce::Justification::centredRight);
    versionLabel.setFont(juce::FontOptions(12.0f));
    versionLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8aaa80));
    versionLabel.setTooltip("Plugin version (CMake project VERSION). Rebuild after bumping it in CMakeLists.txt.");
    addAndMakeVisible(versionLabel);

    userPolicyHeading.setText("style", juce::dontSendNotification);
    userPolicyHeading.setFont(juce::FontOptions(11.0f));
    userPolicyHeading.setColour(juce::Label::textColourId, juce::Colour(0xff8aaa80));
    userPolicyHeading.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(userPolicyHeading);

    for (auto* l : { &genreLabel, &intensityLabel, &variationLabel, &structureBlendLabel, &generativeBassLabel })
    {
        l->setJustificationType(juce::Justification::centredLeft);
        l->setFont(juce::FontOptions(14.0f, juce::Font::bold));
        l->setColour(juce::Label::textColourId, juce::Colour(0xffc8d8c0));
    }

    genreCombo.addItem("Metal",      1);
    genreCombo.addItem("Metalcore",  2);
    genreCombo.addItem("Death",      3);
    genreCombo.addItem("Progressive",4);
    genreCombo.addItem("Black",      5);
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

    generativeBassModeCombo.addItem("Auto", 1);
    generativeBassModeCombo.addItem("On",   2);
    generativeBassModeCombo.addItem("Off",  3);
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

    for (auto* l : { &bpmLabel, &stateLabel, &patternLabel, &rmsLabel, &centroidLabel, &hfFluxLabel })
    {
        l->setJustificationType(juce::Justification::centredLeft);
        l->setFont(juce::FontOptions(11.0f));
        l->setColour(juce::Label::textColourId, juce::Colour(0xff8aaa80));
    }

    addAndMakeVisible(bpmLabel);
    addAndMakeVisible(stateLabel);
    addAndMakeVisible(patternLabel);
    addAndMakeVisible(rmsLabel);
    addAndMakeVisible(centroidLabel);
    addAndMakeVisible(hfFluxLabel);

    debugPatternButton.onClick = [this] { audioProcessorRef.bumpDebugPattern(); };
    addAndMakeVisible(debugPatternButton);

    helpCaptionLabel.setText(
        "MIDI outputs on drum (ch.10) and bass (ch.2) — route this track in your DAW; full steps in README.md and CONTRIBUTING.md.",
        juce::dontSendNotification);
    helpCaptionLabel.setFont(juce::FontOptions(11.0f));
    helpCaptionLabel.setColour(juce::Label::textColourId, juce::Colour(0xff4a6448));
    helpCaptionLabel.setJustificationType(juce::Justification::topLeft);
    helpCaptionLabel.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(helpCaptionLabel);

    startTimerHz(20);
}

AccompanimentEditor::~AccompanimentEditor()
{
    setLookAndFeel(nullptr);
}

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
    // Background
    g.fillAll(juce::Colour(0xff111a10));

    // Control panel rounded rect
    g.setColour(juce::Colour(0xff1a2a18));
    g.fillRoundedRectangle(userPolicyArea.toFloat(), 6.0f);
    g.setColour(juce::Colour(0xff2a4028));
    g.drawRoundedRectangle(userPolicyArea.toFloat().reduced(0.5f), 6.0f, 1.0f);

    // Separator between controls and diagnostics
    const int sepY = userPolicyArea.getBottom() + 10;
    g.setColour(juce::Colour(0xff2a4028));
    g.drawLine(12.0f, (float)sepY, (float)getWidth() - 12.0f, (float)sepY, 1.0f);
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
