#include "AccompanimentEditor.h"
#include "AccompanimentProcessor.h"
#include "analysis/StructureTagger.h"
#include <BinaryData.h>

static const char* stateName(int idx)
{
    switch (static_cast<StructureState>(idx))
    {
        case StructureState::SILENT: return "SILENT";
        case StructureState::SOFT:   return "SOFT";
        case StructureState::LOUD:   return "LOUD";
    }
    return "?";
}

AccompanimentEditor::AccompanimentEditor(AccompanimentProcessor& p)
    : AudioProcessorEditor(&p)
    , audioProcessorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    backgroundImage = juce::ImageCache::getFromMemory(
        BinaryData::forest_png, BinaryData::forest_pngSize);

    setSize(520, 780);

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
    bpmAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "bpm", bpmSlider);
    songFormAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, "songForm", songFormCombo);
    loopAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, "loop", loopToggle);

    loopToggle.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff6a9a50));
    addAndMakeVisible(loopToggle);

    for (auto* l : { &bpmLabel, &stateLabel, &patternLabel, &rmsLabel, &centroidLabel, &hfFluxLabel })
    {
        l->setJustificationType(juce::Justification::centredLeft);
        l->setFont(juce::FontOptions(11.0f));
        l->setColour(juce::Label::textColourId, juce::Colour(0xffaacca0));
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
}

void AccompanimentEditor::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // ── 1. Background image, scaled to fill ──────────────────────────────────
    if (backgroundImage.isValid())
    {
        g.drawImage(backgroundImage,
                    bounds,
                    juce::RectanglePlacement::fillDestination);
    }
    else
    {
        g.fillAll(juce::Colour(0xff111a10));
    }

    // ── 2. Dark vignette overlay — keeps text readable ────────────────────────
    g.setColour(juce::Colour(0xbb050f04));
    g.fillAll();

    // ── 3. Control panel — semi-transparent frosted-glass panel ──────────────
    g.setColour(juce::Colour(0xb0081208));
    g.fillRoundedRectangle(userPolicyArea.toFloat(), 6.0f);
    g.setColour(juce::Colour(0x886a9a50));
    g.drawRoundedRectangle(userPolicyArea.toFloat().reduced(0.5f), 6.0f, 1.0f);

    // ── 4. Thin separator between controls and diagnostics ────────────────────
    const int sepY = userPolicyArea.getBottom() + 10;
    g.setColour(juce::Colour(0x556a9a50));
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
    row = r.removeFromTop(72);
    bpmSliderLabel.setBounds(row.removeFromLeft(140));
    bpmSlider.setBounds(row);
    r.removeFromTop(8);

    row = r.removeFromTop(52);
    songFormLabel.setBounds(row.removeFromLeft(140));
    songFormCombo.setBounds(row);
    r.removeFromTop(8);

    row = r.removeFromTop(28);
    loopToggle.setBounds(row.removeFromLeft(140));

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
    inferenceLabel.setBounds(r.removeFromTop(20));
    r.removeFromTop(4);
    helpCaptionLabel.setBounds(r);
}
