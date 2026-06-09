#include "AccompanimentEditor.h"
#include "AccompanimentProcessor.h"
#include "analysis/StructureTagger.h"

static const char* stateName(int idx)
{
    switch (static_cast<StructureState>(idx))
    {
        case StructureState::SILENT:
            return "SILENT";
        case StructureState::AMBIENT:
            return "AMBIENT";
        case StructureState::SOFT:
            return "SOFT";
        case StructureState::LOUD:
            return "LOUD";
        case StructureState::BREAKDOWN:
            return "BREAKDOWN";
    }
    return "?";
}

AccompanimentEditor::AccompanimentEditor(AccompanimentProcessor& p)
    : AudioProcessorEditor(&p)
    , audioProcessorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    setSize(520, 640);

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

    userPolicyHeading.setText("Controls", juce::dontSendNotification);
    userPolicyHeading.setFont(juce::FontOptions(11.0f));
    userPolicyHeading.setColour(juce::Label::textColourId, juce::Colour(0xff8aaa80));
    userPolicyHeading.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(userPolicyHeading);

    for (auto* l : { &intensityLabel, &structureBlendLabel, &generativeBassLabel })
    {
        l->setJustificationType(juce::Justification::centredLeft);
        l->setFont(juce::FontOptions(14.0f, juce::Font::bold));
        l->setColour(juce::Label::textColourId, juce::Colour(0xffc8d8c0));
    }

    intensitySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    intensitySlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 18);
    intensitySlider.setRange(0.0, 1.0, 0.001);
    addAndMakeVisible(intensityLabel);
    addAndMakeVisible(intensitySlider);

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

    bpmSliderLabel.setJustificationType(juce::Justification::centredLeft);
    bpmSliderLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    bpmSliderLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc8d8c0));
    bpmSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    bpmSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
    bpmSlider.setRange(40.0, 300.0, 1.0);
    bpmSlider.setDoubleClickReturnValue(true, 120.0);
    bpmSlider.setTooltip("Set playback tempo (40-300 BPM). Saves with session.");
    addAndMakeVisible(bpmSliderLabel);
    addAndMakeVisible(bpmSlider);

    songFormLabel.setJustificationType(juce::Justification::centredLeft);
    songFormLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    songFormLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc8d8c0));
    for (const auto& preset : StructureSequencer::getPresets())
        songFormCombo.addItem(preset.name, songFormCombo.getNumItems() + 1);
    songFormCombo.setTooltip("Select song structure preset. Sections advance on bar boundaries.");
    addAndMakeVisible(songFormLabel);
    addAndMakeVisible(songFormCombo);

    sectionLabel.setJustificationType(juce::Justification::centredLeft);
    sectionLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    sectionLabel.setColour(juce::Label::textColourId, juce::Colour(0xff6a9a50));
    addAndMakeVisible(sectionLabel);

    playButton.setClickingTogglesState(true);
    playButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff4a7a3a));
    playButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffc8d8c0));
    playButton.onClick = [this]
    {
        audioProcessorRef.playActive.store(playButton.getToggleState(), std::memory_order_release);
    };
    addAndMakeVisible(playButton);

    auto& apvts = audioProcessorRef.getApvts();
    intensityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "intensity", intensitySlider);
    structureBlendAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "structureBlend", structureBlendSlider);
    generativeBassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, "generativeBassMode", generativeBassModeCombo);
    bpmAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "bpm", bpmSlider);
    songFormAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, "songForm", songFormCombo);

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

    inferenceLabel.setJustificationType(juce::Justification::centredLeft);
    inferenceLabel.setFont(juce::FontOptions(14.0f, juce::Font::plain));
    inferenceLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(inferenceLabel);

    debugPatternButton.onClick = [this] { audioProcessorRef.bumpDebugPattern(); };
    addAndMakeVisible(debugPatternButton);

    captureFeaturesButton.setButtonText("Capture features");
    captureFeaturesButton.setClickingTogglesState(true);
    captureFeaturesButton.onClick = [this]
    {
        audioProcessorRef.setFeatureCaptureEnabled(captureFeaturesButton.getToggleState());
    };
    addAndMakeVisible(captureFeaturesButton);

    captureStatusLabel.setJustificationType(juce::Justification::centredLeft);
    captureStatusLabel.setFont(juce::FontOptions(11.0f));
    captureStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8aaa80));
    captureStatusLabel.setMinimumHorizontalScale(0.75f);
    addAndMakeVisible(captureStatusLabel);

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
    sectionLabel.setText(audioProcessorRef.getSectionName(), juce::dontSendNotification);
    playButton.setToggleState(audioProcessorRef.playActive.load(std::memory_order_acquire), juce::dontSendNotification);
    const float bpm = audioProcessorRef.getDisplayBpm();
    bpmLabel.setText("BPM: " + juce::String(bpm, 1), juce::dontSendNotification);
    stateLabel.setText("State: " + juce::String(stateName(audioProcessorRef.getDisplayStateIndex())), juce::dontSendNotification);
    patternLabel.setText("Pattern: " + juce::String(audioProcessorRef.getDisplayPatternIndex()), juce::dontSendNotification);
    rmsLabel.setText("RMS: " + juce::String(audioProcessorRef.getDisplayRms(), 4), juce::dontSendNotification);
    centroidLabel.setText("Centroid: " + juce::String(audioProcessorRef.getDisplayCentroid(), 0) + " Hz", juce::dontSendNotification);
    hfFluxLabel.setText("HF Flux: " + juce::String(audioProcessorRef.getDisplayHfFlux(), 4), juce::dontSendNotification);

    const juce::String inferenceName(audioProcessorRef.getActiveInferenceName());
    const auto onnxErrors = audioProcessorRef.getOnnxErrorCount();
    juce::String inferenceText = "Inference: " + inferenceName;
    if (onnxErrors > 0 || inferenceName.containsIgnoreCase("Onnx"))
        inferenceText += " | ONNX errors: " + juce::String(static_cast<juce::int64>(onnxErrors));
    inferenceLabel.setText(inferenceText, juce::dontSendNotification);
    captureFeaturesButton.setToggleState(audioProcessorRef.isFeatureCaptureEnabled(), juce::dontSendNotification);
    const auto path = juce::File(audioProcessorRef.getFeatureCapturePath()).getFileName();
    const auto fileText = path.isEmpty() ? juce::String("no file") : path;
    captureStatusLabel.setText(
        juce::String(audioProcessorRef.isFeatureCaptureEnabled() ? "Capture: on" : "Capture: off")
            + " | dropped: " + juce::String(static_cast<juce::int64>(audioProcessorRef.getFeatureCaptureDroppedRows()))
            + " | " + fileText,
        juce::dontSendNotification);
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
    playButton.setBounds(titleRow.removeFromRight(100));
    titleRow.removeFromRight(8);
    versionLabel.setBounds(titleRow.removeFromRight(88));
    titleLabel.setBounds(titleRow);
    r.removeFromTop(8);

    const int userTop = r.getY();
    userPolicyHeading.setBounds(r.removeFromTop(20));
    r.removeFromTop(8);

    auto row = r.removeFromTop(52);
    intensityLabel.setBounds(row.removeFromLeft(140));
    intensitySlider.setBounds(row);
    r.removeFromTop(8);

    row = r.removeFromTop(52);
    structureBlendLabel.setBounds(row.removeFromLeft(140));
    structureBlendSlider.setBounds(row);
    r.removeFromTop(8);

    row = r.removeFromTop(60);
    generativeBassLabel.setBounds(row.removeFromLeft(140));
    generativeBassModeCombo.setBounds(row);
    r.removeFromTop(8);

    row = r.removeFromTop(72);
    bpmSliderLabel.setBounds(row.removeFromLeft(140));
    bpmSlider.setBounds(row);
    r.removeFromTop(8);

    row = r.removeFromTop(52);
    songFormLabel.setBounds(row.removeFromLeft(140));
    songFormCombo.setBounds(row);

    const int userBottom = r.getY();
    userPolicyArea = juce::Rectangle<int>(12, userTop, getWidth() - 24, juce::jmax(1, userBottom - userTop));

    r.removeFromTop(16);

    sectionLabel.setBounds(r.removeFromTop(28));
    r.removeFromTop(4);
    bpmLabel.setBounds(r.removeFromTop(24));
    stateLabel.setBounds(r.removeFromTop(24));
    patternLabel.setBounds(r.removeFromTop(24));
    rmsLabel.setBounds(r.removeFromTop(24));
    centroidLabel.setBounds(r.removeFromTop(24));
    hfFluxLabel.setBounds(r.removeFromTop(24));
    r.removeFromTop(8);
    debugPatternButton.setBounds(r.removeFromTop(32));
    r.removeFromTop(8);
    captureFeaturesButton.setBounds(r.removeFromTop(28));
    captureStatusLabel.setBounds(r.removeFromTop(24));
    r.removeFromTop(8);
    inferenceLabel.setBounds(r.removeFromTop(20));
    r.removeFromTop(4);
    helpCaptionLabel.setBounds(r);
}
