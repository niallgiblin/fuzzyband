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

/** One row: drag handle + section type + bar count + remove. */
class SectionRow final : public juce::Component
{
public:
    explicit SectionRow(const SongSection& sec)
    {
        grip.setText("::", juce::dontSendNotification);
        grip.setJustificationType(juce::Justification::centred);
        grip.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        grip.setColour(juce::Label::textColourId, juce::Colour(FuzzybandPalette::moss));
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
        removeButton.setComponentID("sect-remove");
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
        // rows + small gap + the 'Add section' button, which now sits directly
        // beneath the last row (so it scrolls with a long arrangement).
        return juce::jmax(1, rows.size()) * kSectionRowH + 4 + 28;
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
        for (auto* row : rows)
            row->setBounds(r.removeFromTop(kSectionRowH));
        r.removeFromTop(4);
        addButton.setBounds(r.removeFromTop(28));
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

    // Install the bundled OFL typefaces (warm humanist Alegreya Sans for UI,
    // IBM Plex Mono for the numeric/status readouts). Must happen before any
    // label font is applied so the helpers below have a typeface to build on.
    lookAndFeel.installFonts(
        juce::Typeface::createSystemTypefaceFor(BinaryData::AlegreyaSansRegular_ttf, BinaryData::AlegreyaSansRegular_ttfSize),
        juce::Typeface::createSystemTypefaceFor(BinaryData::AlegreyaSansMedium_ttf, BinaryData::AlegreyaSansMedium_ttfSize),
        juce::Typeface::createSystemTypefaceFor(BinaryData::AlegreyaSansBold_ttf, BinaryData::AlegreyaSansBold_ttfSize),
        juce::Typeface::createSystemTypefaceFor(BinaryData::IBMPlexMonoRegular_ttf, BinaryData::IBMPlexMonoRegular_ttfSize),
        juce::Typeface::createSystemTypefaceFor(BinaryData::IBMPlexMonoMedium_ttf, BinaryData::IBMPlexMonoMedium_ttfSize));

    backgroundImage = juce::ImageCache::getFromMemory(
        BinaryData::forest_png, BinaryData::forest_pngSize);

    // Snapshot before ComboBoxAttachment/onChange can replace it with a preset.
    const juce::String savedCustomForm = audioProcessorRef.getCustomSongForm();

    titleLabel.setText("fuzzyband", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setFont(lookAndFeel.displayFont(24.0f));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(FuzzybandPalette::moss));
    addAndMakeVisible(titleLabel);

    versionLabel.setText(juce::String("v") + ProjectInfo::versionString, juce::dontSendNotification);
    versionLabel.setJustificationType(juce::Justification::centredRight);
    versionLabel.setFont(lookAndFeel.monoFont(11.0f));
    versionLabel.setColour(juce::Label::textColourId, juce::Colour(FuzzybandPalette::inkMuted));
    versionLabel.setTooltip("Plugin version (CMake project VERSION). Rebuild after bumping it in CMakeLists.txt.");
    addAndMakeVisible(versionLabel);

    userPolicyHeading.setText("CONTROLS", juce::dontSendNotification);
    userPolicyHeading.setFont(lookAndFeel.labelFont(11.0f));
    userPolicyHeading.setColour(juce::Label::textColourId, juce::Colour(FuzzybandPalette::inkMuted));
    userPolicyHeading.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(userPolicyHeading);

    genreLabel.setJustificationType(juce::Justification::centredLeft);
    genreLabel.setFont(lookAndFeel.labelFont(12.0f));
    genreLabel.setColour(juce::Label::textColourId, juce::Colour(FuzzybandPalette::ink));
    for (int i = 0; i < Groove::presetCount(); ++i)
        genreCombo.addItem(Groove::presetFor(i).name, genreCombo.getNumItems() + 1);
    genreCombo.setTooltip("Genre preset: groove feel, velocity profile, section dynamics (B1). Rock is the default; metal and rock subgenres are presets.");
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
    swingLabel.setFont(lookAndFeel.labelFont(12.0f));
    swingLabel.setColour(juce::Label::textColourId, juce::Colour(FuzzybandPalette::ink));
    swingSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    swingSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 18);
    swingSlider.setRange(0.0, 1.0, 0.01);
    swingSlider.setDoubleClickReturnValue(true, 0.0);
    swingSlider.setTooltip("Swing/shuffle ratio: delays off-8th drum events (0-100%).");
    addAndMakeVisible(swingLabel);
    addAndMakeVisible(swingSlider);

    songSectionsLabel.setJustificationType(juce::Justification::centredLeft);
    songSectionsLabel.setFont(lookAndFeel.labelFont(13.0f));
    songSectionsLabel.setColour(juce::Label::textColourId, juce::Colour(FuzzybandPalette::moss));
    addAndMakeVisible(songSectionsLabel);

    // Editable section list inside a viewport so the rows are always visible
    // (the previous layout called resized() before this existed, so the list
    // had 0×0 bounds — heading visible, no items).
    // Default custom form: INTRO → VERSE → CHORUS → VERSE → CHORUS → OUTRO
    // (single dropdown removed; the editable section list is the song form).
    sectionListEditor = std::make_unique<SectionListEditor>();
    sectionListEditor->setForm(SongForm{ "Custom",
        { SongSection{ "INTRO", 4 },
          SongSection{ "VERSE", 8 },
          SongSection{ "CHORUS", 8 },
          SongSection{ "VERSE", 8 },
          SongSection{ "CHORUS", 8 },
          SongSection{ "OUTRO", 4 } } });
    sectionListEditor->setOnChange([this]
    {
        const auto form = sectionListEditor->getForm();
        audioProcessorRef.setCustomSongForm(juce::String(StructureSequencer::serializeForm(form)));
        sectionListEditor->setSize(juce::jmax(1, songSectionsViewport.getMaximumVisibleWidth()),
                                   juce::jmax(1, juce::jmax(songSectionsViewport.getHeight(),
                                                            sectionListEditor->getHeightHint())));
    });
    songSectionsViewport.setViewedComponent(sectionListEditor.get(), false);
    songSectionsViewport.setScrollBarsShown(true, false);
    songSectionsViewport.setScrollBarThickness(8);
    addAndMakeVisible(songSectionsViewport);

    playButton.setComponentID("play");
    playButton.setClickingTogglesState(true);
    playButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff4a7a3a));
    playButton.setColour(juce::TextButton::textColourOnId, juce::Colour(FuzzybandPalette::inkWarm));
    playButton.setTooltip("Play the Sections song form once, then stop. The plugin is idle (silent) until you press Play or Record riff.");
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

    // Stop: end the record-riff mini-structure loop (A-B-A-C-A) and return to
    // idle/silent. Same outcome as Forget — the next Play or Record riff re-arms.
    stopButton.setTooltip("Stop the record-riff loop and go silent. Change the riff by pressing Record riff again.");
    stopButton.onClick = [this]
    {
        audioProcessorRef.requestRiffStop();
        recordRiffButton.setToggleState(false, juce::dontSendNotification);
    };
    addAndMakeVisible(stopButton);

    auto& apvts = audioProcessorRef.getApvts();
    genreAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, "genre", genreCombo);
    swingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "swing", swingSlider);

    // Generative groove lock: hold length + live status indicator.
    lockBarsLabel.setJustificationType(juce::Justification::centredLeft);
    lockBarsLabel.setFont(lookAndFeel.labelFont(12.0f));
    lockBarsLabel.setColour(juce::Label::textColourId, juce::Colour(FuzzybandPalette::ink));
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
    grooveStatusLabel.setFont(lookAndFeel.labelFont(13.0f));
    grooveStatusLabel.setColour(juce::Label::textColourId, juce::Colour(FuzzybandPalette::inkMuted));
    grooveStatusLabel.setText("Groove: follow", juce::dontSendNotification);
    addAndMakeVisible(grooveStatusLabel);

    // ── A5.2: post-lock transition grammar controls ─────────────────────────
    transitionBarsLabel.setJustificationType(juce::Justification::centredLeft);
    transitionBarsLabel.setFont(lookAndFeel.labelFont(12.0f));
    transitionBarsLabel.setColour(juce::Label::textColourId, juce::Colour(FuzzybandPalette::ink));
    transitionBarsSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    transitionBarsSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 18);
    transitionBarsSlider.setRange(2.0, 32.0, 2.0);
    transitionBarsSlider.setDoubleClickReturnValue(true, 8.0);
    transitionBarsSlider.setTooltip("After the riff lock expires, how many bars each transition section holds before returning to the locked riff.");
    transitionBarsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "transitionBars", transitionBarsSlider);
    addAndMakeVisible(transitionBarsLabel);
    addAndMakeVisible(transitionBarsSlider);

    transitionSectionsLabel.setJustificationType(juce::Justification::centredLeft);
    transitionSectionsLabel.setFont(lookAndFeel.labelFont(12.0f));
    transitionSectionsLabel.setColour(juce::Label::textColourId, juce::Colour(FuzzybandPalette::ink));
    transitionSectionsSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    transitionSectionsSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 18);
    transitionSectionsSlider.setRange(1.0, 4.0, 1.0);
    transitionSectionsSlider.setDoubleClickReturnValue(true, 2.0);
    transitionSectionsSlider.setTooltip("How many distinct contrast sections to visit. Each one returns to the locked riff (A) before the next: 2 = A-B-A-C-A, not A-B-C-A.");
    transitionSectionsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "transitionSections", transitionSectionsSlider);
    addAndMakeVisible(transitionSectionsLabel);
    addAndMakeVisible(transitionSectionsSlider);

    transitionStatusLabel.setJustificationType(juce::Justification::centredLeft);
    transitionStatusLabel.setFont(lookAndFeel.labelFont(13.0f));
    transitionStatusLabel.setColour(juce::Label::textColourId, juce::Colour(FuzzybandPalette::amber));
    transitionStatusLabel.setText("Section: -", juce::dontSendNotification);
    addAndMakeVisible(transitionStatusLabel);

    // ── DAW-style input scope ───────────────────────────────────────────────
    scopeComponent.setOpaque(false);
    addAndMakeVisible(scopeComponent);

    // ── Section-bar progress (Play / riff lock / transition) ────────────────
    sectionProgressComponent.setOpaque(false);
    addAndMakeVisible(sectionProgressComponent);

    for (auto* l : { &bpmLabel, &stateLabel, &patternLabel, &styleLabel })
    {
        l->setJustificationType(juce::Justification::centredLeft);
        l->setFont(lookAndFeel.monoFont(11.0f));
        l->setColour(juce::Label::textColourId, juce::Colour(FuzzybandPalette::inkWarm));
    }

    addAndMakeVisible(bpmLabel);
    addAndMakeVisible(stateLabel);
    addAndMakeVisible(patternLabel);
    addAndMakeVisible(styleLabel);

    // Restore a persisted custom form (the editable section list is the form);
    // otherwise default to the practice form INTRO → VERSE → CHORUS → VERSE →
    // CHORUS → OUTRO so Play plays that out of the box.
    if (savedCustomForm.isNotEmpty())
    {
        sectionListEditor->setForm(StructureSequencer::parseFormString(savedCustomForm.toStdString()));
        audioProcessorRef.setCustomSongForm(savedCustomForm);
    }
    else
    {
        audioProcessorRef.setCustomSongForm(
            "INTRO:4,VERSE:8,CHORUS:8,VERSE:8,CHORUS:8,OUTRO:4");
    }

    setResizable(true, false);
    setResizeLimits(520, 900, 720, 1800);
    setSize(520, 1040);
    startTimerHz(20);
}

AccompanimentEditor::~AccompanimentEditor()
{
    setLookAndFeel(nullptr);
}

void AccompanimentEditor::timerCallback()
{
    playButton.setToggleState(audioProcessorRef.playActive.load(std::memory_order_acquire), juce::dontSendNotification);
    const float bpm = audioProcessorRef.getDisplayBpm();
    bpmLabel.setText("BPM: " + juce::String(bpm, 1), juce::dontSendNotification);
    stateLabel.setText("State: " + juce::String(stateName(audioProcessorRef.getDisplayStateIndex())), juce::dontSendNotification);
    patternLabel.setText("Pattern: " + juce::String(audioProcessorRef.getDisplayPatternIndex()), juce::dontSendNotification);

    static const char* kStyleNames[] = {"Palm Mute", "Open Chord", "Single Note", "Sustain", "Silence"};
    const int si = audioProcessorRef.getDisplayStyle();
    if (si >= 0 && si <= 4)
        styleLabel.setText("Style: " + juce::String(kStyleNames[si]), juce::dontSendNotification);
    else
        styleLabel.setText("Style: -", juce::dontSendNotification);

    // Generative groove-lock / riff-capture status.
    recordRiffButton.setToggleState(audioProcessorRef.isRiffCapturing(), juce::dontSendNotification);
    const bool riffLoopArmed = audioProcessorRef.hasLearnedRiff()
        || audioProcessorRef.isRiffCapturing()
        || audioProcessorRef.isGrooveLocked()
        || audioProcessorRef.isTransitionSectionActive();
    forgetRiffButton.setEnabled(riffLoopArmed);
    stopButton.setEnabled(riffLoopArmed);
    if (audioProcessorRef.isRiffCapturing())
    {
        const int n = audioProcessorRef.getRiffCaptureNoteCount();
        const int bar = audioProcessorRef.getRiffCaptureBar();
        if (bar <= 0)
        {
            recordRiffButton.setButtonText("Count-in...");
            grooveStatusLabel.setText("Groove: COUNT-IN - play on 1", juce::dontSendNotification);
        }
        else
        {
            recordRiffButton.setButtonText("Rec " + juce::String(bar) + "/4 - " + juce::String(n));
            grooveStatusLabel.setText("Groove: RECORDING bar " + juce::String(bar)
                                          + "/4 - " + juce::String(n) + " hits",
                                      juce::dontSendNotification);
        }
        grooveStatusLabel.setColour(juce::Label::textColourId, juce::Colour(FuzzybandPalette::amber));
    }
    else if (audioProcessorRef.isTransitionSectionActive())
    {
        recordRiffButton.setButtonText("Record riff");
        grooveStatusLabel.setText("Groove: transition", juce::dontSendNotification);
        grooveStatusLabel.setColour(juce::Label::textColourId, juce::Colour(FuzzybandPalette::amber));
    }
    else if (audioProcessorRef.isGrooveLocked() || audioProcessorRef.hasLearnedRiff())
    {
        recordRiffButton.setButtonText("Record riff");
        // The bar countdown lives in the unified section display below.
        grooveStatusLabel.setText("Groove: LOCKED (riff A)", juce::dontSendNotification);
        grooveStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff9ade78));
    }
    else
    {
        recordRiffButton.setButtonText("Record riff");
        // The engine only listens once armed (Play or Record riff); otherwise it
        // is idle and silent.
        if (audioProcessorRef.playActive.load(std::memory_order_acquire))
            grooveStatusLabel.setText("Groove: PLAYING", juce::dontSendNotification);
        else
            grooveStatusLabel.setText("Groove: idle - press Play or Record riff",
                                      juce::dontSendNotification);
        grooveStatusLabel.setColour(juce::Label::textColourId, juce::Colour(FuzzybandPalette::inkMuted));
    }

    // ── Unified section countdown + progress (Play / riff lock / transition) ──
    // One consistent "SECTION - bar X/Y - N left" read so the guitarist always
    // knows when to anticipate a change, with a bar-segment progress bar under it.
    const int phase = audioProcessorRef.getSectionPhase();
    const int bar = audioProcessorRef.getSectionBar();
    const int tot = audioProcessorRef.getSectionBarsTotal();
    const int rem = audioProcessorRef.getSectionBarsRemaining();
    const float frac = audioProcessorRef.getSectionProgress();

    juce::String sectionText;
    juce::Colour sectionColour;
    if (phase == static_cast<int>(AccompanimentProcessor::SectionPhase::Play))
    {
        const juce::String sec = audioProcessorRef.getCurrentSectionName();
        sectionText = (bar > 0 && tot > 0)
            ? sec + " - bar " + juce::String(bar) + "/" + juce::String(tot)
                + " - " + juce::String(rem) + " left"
            : sec;
        sectionColour = juce::Colour(FuzzybandPalette::moss);
    }
    else if (phase == static_cast<int>(AccompanimentProcessor::SectionPhase::Transition))
    {
        const char* secName = audioProcessorRef.getTransitionSectionName();
        const int num = audioProcessorRef.getTransitionSectionNumber();
        const char letter = static_cast<char>('B' + juce::jmax(0, num - 1));
        sectionText = (bar > 0 && tot > 0)
            ? juce::String("Transition ") + letter + " - " + juce::String(secName)
                + " - bar " + juce::String(bar) + "/" + juce::String(tot)
                + " - " + juce::String(rem) + " left"
            : juce::String("Transition ") + letter + " - " + juce::String(secName);
        sectionColour = juce::Colour(FuzzybandPalette::amber);
    }
    else if (phase == static_cast<int>(AccompanimentProcessor::SectionPhase::Lock))
    {
        sectionText = (bar > 0 && tot > 0)
            ? juce::String("Riff A - bar ") + juce::String(bar) + "/"
                + juce::String(tot) + " - " + juce::String(rem) + " left"
            : juce::String("Riff A");
        sectionColour = juce::Colour(0xff9ade78);
    }
    else
    {
        sectionText = "-";
        sectionColour = juce::Colour(FuzzybandPalette::inkMuted);
    }
    transitionStatusLabel.setText("Section: " + sectionText, juce::dontSendNotification);
    transitionStatusLabel.setColour(juce::Label::textColourId, sectionColour);
    sectionProgressComponent.setProgress(bar, tot, rem, frac);

    // ── DAW-style scope: copy ring + playhead, repaint ───────────────────────
    std::array<float, AccompanimentProcessor::kScopeSize> scopeCopy{};
    audioProcessorRef.copyScopeSamples(scopeCopy.data(), AccompanimentProcessor::kScopeSize);
    scopeComponent.setScopeData(scopeCopy.data(), AccompanimentProcessor::kScopeSize,
                                audioProcessorRef.getPlayheadFraction(),
                                audioProcessorRef.getScopeSamplesPerBar());
}

void AccompanimentEditor::ScopeComponent::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Panel background — dark frosted panel matching the theme.
    g.setColour(juce::Colour(0xd0081208));
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(juce::Colour(0x886a9a50));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

    const float width = bounds.getWidth();
    const float midY = bounds.getCentreY();
    const float halfH = bounds.getHeight() * 0.42f;

    // Center line.
    g.setColour(juce::Colour(0x556a9a50));
    g.drawLine(bounds.getX(), midY, bounds.getRight(), midY, 1.0f);

    // ── Bar-aligned beat grid: beat 1 (downbeat) sits at the LEFT edge ────────
    // The ring is a rolling buffer of decimated input samples (newest last). The
    // current sample sits at bar phase `playheadFraction_`; the downbeat of the
    // current bar is `playheadFraction_ * samplesPerBar_` samples earlier. We
    // render that bar slice so the downbeat coincides with the left edge.
    const float ph = juce::jlimit(0.0f, 1.0f, playheadFraction_);
    const int spb = juce::jmax(1, samplesPerBar_);
    int elapsed = static_cast<int>(ph * static_cast<float>(spb));
    if (elapsed < 0) elapsed = 0;
    const int startIdx = juce::jmax(0, count_ - 1 - elapsed);
    const int barSamples = count_ - startIdx;  // played samples in this bar so far

    // Beat grid verticals + labels (1 = downbeat at the left).
    g.setColour(juce::Colour(0x556a9a50));
    for (int b = 1; b <= 4; ++b)
    {
        const float frac = static_cast<float>(b - 1) / 4.0f;
        const float x = bounds.getX() + width * frac;
        g.drawLine(x, bounds.getY() + 2.0f, x, bounds.getBottom() - 16.0f, 1.0f);
    }
    g.setColour(juce::Colour(0x99c8d8c0));
    g.setFont(juce::FontOptions(10.0f));
    for (int b = 1; b <= 4; ++b)
    {
        const float frac = static_cast<float>(b - 1) / 4.0f;
        const float x = bounds.getX() + width * frac;
        g.drawFittedText(juce::String(b),
                         juce::Rectangle<int>(static_cast<int>(x - 9.0f),
                                              static_cast<int>(bounds.getBottom() - 15.0f),
                                              18, 13),
                         juce::Justification::centred, 1);
    }

    // Waveform: the played portion of the current bar, drawn so the downbeat is
    // at the left and fills to the playhead (the un-played bar stays empty).
    if (barSamples > 1)
    {
        float maxAbs = 0.0001f;
        for (int i = startIdx; i < count_; ++i)
            maxAbs = juce::jmax(maxAbs, std::abs(samples_[static_cast<size_t>(i)]));
        const float scale = halfH / maxAbs;
        const float drawW = width * ph;  // played portion of the bar

        juce::Path wave;
        wave.startNewSubPath(bounds.getX(), midY);
        for (int i = startIdx; i < count_; ++i)
        {
            const float frac = static_cast<float>(i - startIdx)
                             / static_cast<float>(juce::jmax(1, barSamples - 1));
            const float x = bounds.getX() + frac * drawW;
            const float v = samples_[static_cast<size_t>(i)] * scale;
            wave.addLineSegment({ x, midY - v, x, midY }, 1.0f);
        }
        g.setColour(juce::Colour(0xff7ab860));
        g.strokePath(wave, juce::PathStrokeType(1.0f));

        // Soft green glow under the played waveform.
        juce::Path fill;
        fill.startNewSubPath(bounds.getX(), midY);
        for (int i = startIdx; i < count_; ++i)
        {
            const float frac = static_cast<float>(i - startIdx)
                             / static_cast<float>(juce::jmax(1, barSamples - 1));
            const float x = bounds.getX() + frac * drawW;
            const float v = samples_[static_cast<size_t>(i)] * scale;
            fill.lineTo(x, midY - v);
        }
        fill.lineTo(bounds.getX() + drawW, midY);
        fill.closeSubPath();
        g.setColour(juce::Colour(0x337ab860));
        g.fillPath(fill);
    }

    // Playhead — bright vertical cursor at the current bar position.
    const float px = bounds.getX() + width * ph;
    g.setColour(juce::Colour(0xff9ade78));
    g.drawLine(px, bounds.getY(), px, bounds.getBottom(), 2.0f);

    // Playhead cap triangle.
    juce::Path cap;
    cap.addTriangle(px, bounds.getY(),
                    px - 4.0f, bounds.getY() + 6.0f,
                    px + 4.0f, bounds.getY() + 6.0f);
    g.fillPath(cap);
}

void AccompanimentEditor::SectionProgressComponent::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Panel background — dark frosted panel matching the theme.
    g.setColour(juce::Colour(0xd0081208));
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(juce::Colour(0x886a9a50));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 5.0f, 1.0f);

    if (total_ <= 0)
        return;

    const float pad = 6.0f;
    const float barTop = bounds.getY() + 5.0f;
    const float barH = bounds.getHeight() - 10.0f;
    const float trackW = bounds.getWidth() - pad * 2.0f;

    // Bar-segment track: `total_` segments, each a thin rounded cell. Completed
    // bars fill; the current bar is highlighted (bright); future bars are dim.
    const float segGap = 3.0f;
    const float segW = (trackW - segGap * static_cast<float>(total_ - 1))
                     / static_cast<float>(total_);
    for (int i = 0; i < total_; ++i)
    {
        const float x = bounds.getX() + pad + static_cast<float>(i) * (segW + segGap);
        const juce::Rectangle<float> cell(x, barTop, segW, barH);
        const bool done = (i + 1) < bar_;       // fully elapsed bars
        const bool current = (i + 1) == bar_;   // the bar we are in
        if (done)
            g.setColour(juce::Colour(0xff8cc46a));          // moss (completed)
        else if (current)
            g.setColour(juce::Colour(0xffe0b06a));          // amber (now)
        else
            g.setColour(juce::Colour(0x553a6030));          // dim (ahead)
        g.fillRoundedRectangle(cell, 2.0f);
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

    // ── Title row (fixed) ────────────────────────────────────────────────────
    auto titleRow = r.removeFromTop(28);
    playButton.setBounds(titleRow.removeFromRight(92));
    titleRow.removeFromRight(6);
    recordRiffButton.setBounds(titleRow.removeFromRight(100));
    titleRow.removeFromRight(6);
    stopButton.setBounds(titleRow.removeFromRight(56));
    titleRow.removeFromRight(6);
    forgetRiffButton.setBounds(titleRow.removeFromRight(56));
    titleRow.removeFromRight(6);
    versionLabel.setBounds(titleRow.removeFromRight(64));
    titleLabel.setBounds(titleRow);
    r.removeFromTop(8);

    const int userTop = r.getY();

    // ── Adaptive vertical layout ─────────────────────────────────────────────
    // Fixed heights for the rows/headings and the diagnostics. The editable
    // sections list is the flexible element: it grows to absorb extra vertical
    // space so the controls spread to fill the window instead of leaving a
    // large empty band at the bottom. Inter-group gaps stay modest and even.
    constexpr int rowH     = 52;    // label+control row
    constexpr int headH    = 20;    // panel heading
    constexpr int sectionH = 18;    // "SECTIONS" heading
    constexpr int scopeH   = 110;   // waveform scope
    constexpr int diagH    = 24;    // status/readout line
    constexpr int progH    = 18;    // section bar-segment progress
    constexpr int gap      = 12;    // breathing room between control groups
    constexpr int diagGap  = 14;    // panel → diagnostics
    constexpr int listMin  = 170;   // sections list never collapses below this
    constexpr int nGaps    = 7;     // gaps inside the panel

    const int panelFixed = headH + 5 * rowH + sectionH;                 // heading, 5 rows, sections head
    const int diagFixed  = diagGap + 2 * diagH + progH + scopeH + 4 * diagH;  // status x2, progress, scope, readouts
    const int other      = panelFixed + nGaps * gap + diagFixed;        // everything except the list
    const int availH     = r.getHeight();

    // Give the list everything left over, with a small margin so the last
    // readout isn't flush against the window edge; clamp so the list never
    // collapses and never pushes the other rows off the bottom.
    const int sectionsH = juce::jmax(listMin, availH - other - 6);

    userPolicyHeading.setBounds(r.removeFromTop(headH));
    r.removeFromTop(gap);

    auto row = r.removeFromTop(rowH);
    genreLabel.setBounds(row.removeFromLeft(140));
    genreCombo.setBounds(row);
    r.removeFromTop(gap);

    row = r.removeFromTop(rowH);
    swingLabel.setBounds(row.removeFromLeft(140));
    swingSlider.setBounds(row);
    r.removeFromTop(gap);

    songSectionsLabel.setBounds(r.removeFromTop(sectionH));
    r.removeFromTop(2);

    auto listArea = r.removeFromTop(sectionsH);
    songSectionsViewport.setBounds(listArea);
    if (sectionListEditor)
        sectionListEditor->setSize(juce::jmax(1, songSectionsViewport.getMaximumVisibleWidth()),
                                   juce::jmax(listArea.getHeight(), sectionListEditor->getHeightHint()));
    r.removeFromTop(gap);

    row = r.removeFromTop(rowH);
    lockBarsLabel.setBounds(row.removeFromLeft(140));
    lockBarsSlider.setBounds(row);
    r.removeFromTop(gap);

    row = r.removeFromTop(rowH);
    transitionBarsLabel.setBounds(row.removeFromLeft(140));
    transitionBarsSlider.setBounds(row);
    r.removeFromTop(gap);

    row = r.removeFromTop(rowH);
    transitionSectionsLabel.setBounds(row.removeFromLeft(140));
    transitionSectionsSlider.setBounds(row);

    const int userBottom = r.getY();
    userPolicyArea = juce::Rectangle<int>(12, userTop, getWidth() - 24, juce::jmax(1, userBottom - userTop));

    r.removeFromTop(diagGap);

    grooveStatusLabel.setBounds(r.removeFromTop(diagH));
    transitionStatusLabel.setBounds(r.removeFromTop(diagH));
    sectionProgressComponent.setBounds(r.removeFromTop(progH));
    scopeComponent.setBounds(r.removeFromTop(scopeH));
    r.removeFromTop(4);
    bpmLabel.setBounds(r.removeFromTop(diagH));
    stateLabel.setBounds(r.removeFromTop(diagH));
    patternLabel.setBounds(r.removeFromTop(diagH));
    styleLabel.setBounds(r.removeFromTop(diagH));
}
