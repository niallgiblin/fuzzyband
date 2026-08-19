#include "AccompanimentEditor.h"
#include "AccompanimentProcessor.h"
#include "analysis/StructureTagger.h"
#include "analysis/StructureSequencer.h"
#include <BinaryData.h>
#include <functional>

// ─── Phase 2: editable custom song form ───────────────────────────────────────
namespace
{

/** One row of the section list: section type + bar count + move/remove. */
class SectionRow final : public juce::Component
{
public:
    explicit SectionRow(const SongSection& sec)
    {
        for (const char* n : StructureSequencer::sectionNames())
            sectionCombo.addItem(juce::String(n), sectionCombo.getNumItems() + 1);
        sectionCombo.setSelectedId(0, juce::dontSendNotification);
        for (int i = 0; i < sectionCombo.getNumItems(); ++i)
        {
            if (sectionCombo.getItemText(i) == juce::String(sec.name))
            {
                sectionCombo.setSelectedItemIndex(i, juce::dontSendNotification);
                break;
            }
        }

        barsSlider.setSliderStyle(juce::Slider::IncDecButtons);
        barsSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 32, 18);
        barsSlider.setRange(1.0, 64.0, 1.0);
        barsSlider.setValue(static_cast<double>(sec.bars), juce::dontSendNotification);

        upButton.setButtonText("^");
        upButton.setTooltip("Move section up");
        downButton.setButtonText("v");
        downButton.setTooltip("Move section down");
        removeButton.setButtonText("x");
        removeButton.setTooltip("Remove section");

        addAndMakeVisible(sectionCombo);
        addAndMakeVisible(barsSlider);
        addAndMakeVisible(upButton);
        addAndMakeVisible(downButton);
        addAndMakeVisible(removeButton);
    }

    SongSection getSection() const
    {
        return { sectionCombo.getText().toStdString(),
                 juce::jlimit(1, 64, static_cast<int>(std::lround(barsSlider.getValue()))) };
    }

    void setMoveUpCallback(std::function<void()> cb)   { upButton.onClick = std::move(cb); }
    void setMoveDownCallback(std::function<void()> cb) { downButton.onClick = std::move(cb); }
    void setRemoveCallback(std::function<void()> cb)   { removeButton.onClick = std::move(cb); }

    void resized() override
    {
        auto r = getLocalBounds();
        removeButton.setBounds(r.removeFromRight(24));
        downButton.setBounds(r.removeFromRight(24));
        upButton.setBounds(r.removeFromRight(24));
        barsSlider.setBounds(r.removeFromRight(72));
        sectionCombo.setBounds(r);
    }

    juce::ComboBox sectionCombo;
    juce::Slider barsSlider;
    juce::TextButton upButton{ "^" }, downButton{ "v" }, removeButton{ "x" };
};

} // namespace

/** Editable, ordered list of song sections. */
class SectionListEditor final : public juce::Component
{
public:
    SectionListEditor()
    {
        addButton.setButtonText("+ Add section");
        addAndMakeVisible(addButton);
        addButton.onClick = [this]
        {
            rows.add(std::make_unique<SectionRow>(SongSection{ "VERSE", 8 }));
            wireRow(rows.getLast());
            addAndMakeVisible(rows.getLast());
            resized();
            notify();
        };
    }

    void setOnChange(std::function<void()> cb) { onChange = std::move(cb); }

    void setForm(const SongForm& form)
    {
        rows.clear();
        for (const auto& s : form.sections)
        {
            rows.add(std::make_unique<SectionRow>(s));
            wireRow(rows.getLast());
            addAndMakeVisible(rows.getLast());
        }
        resized();
    }

    SongForm getForm() const
    {
        SongForm f;
        f.name = "Custom";
        for (const auto* r : rows)
            f.sections.push_back(r->getSection());
        return f;
    }

    int getHeightHint() const noexcept { return rows.size() * 28 + 32; }

    void resized() override
    {
        auto r = getLocalBounds();
        addButton.setBounds(r.removeFromBottom(24));
        r.removeFromBottom(4);
        for (auto* row : rows)
            row->setBounds(r.removeFromTop(28));
    }

private:
    void wireRow(SectionRow* row)
    {
        // Capture the row pointer and look up its index at click time, so the
        // callbacks stay correct after swaps/removals reorder the list.
        row->setMoveUpCallback([this, row]
        {
            const int idx = rows.indexOf(row);
            if (idx > 0)
            {
                rows.swap(idx, idx - 1);
                resized();
                notify();
            }
        });
        row->setMoveDownCallback([this, row]
        {
            const int idx = rows.indexOf(row);
            if (idx < rows.size() - 1)
            {
                rows.swap(idx, idx + 1);
                resized();
                notify();
            }
        });
        row->setRemoveCallback([this, row]
        {
            const int idx = rows.indexOf(row);
            if (idx >= 0)
            {
                rows.remove(idx, true);
                resized();
                notify();
            }
        });
    }

    void notify()
    {
        if (onChange)
            onChange();
    }

    juce::OwnedArray<SectionRow> rows;
    juce::TextButton addButton{ "+ Add section" };
    std::function<void()> onChange;
};

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

    setSize(520, 980);

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

    genreLabel.setJustificationType(juce::Justification::centredLeft);
    genreLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    genreLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc8d8c0));
    for (int i = 0; i < Groove::presetCount(); ++i)
        genreCombo.addItem(Groove::presetFor(i).name, genreCombo.getNumItems() + 1);
    genreCombo.setTooltip("Genre preset: groove feel, velocity profile, section dynamics (B1). Rock is the default; metal remains a preset.");
    genreCombo.onChange = [this]
    {
        // Applying a genre also applies its default swing to the swing knob.
        const int g = genreCombo.getSelectedItemIndex();
        if (g >= 0 && g < Groove::presetCount())
        {
            if (auto* swingParam = dynamic_cast<juce::AudioParameterFloat*>(
                    audioProcessorRef.getApvts().getParameter("swing")))
                *swingParam = Groove::presetFor(g).defaultSwing;
        }
    };
    addAndMakeVisible(genreLabel);
    addAndMakeVisible(genreCombo);

    swingLabel.setJustificationType(juce::Justification::centredLeft);
    swingLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    swingLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc8d8c0));
    swingSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    swingSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 18);
    swingSlider.setRange(0.0, 1.0, 0.01);
    swingSlider.setDoubleClickReturnValue(true, 0.0);
    swingSlider.setTooltip("Swing/shuffle ratio: delays off-8th drum events (0-100%).");
    addAndMakeVisible(swingLabel);
    addAndMakeVisible(swingSlider);

    bassOctaveLabel.setJustificationType(juce::Justification::centredLeft);
    bassOctaveLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    bassOctaveLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc8d8c0));
    bassOctaveCombo.addItem("-12 (down)", 1);
    bassOctaveCombo.addItem("0 (normal)", 2);
    bassOctaveCombo.addItem("+12 (up)", 3);
    bassOctaveCombo.setTooltip("Shift the bass MIDI an octave. MIDI 36 = C2 in standard pitch; some bass VSTs display it as C1 and can't sound below ~E2 — shift up to fit the instrument's range.");
    addAndMakeVisible(bassOctaveLabel);
    addAndMakeVisible(bassOctaveCombo);

    songFormLabel.setJustificationType(juce::Justification::centredLeft);
    songFormLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    songFormLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc8d8c0));
    for (const auto& preset : StructureSequencer::getPresets())
        songFormCombo.addItem(preset.name, songFormCombo.getNumItems() + 1);
    songFormCombo.setTooltip("Select a preset form (editable below). Sections advance on bar boundaries.");
    addAndMakeVisible(songFormLabel);
    addAndMakeVisible(songFormCombo);

    songSectionsLabel.setJustificationType(juce::Justification::centredLeft);
    songSectionsLabel.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    songSectionsLabel.setColour(juce::Label::textColourId, juce::Colour(0xff9ade78));
    addAndMakeVisible(songSectionsLabel);

    // Phase 2: editable section list. Editing it writes the custom form back to
    // the processor (which persists it); picking a preset seeds the list.
    sectionListEditor = std::make_unique<SectionListEditor>();
    sectionListEditor->setForm(StructureSequencer::getPresets().front());
    sectionListEditor->setOnChange([this]
    {
        const auto form = sectionListEditor->getForm();
        audioProcessorRef.setCustomSongForm(juce::String(StructureSequencer::serializeForm(form)));
    });
    addAndMakeVisible(sectionListEditor.get());
    songFormCombo.onChange = [this]
    {
        const int i = songFormCombo.getSelectedItemIndex();
        const auto& presets = StructureSequencer::getPresets();
        if (i >= 0 && i < static_cast<int>(presets.size()))
        {
            sectionListEditor->setForm(presets[static_cast<size_t>(i)]);
            audioProcessorRef.setCustomSongForm(juce::String(StructureSequencer::serializeForm(presets[static_cast<size_t>(i)])));
        }
    };

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
    genreAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, "genre", genreCombo);
    swingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "swing", swingSlider);
    bassOctaveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, "bassTranspose", bassOctaveCombo);
    songFormAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, "songForm", songFormCombo);
    loopAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, "loop", loopToggle);

    loopToggle.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff6a9a50));
    addAndMakeVisible(loopToggle);

    // Generative groove lock: hold length + live status indicator.
    lockBarsLabel.setJustificationType(juce::Justification::centredLeft);
    lockBarsLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    lockBarsLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc8d8c0));
    lockBarsSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    lockBarsSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 18);
    lockBarsSlider.setRange(4.0, 64.0, 4.0);
    lockBarsSlider.setDoubleClickReturnValue(true, 16.0);
    lockBarsSlider.setTooltip("Generative mode: how many bars the auto-lock holds after you stop playing the riff (returning to the riff extends it).");
    lockBarsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "lockBars", lockBarsSlider);
    addAndMakeVisible(lockBarsLabel);
    addAndMakeVisible(lockBarsSlider);

    grooveStatusLabel.setJustificationType(juce::Justification::centredLeft);
    grooveStatusLabel.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    grooveStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaacca0));
    grooveStatusLabel.setText("Groove: follow", juce::dontSendNotification);
    addAndMakeVisible(grooveStatusLabel);

    for (auto* l : { &bpmLabel, &stateLabel, &patternLabel, &styleLabel, &rmsLabel, &centroidLabel, &hfFluxLabel, &noiseFloorLabel })
    {
        l->setJustificationType(juce::Justification::centredLeft);
        l->setFont(juce::FontOptions(11.0f));
        l->setColour(juce::Label::textColourId, juce::Colour(0xffaacca0));
    }

    addAndMakeVisible(bpmLabel);
    addAndMakeVisible(stateLabel);
    addAndMakeVisible(patternLabel);
    addAndMakeVisible(styleLabel);
    addAndMakeVisible(rmsLabel);
    addAndMakeVisible(centroidLabel);
    addAndMakeVisible(hfFluxLabel);
    addAndMakeVisible(noiseFloorLabel);

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

    static const char* kStyleNames[] = {"Palm Mute", "Open Chord", "Single Note", "Sustain", "Silence"};
    const int si = juce::jlimit(0, 4, audioProcessorRef.getDisplayStyle());
    styleLabel.setText("Style: " + juce::String(kStyleNames[si]), juce::dontSendNotification);

    rmsLabel.setText("RMS: " + juce::String(audioProcessorRef.getDisplayRms(), 4), juce::dontSendNotification);
    centroidLabel.setText("Centroid: " + juce::String(audioProcessorRef.getDisplayCentroid(), 0) + " Hz", juce::dontSendNotification);
    hfFluxLabel.setText("HF Flux: " + juce::String(audioProcessorRef.getDisplayHfFlux(), 4), juce::dontSendNotification);
    noiseFloorLabel.setText("Noise floor: " + juce::String(audioProcessorRef.getDisplayNoiseFloor(), 4), juce::dontSendNotification);

    // Generative groove-lock status (riff repeat → drums+bass frozen).
    if (audioProcessorRef.isGrooveLocked())
    {
        grooveStatusLabel.setText("Groove: LOCKED (riff)", juce::dontSendNotification);
        grooveStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff9ade78));
    }
    else
    {
        grooveStatusLabel.setText("Groove: follow", juce::dontSendNotification);
        grooveStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaacca0));
    }
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
    genreLabel.setBounds(row.removeFromLeft(140));
    genreCombo.setBounds(row);
    r.removeFromTop(8);

    row = r.removeFromTop(52);
    swingLabel.setBounds(row.removeFromLeft(140));
    swingSlider.setBounds(row);
    r.removeFromTop(8);

    row = r.removeFromTop(52);
    bassOctaveLabel.setBounds(row.removeFromLeft(140));
    bassOctaveCombo.setBounds(row);
    r.removeFromTop(8);

    row = r.removeFromTop(52);
    songFormLabel.setBounds(row.removeFromLeft(140));
    songFormCombo.setBounds(row);
    r.removeFromTop(8);

    songSectionsLabel.setBounds(r.removeFromTop(18));
    r.removeFromTop(2);

    // Phase 2: editable section list (height tracks the number of rows).
    // Guard for the early resized() that setSize() fires in the constructor,
    // before sectionListEditor has been created.
    if (sectionListEditor)
        sectionListEditor->setBounds(r.removeFromTop(sectionListEditor->getHeightHint()));
    r.removeFromTop(8);

    row = r.removeFromTop(28);
    loopToggle.setBounds(row.removeFromLeft(140));

    row = r.removeFromTop(52);
    lockBarsLabel.setBounds(row.removeFromLeft(140));
    lockBarsSlider.setBounds(row);
    r.removeFromTop(8);

    const int userBottom = r.getY();
    userPolicyArea = juce::Rectangle<int>(12, userTop, getWidth() - 24, juce::jmax(1, userBottom - userTop));

    r.removeFromTop(16);

    sectionLabel.setBounds(r.removeFromTop(28));
    r.removeFromTop(4);
    grooveStatusLabel.setBounds(r.removeFromTop(24));
    bpmLabel.setBounds(r.removeFromTop(24));
    stateLabel.setBounds(r.removeFromTop(24));
    patternLabel.setBounds(r.removeFromTop(24));
    styleLabel.setBounds(r.removeFromTop(24));
    rmsLabel.setBounds(r.removeFromTop(24));
    centroidLabel.setBounds(r.removeFromTop(24));
    hfFluxLabel.setBounds(r.removeFromTop(24));
    noiseFloorLabel.setBounds(r.removeFromTop(24));
}
