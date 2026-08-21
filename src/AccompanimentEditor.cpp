#include "AccompanimentEditor.h"
#include "AccompanimentProcessor.h"
#include "analysis/StructureTagger.h"
#include "analysis/StructureSequencer.h"
#include <BinaryData.h>
#include <functional>

// ─── Phase 2: editable custom song form (drag-to-reorder) ─────────────────────
namespace
{

constexpr int kSectionRowH = 32;
constexpr int kSectionListViewH = 280;

/** One row: drag handle + section type + bar count + remove. */
class SectionRow final : public juce::Component
{
public:
    explicit SectionRow(const SongSection& sec)
    {
        grip.setText("::", juce::dontSendNotification);
        grip.setJustificationType(juce::Justification::centred);
        grip.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        grip.setColour(juce::Label::textColourId, juce::Colour(0xff7ab860));
        grip.setTooltip("Drag to reorder");
        grip.setInterceptsMouseClicks(false, false);

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
        barsSlider.setTooltip("Bars in this section");

        removeButton.setButtonText("x");
        removeButton.setTooltip("Remove section");

        addAndMakeVisible(grip);
        addAndMakeVisible(sectionCombo);
        addAndMakeVisible(barsSlider);
        addAndMakeVisible(removeButton);
    }

    SongSection getSection() const
    {
        return { sectionCombo.getText().toStdString(),
                 juce::jlimit(1, 64, static_cast<int>(std::lround(barsSlider.getValue()))) };
    }

    void setOnChange(std::function<void()> cb)
    {
        sectionCombo.onChange = cb;
        barsSlider.onValueChange = std::move(cb);
    }

    void setRemoveCallback(std::function<void()> cb) { removeButton.onClick = std::move(cb); }

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(juce::Colour(0xcc0e1a0c));
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(juce::Colour(0x886a9a50));
        g.drawRoundedRectangle(r, 4.0f, 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(2);
        grip.setBounds(r.removeFromLeft(22));
        removeButton.setBounds(r.removeFromRight(24));
        barsSlider.setBounds(r.removeFromRight(76));
        r.removeFromRight(4);
        sectionCombo.setBounds(r);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        dragStarted = false;
        dragOrigin = e.position;
        juce::ignoreUnused(dragOrigin);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragStarted)
            return;
        if (e.getDistanceFromDragStart() < 6)
            return;
        // Don't steal drags that started on the combo/slider/button.
        if (e.originalComponent != this && e.originalComponent != &grip)
            return;
        if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor(this))
        {
            dragStarted = true;
            c->startDragging("section-row", this);
        }
    }

    juce::Label grip;
    juce::ComboBox sectionCombo;
    juce::Slider barsSlider;
    juce::TextButton removeButton{ "x" };

private:
    bool dragStarted = false;
    juce::Point<float> dragOrigin;
};

} // namespace

/** Editable, ordered list of song sections with drag-and-drop reorder. */
class SectionListEditor final : public juce::Component,
                                public juce::DragAndDropContainer,
                                public juce::DragAndDropTarget
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
            layoutRows();
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
        layoutRows();
    }

    SongForm getForm() const
    {
        SongForm f;
        f.name = "Custom";
        for (const auto* r : rows)
            f.sections.push_back(r->getSection());
        return f;
    }

    int getHeightHint() const noexcept
    {
        return juce::jmax(1, rows.size()) * kSectionRowH + 36;
    }

    void resized() override
    {
        layoutRows();
    }

    bool isInterestedInDragSource(const SourceDetails& d) override
    {
        return d.description.toString() == "section-row";
    }

    void itemDragMove(const SourceDetails& d) override
    {
        dropIndex = indexForY(d.localPosition.y);
        repaint();
    }

    void itemDragExit(const SourceDetails&) override
    {
        dropIndex = -1;
        repaint();
    }

    void itemDropped(const SourceDetails& d) override
    {
        auto* src = dynamic_cast<SectionRow*>(d.sourceComponent.get());
        const int from = rows.indexOf(src);
        int to = indexForY(d.localPosition.y);
        dropIndex = -1;
        if (from < 0 || to < 0)
        {
            repaint();
            return;
        }
        if (to > from)
            --to;
        to = juce::jlimit(0, rows.size() - 1, to);
        if (from != to)
        {
            rows.move(from, to);
            layoutRows();
            notify();
        }
        else
        {
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        if (dropIndex < 0)
            return;
        const int y = dropIndex * kSectionRowH;
        g.setColour(juce::Colour(0xff9ade78));
        g.fillRect(0, y, getWidth(), 2);
    }

private:
    int indexForY(int y) const noexcept
    {
        if (rows.isEmpty())
            return 0;
        const int idx = y / kSectionRowH;
        return juce::jlimit(0, rows.size(), idx);
    }

    void layoutRows()
    {
        auto r = getLocalBounds();
        addButton.setBounds(r.removeFromBottom(28));
        r.removeFromBottom(4);
        for (auto* row : rows)
            row->setBounds(r.removeFromTop(kSectionRowH));
    }

    void wireRow(SectionRow* row)
    {
        row->setOnChange([this] { notify(); });
        row->setRemoveCallback([this, row]
        {
            const int idx = rows.indexOf(row);
            if (idx >= 0)
            {
                rows.remove(idx, true);
                layoutRows();
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
    int dropIndex = -1;
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

    // Snapshot before ComboBoxAttachment/onChange can replace it with a preset.
    const juce::String savedCustomForm = audioProcessorRef.getCustomSongForm();

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

    // Editable section list inside a viewport so the rows are always visible
    // (the previous layout called resized() before this existed, so the list
    // had 0×0 bounds — heading visible, no items).
    sectionListEditor = std::make_unique<SectionListEditor>();
    sectionListEditor->setForm(StructureSequencer::getPresets().front());
    sectionListEditor->setOnChange([this]
    {
        const auto form = sectionListEditor->getForm();
        audioProcessorRef.setCustomSongForm(juce::String(StructureSequencer::serializeForm(form)));
        sectionListEditor->setSize(juce::jmax(1, songSectionsViewport.getMaximumVisibleWidth()),
                                   juce::jmax(kSectionListViewH, sectionListEditor->getHeightHint()));
    });
    songSectionsViewport.setViewedComponent(sectionListEditor.get(), false);
    songSectionsViewport.setScrollBarsShown(true, false);
    songSectionsViewport.setScrollBarThickness(8);
    addAndMakeVisible(songSectionsViewport);

    songFormCombo.onChange = [this]
    {
        const int i = songFormCombo.getSelectedItemIndex();
        const auto& presets = StructureSequencer::getPresets();
        if (i >= 0 && i < static_cast<int>(presets.size()))
        {
            sectionListEditor->setForm(presets[static_cast<size_t>(i)]);
            audioProcessorRef.setCustomSongForm(juce::String(StructureSequencer::serializeForm(presets[static_cast<size_t>(i)])));
            sectionListEditor->setSize(juce::jmax(1, songSectionsViewport.getMaximumVisibleWidth()),
                                       juce::jmax(kSectionListViewH, sectionListEditor->getHeightHint()));
        }
    };

    sectionLabel.setJustificationType(juce::Justification::centredLeft);
    sectionLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    sectionLabel.setColour(juce::Label::textColourId, juce::Colour(0xff6a9a50));
    addAndMakeVisible(sectionLabel);

    playButton.setComponentID("play");
    playButton.setClickingTogglesState(true);
    playButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff4a7a3a));
    playButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffc8d8c0));
    playButton.setTooltip("Play mode: scripted song form. Off = follow/listen (reactive).");
    playButton.onClick = [this]
    {
        audioProcessorRef.playActive.store(playButton.getToggleState(), std::memory_order_release);
        if (playButton.getToggleState() && audioProcessorRef.isRiffCapturing())
            audioProcessorRef.requestRiffCaptureStop();
    };
    addAndMakeVisible(playButton);

    recordRiffButton.setClickingTogglesState(true);
    recordRiffButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff7a3a3a));
    recordRiffButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffecd0d0));
    recordRiffButton.setTooltip("Count-in (1 bar), then play a 4-bar riff to the click. Kick on 1, stick on 2/3/4. Auto-locks drums+bass to that take.");
    recordRiffButton.onClick = [this]
    {
        if (recordRiffButton.getToggleState())
            audioProcessorRef.requestRiffCaptureStart();
        else
            audioProcessorRef.requestRiffCaptureStop();
    };
    addAndMakeVisible(recordRiffButton);

    forgetRiffButton.setTooltip("Clear the recorded or auto-learned riff and return to follow.");
    forgetRiffButton.onClick = [this]
    {
        audioProcessorRef.requestRiffForget();
        recordRiffButton.setToggleState(false, juce::dontSendNotification);
    };
    addAndMakeVisible(forgetRiffButton);

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

    // ── A5.2: post-lock transition grammar controls ─────────────────────────
    transitionBarsLabel.setJustificationType(juce::Justification::centredLeft);
    transitionBarsLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    transitionBarsLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc8d8c0));
    transitionBarsSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    transitionBarsSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 18);
    transitionBarsSlider.setRange(2.0, 32.0, 2.0);
    transitionBarsSlider.setDoubleClickReturnValue(true, 8.0);
    transitionBarsSlider.setTooltip("After the riff lock expires, how many bars each transition section holds before returning to follow.");
    transitionBarsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "transitionBars", transitionBarsSlider);
    addAndMakeVisible(transitionBarsLabel);
    addAndMakeVisible(transitionBarsSlider);

    transitionSectionsLabel.setJustificationType(juce::Justification::centredLeft);
    transitionSectionsLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    transitionSectionsLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc8d8c0));
    transitionSectionsSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    transitionSectionsSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 18);
    transitionSectionsSlider.setRange(1.0, 4.0, 1.0);
    transitionSectionsSlider.setDoubleClickReturnValue(true, 2.0);
    transitionSectionsSlider.setTooltip("How many distinct contrast sections the transition visits (B, C, …) before returning to follow.");
    transitionSectionsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "transitionSections", transitionSectionsSlider);
    addAndMakeVisible(transitionSectionsLabel);
    addAndMakeVisible(transitionSectionsSlider);

    transitionStatusLabel.setJustificationType(juce::Justification::centredLeft);
    transitionStatusLabel.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    transitionStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffe8c070));
    transitionStatusLabel.setText("Section: —", juce::dontSendNotification);
    addAndMakeVisible(transitionStatusLabel);

    // ── DAW-style input scope ───────────────────────────────────────────────
    scopeComponent.setOpaque(false);
    addAndMakeVisible(scopeComponent);

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

    // Restore a persisted custom form AFTER ComboBoxAttachment has seeded the
    // preset combo (which would otherwise overwrite the list via onChange).
    if (savedCustomForm.isNotEmpty())
    {
        sectionListEditor->setForm(StructureSequencer::parseFormString(savedCustomForm.toStdString()));
        audioProcessorRef.setCustomSongForm(savedCustomForm);
    }

    setResizable(true, false);
    setResizeLimits(520, 900, 720, 1800);
    setSize(520, 1280);
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

    // Generative groove-lock / riff-capture status.
    recordRiffButton.setToggleState(audioProcessorRef.isRiffCapturing(), juce::dontSendNotification);
    forgetRiffButton.setEnabled(audioProcessorRef.hasLearnedRiff()
                                || audioProcessorRef.isRiffCapturing()
                                || audioProcessorRef.isGrooveLocked());
    if (audioProcessorRef.isRiffCapturing())
    {
        const int n = audioProcessorRef.getRiffCaptureNoteCount();
        const int bar = audioProcessorRef.getRiffCaptureBar();
        if (bar <= 0)
        {
            recordRiffButton.setButtonText("Count-in…");
            grooveStatusLabel.setText("Groove: COUNT-IN — play on 1", juce::dontSendNotification);
        }
        else
        {
            recordRiffButton.setButtonText("Rec " + juce::String(bar) + "/4 · " + juce::String(n));
            grooveStatusLabel.setText("Groove: RECORDING bar " + juce::String(bar)
                                          + "/4 — " + juce::String(n) + " hits",
                                      juce::dontSendNotification);
        }
        grooveStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffe8c070));
    }
    else if (audioProcessorRef.isLiveGridListening())
    {
        recordRiffButton.setButtonText("Record riff");
        const int bar = audioProcessorRef.getLiveListenBar();
        const int n = audioProcessorRef.getLiveListenNoteCount();
        if (bar <= 0)
            grooveStatusLabel.setText("Groove: listening — play in time with drums",
                                      juce::dontSendNotification);
        else
            grooveStatusLabel.setText("Groove: listening " + juce::String(bar) + "/4 · "
                                          + juce::String(n) + " hits",
                                      juce::dontSendNotification);
        grooveStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffe8c070));
    }
    else if (audioProcessorRef.isGrooveLocked() || audioProcessorRef.hasLearnedRiff())
    {
        recordRiffButton.setButtonText("Record riff");
        // While a riff lock is held (recorded take or live grid listen), show
        // how far through the hold we are and how many bars remain before the
        // transition fires.
        const int cur = audioProcessorRef.getLockBarCurrent();
        const int rem = audioProcessorRef.getLockBarsRemaining();
        const int tot = audioProcessorRef.getLockBarsTotal();
        if (cur > 0 && tot > 0)
            grooveStatusLabel.setText("Groove: LOCKED — bar " + juce::String(cur) + "/"
                                          + juce::String(tot) + " · " + juce::String(rem)
                                          + " left before transition",
                                      juce::dontSendNotification);
        else
            grooveStatusLabel.setText("Groove: LOCKED (riff)", juce::dontSendNotification);
        grooveStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff9ade78));
    }
    else
    {
        recordRiffButton.setButtonText("Record riff");
        grooveStatusLabel.setText("Groove: follow", juce::dontSendNotification);
        grooveStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaacca0));
    }

    // ── A5.2: live post-lock transition status ───────────────────────────────
    if (audioProcessorRef.isTransitionSectionActive())
    {
        const char* secName = audioProcessorRef.getTransitionSectionName();
        const int rem = audioProcessorRef.getTransitionBarsRemaining();
        const int tot = audioProcessorRef.getTransitionBarsTotal();
        const int num = audioProcessorRef.getTransitionSectionNumber();
        // Section letters: 1 → B, 2 → C, 3 → D, …
        const char letter = static_cast<char>('B' + juce::jmax(0, num - 1));
        transitionStatusLabel.setText(
            juce::String("Section ") + letter + " · " + juce::String(secName)
                + " · " + juce::String(rem) + "/" + juce::String(tot) + " bars",
            juce::dontSendNotification);
        transitionStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffe8c070));
    }
    else
    {
        transitionStatusLabel.setText("Section: —", juce::dontSendNotification);
        transitionStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8aaa80));
    }

    // ── DAW-style scope: copy ring + playhead, repaint ───────────────────────
    std::array<float, AccompanimentProcessor::kScopeSize> scopeCopy{};
    audioProcessorRef.copyScopeSamples(scopeCopy.data(), AccompanimentProcessor::kScopeSize);
    scopeComponent.setScopeData(scopeCopy.data(), AccompanimentProcessor::kScopeSize,
                                audioProcessorRef.getPlayheadFraction());
}

void AccompanimentEditor::ScopeComponent::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Panel background — dark frosted panel matching the theme.
    g.setColour(juce::Colour(0xd0081208));
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(juce::Colour(0x886a9a50));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

    const float midY = bounds.getCentreY();
    const float halfH = bounds.getHeight() * 0.42f;

    // Center line.
    g.setColour(juce::Colour(0x556a9a50));
    g.drawLine(bounds.getX(), midY, bounds.getRight(), midY, 1.0f);

    // Waveform: draw each sample as a vertical line mirrored around the center.
    if (count_ > 0)
    {
        const float step = bounds.getWidth() / static_cast<float>(count_);
        juce::Path wave;
        wave.startNewSubPath(bounds.getX(), midY);
        float maxAbs = 0.0001f;
        for (int i = 0; i < count_; ++i)
            maxAbs = juce::jmax(maxAbs, std::abs(samples_[static_cast<size_t>(i)]));
        const float scale = halfH / maxAbs;

        for (int i = 0; i < count_; ++i)
        {
            const float x = bounds.getX() + static_cast<float>(i) * step;
            const float v = samples_[static_cast<size_t>(i)] * scale;
            wave.addLineSegment({ x, midY - v, x, midY }, 1.0f);
        }
        g.setColour(juce::Colour(0xff7ab860));
        g.strokePath(wave, juce::PathStrokeType(1.0f));

        // Soft green fill under the positive half for a "scope glow".
        juce::Path fill;
        fill.startNewSubPath(bounds.getX(), midY);
        for (int i = 0; i < count_; ++i)
        {
            const float x = bounds.getX() + static_cast<float>(i) * step;
            const float v = samples_[static_cast<size_t>(i)] * scale;
            fill.lineTo(x, midY - v);
        }
        fill.lineTo(bounds.getRight(), midY);
        fill.closeSubPath();
        g.setColour(juce::Colour(0x337ab860));
        g.fillPath(fill);
    }

    // Playhead — bright vertical cursor sweeping across the bar (DAW-style).
    const float px = bounds.getX() + bounds.getWidth() * juce::jlimit(0.0f, 1.0f, playheadFraction_);
    g.setColour(juce::Colour(0xff9ade78));
    g.drawLine(px, bounds.getY(), px, bounds.getBottom(), 2.0f);

    // Playhead cap triangle.
    juce::Path cap;
    cap.addTriangle(px, bounds.getY(),
                    px - 4.0f, bounds.getY() + 6.0f,
                    px + 4.0f, bounds.getY() + 6.0f);
    g.fillPath(cap);
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
    titleRow.removeFromRight(6);
    recordRiffButton.setBounds(titleRow.removeFromRight(108));
    titleRow.removeFromRight(6);
    forgetRiffButton.setBounds(titleRow.removeFromRight(64));
    titleRow.removeFromRight(6);
    versionLabel.setBounds(titleRow.removeFromRight(72));
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

    auto listArea = r.removeFromTop(kSectionListViewH);
    songSectionsViewport.setBounds(listArea);
    if (sectionListEditor)
        sectionListEditor->setSize(juce::jmax(1, songSectionsViewport.getMaximumVisibleWidth()),
                                   juce::jmax(listArea.getHeight(), sectionListEditor->getHeightHint()));
    r.removeFromTop(8);

    row = r.removeFromTop(28);
    loopToggle.setBounds(row.removeFromLeft(140));

    row = r.removeFromTop(52);
    lockBarsLabel.setBounds(row.removeFromLeft(140));
    lockBarsSlider.setBounds(row);
    r.removeFromTop(8);

    row = r.removeFromTop(52);
    transitionBarsLabel.setBounds(row.removeFromLeft(140));
    transitionBarsSlider.setBounds(row);
    r.removeFromTop(8);

    row = r.removeFromTop(52);
    transitionSectionsLabel.setBounds(row.removeFromLeft(140));
    transitionSectionsSlider.setBounds(row);
    r.removeFromTop(8);

    const int userBottom = r.getY();
    userPolicyArea = juce::Rectangle<int>(12, userTop, getWidth() - 24, juce::jmax(1, userBottom - userTop));

    r.removeFromTop(16);

    sectionLabel.setBounds(r.removeFromTop(28));
    r.removeFromTop(4);
    grooveStatusLabel.setBounds(r.removeFromTop(24));
    transitionStatusLabel.setBounds(r.removeFromTop(24));
    scopeComponent.setBounds(r.removeFromTop(110));
    r.removeFromTop(8);
    bpmLabel.setBounds(r.removeFromTop(24));
    stateLabel.setBounds(r.removeFromTop(24));
    patternLabel.setBounds(r.removeFromTop(24));
    styleLabel.setBounds(r.removeFromTop(24));
    rmsLabel.setBounds(r.removeFromTop(24));
    centroidLabel.setBounds(r.removeFromTop(24));
    hfFluxLabel.setBounds(r.removeFromTop(24));
    noiseFloorLabel.setBounds(r.removeFromTop(24));
}
