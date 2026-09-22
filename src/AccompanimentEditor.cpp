#include "AccompanimentEditor.h"
#include "AccompanimentProcessor.h"
#include "analysis/StructureTagger.h"
#include "analysis/StructureSequencer.h"
#include "midi/GrooveTemplate.h"
#include <BinaryData.h>
#include <functional>

// ─── Phase 2: editable custom song form (drag-to-reorder) ─────────────────────
namespace
{

constexpr int kSectionRowH = 32;

// Editor frame: the margin around the content, the title row, and the gap
// between it and the CONTROLS panel. Fixed regardless of window size — these
// are the only chrome the adaptive layout cannot reclaim.
constexpr int kEditorMargin = 12;
constexpr int kTitleRowH    = 28;
constexpr int kTitleGap     = 8;
constexpr int kRowLabelW    = 140;   // label column of a label+control row
constexpr int kEditorChromeH = 2 * kEditorMargin + kTitleRowH + kTitleGap;

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
        // The section list sits inside the editor's scrolling panel; leave the
        // wheel to the viewport instead of editing bars by accident.
        barsSlider.setScrollWheelEnabled(false);

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
        // Fill only — no outline. Every box carrying the same 1px sage stroke is
        // what made the panel read as a wireframe; the rows are separated by
        // tone and spacing instead.
        g.setColour(juce::Colour(0xd0162411));
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 4.0f);
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
        // Rows stack from the top; "+ Add section" is anchored to the bottom of
        // the well. The panel hands the list any spare window height, and pinning
        // the button means that slack reads as room for more sections rather than
        // as a gap with a button floating in the middle of it.
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

/**
 * @brief Human-readable loop shape of the post-lock transition grammar.
 *
 * The engine returns to the locked riff (A) between every contrast, and the
 * contrast slots repeat in order (B, C, D, E) once the cycle wraps, so the
 * shape is A-B-A-C-A-D-A-E… For one contrast the period is just A-B, so it is
 * shown twice (`A-B-A-B…`) to make the loop obvious; two or more show one full
 * cycle. `transitionSections` is clamped to 1..4.
 */
static juce::String transitionFormShape(int sections)
{
    sections = juce::jlimit(1, 4, sections);
    if (sections == 1)
        return "A-B-A-B...";

    static const char* kContrast = "BCDE";
    juce::String s = "A";
    for (int i = 0; i < sections; ++i)
        s << "-" << kContrast[i] << "-A";
    s = s.dropLastCharacters(2);   // drop the trailing return to A (the wrap implies it)
    return s + "...";
}

/**
 * @brief The same shape as bare letters for the chip readout.
 *
 * "ABAB" (one contrast, shown twice so the loop is obvious) or the full cycle
 * "ABAC" / "ABACAD" / "ABACADAE" with the trailing return to A dropped (the
 * loop ellipsis implies it).
 */
static juce::String transitionFormLetters(int sections)
{
    sections = juce::jlimit(1, 4, sections);
    if (sections == 1)
        return "ABAB";

    static const char* kContrast = "BCDE";
    juce::String s = "A";
    for (int i = 0; i < sections; ++i)
        s << kContrast[i] << "A";
    return s.dropLastCharacters(1);
}

AccompanimentEditor::AccompanimentEditor(AccompanimentProcessor& p)
    : AudioProcessorEditor(&p)
    , audioProcessorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    // Everything the user sees goes inside `content`, which we then put in a
    // vertical viewport. The editor may be shorter than the panel needs; when it
    // is, the panel scrolls instead of losing its bottom edge off-screen.
    addAndMakeVisible(contentViewport);
    contentViewport.setViewedComponent(&content, false);
    contentViewport.setScrollBarsShown(true, false);
    contentViewport.setScrollBarThickness(10);

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
    content.addAndMakeVisible(titleLabel);

    versionLabel.setText(juce::String("v") + ProjectInfo::versionString, juce::dontSendNotification);
    versionLabel.setJustificationType(juce::Justification::centredRight);
    versionLabel.setFont(lookAndFeel.monoFont(11.0f));
    versionLabel.setColour(juce::Label::textColourId, juce::Colour(FuzzybandPalette::inkMuted));
    versionLabel.setTooltip("Plugin version (CMake project VERSION). Rebuild after bumping it in CMakeLists.txt.");
    content.addAndMakeVisible(versionLabel);

    genreLabel.setJustificationType(juce::Justification::centredLeft);
    genreLabel.setFont(lookAndFeel.labelFont(12.0f));
    genreLabel.setColour(juce::Label::textColourId, juce::Colour(FuzzybandPalette::ink));
    for (int i = 0; i < Groove::presetCount(); ++i)
        genreCombo.addItem(Groove::presetFor(i).name, genreCombo.getNumItems() + 1);
    genreCombo.setTooltip("Genre preset: groove feel, velocity profile, section dynamics (B1). Rock is the default; metal and rock subgenres are presets.");
    content.addAndMakeVisible(genreLabel);
    content.addAndMakeVisible(genreCombo);

    swingLabel.setJustificationType(juce::Justification::centredLeft);
    swingLabel.setFont(lookAndFeel.labelFont(12.0f));
    swingLabel.setColour(juce::Label::textColourId, juce::Colour(FuzzybandPalette::ink));
    swingSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    swingSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 18);
    swingSlider.setRange(0.0, 1.0, 0.01);
    swingSlider.setDoubleClickReturnValue(true, 0.0);
    swingSlider.setTooltip("Swing/shuffle ratio: delays off-8th drum events (0-100%).");
    content.addAndMakeVisible(swingLabel);
    content.addAndMakeVisible(swingSlider);

    humanizeLabel.setJustificationType(juce::Justification::centredLeft);
    humanizeLabel.setFont(lookAndFeel.labelFont(12.0f));
    humanizeLabel.setColour(juce::Label::textColourId, juce::Colour(FuzzybandPalette::ink));
    humanizeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    humanizeSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 18);
    humanizeSlider.setRange(0.0, 1.0, 0.01);
    humanizeSlider.setDoubleClickReturnValue(true, 0.35);
    humanizeSlider.setTooltip("Ornament amount: scales open-hat, ghost, kick-drop and micro-fill probability. 0 is a literal groove.");
    content.addAndMakeVisible(humanizeLabel);
    content.addAndMakeVisible(humanizeSlider);

    // Editable section list inside a viewport so the rows are always visible
    // (the previous layout called resized() before this existed, so the list
    // had 0×0 bounds — heading visible, no items).
    // Default custom form: INTRO → VERSE → CHORUS → VERSE → CHORUS → OUTRO
    // (single dropdown removed; the editable section list is the song form).
    // No "SECTIONS" heading: each row names its own section, and "+ Add section"
    // anchors the bottom of the list.
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
    content.addAndMakeVisible(songSectionsViewport);

    // Mode heading over the song-form list (the editable section rows are the
    // Play-mode form; without a heading they read as controls for whatever is
    // above them).
    playModeHeader.setHeader("Play mode", lookAndFeel.displayFont(13.0f));
    content.addAndMakeVisible(playModeHeader);

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
    content.addAndMakeVisible(playButton);

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
    content.addAndMakeVisible(recordRiffButton);

    forgetRiffButton.setTooltip("Clear the recorded riff and go silent. Record again to capture a new take.");
    forgetRiffButton.onClick = [this]
    {
        audioProcessorRef.requestRiffForget();
        recordRiffButton.setToggleState(false, juce::dontSendNotification);
    };
    content.addAndMakeVisible(forgetRiffButton);

    auto& apvts = audioProcessorRef.getApvts();
    genreAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, "genre", genreCombo);
    swingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "swing", swingSlider);
    humanizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "humanize", humanizeSlider);

    // ComboBoxAttachment owns genreCombo.onChange, so swing must follow the
    // genre *parameter* (T8.3). Listen after sendInitialUpdate so opening the
    // editor does not clobber a persisted swing value.
    genreSwingListener.owner = this;
    if (auto* genreParam = apvts.getParameter("genre"))
        genreParam->addListener(&genreSwingListener);

    // Mode heading over the recorded-riff controls (lock hold + post-lock
    // transition grammar).
    recordRiffHeader.setHeader("Record riff", lookAndFeel.displayFont(13.0f));
    content.addAndMakeVisible(recordRiffHeader);

    // Generative groove lock: hold length.
    lockBarsLabel.setJustificationType(juce::Justification::centredLeft);
    lockBarsLabel.setFont(lookAndFeel.labelFont(12.0f));
    lockBarsLabel.setColour(juce::Label::textColourId, juce::Colour(FuzzybandPalette::ink));
    lockBarsSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    lockBarsSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 18);
    lockBarsSlider.setRange(4.0, 64.0, 4.0);
    lockBarsSlider.setDoubleClickReturnValue(true, 16.0);
    lockBarsSlider.setTooltip("Lock length in bars: how long the auto-lock holds after you stop playing the riff (returning to the riff extends it).");
    lockBarsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "lockBars", lockBarsSlider);
    content.addAndMakeVisible(lockBarsLabel);
    content.addAndMakeVisible(lockBarsSlider);

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
    content.addAndMakeVisible(transitionBarsLabel);
    content.addAndMakeVisible(transitionBarsSlider);

    transitionSectionsLabel.setJustificationType(juce::Justification::centredLeft);
    transitionSectionsLabel.setFont(lookAndFeel.labelFont(12.0f));
    transitionSectionsLabel.setColour(juce::Label::textColourId, juce::Colour(FuzzybandPalette::ink));
    transitionSectionsSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    transitionSectionsSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 18);
    transitionSectionsSlider.setRange(1.0, 4.0, 1.0);
    transitionSectionsSlider.setDoubleClickReturnValue(true, 2.0);
    transitionSectionsSlider.setTooltip("How many distinct contrast sections to visit. Each returns to the locked riff (A) first, and each slot keeps the same groove every cycle: 1 = A-B-A-B, 2 = A-B-A-C-A.");
    transitionSectionsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "transitionSections", transitionSectionsSlider);
    content.addAndMakeVisible(transitionSectionsLabel);
    content.addAndMakeVisible(transitionSectionsSlider);

    // Loop-shape pill: the post-lock form as letter chips (A = locked riff,
    // B/C/D/E = contrast sections). Set from the timer so it tracks host
    // automation as well as user moves.
    formShapeComponent.setFonts(lookAndFeel.monoFont(13.0f), lookAndFeel.labelFont(10.0f));
    content.addAndMakeVisible(formShapeComponent);

    // ── One-line status: phase dot + text, section progress on the same row ──
    statusLabel.setJustificationType(juce::Justification::centredLeft);
    statusLabel.setFont(lookAndFeel.labelFont(13.0f));
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(FuzzybandPalette::moss));
    statusLabel.setText("Idle", juce::dontSendNotification);
    statusLabel.setTooltip("Left: what the engine is doing (idle / playing / recording / locked to your riff). Right: progress through the current section.");
    content.addAndMakeVisible(statusDot);
    content.addAndMakeVisible(statusLabel);

    // ── DAW-style input scope ───────────────────────────────────────────────
    scopeComponent.setOpaque(false);
    content.addAndMakeVisible(scopeComponent);

    // ── Section-bar progress (Play / riff lock / transition) ────────────────
    sectionProgressComponent.setOpaque(false);
    content.addAndMakeVisible(sectionProgressComponent);

    // ── Engine readout: one quiet line instead of four labelled fields ──────
    readoutLabel.setJustificationType(juce::Justification::centredLeft);
    readoutLabel.setFont(lookAndFeel.monoFont(11.0f));
    readoutLabel.setColour(juce::Label::textColourId, juce::Colour(FuzzybandPalette::inkMuted));
    readoutLabel.setTooltip("Live engine readout: tempo, dynamic state, pattern index, detected input style.");
    content.addAndMakeVisible(readoutLabel);

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

    // Sliders must not eat the mouse wheel: inside the scrolling panel the wheel
    // belongs to the viewport. Without this, hovering a slider scrolls nothing
    // (and silently edits the value instead).
    for (auto* s : { &swingSlider, &humanizeSlider, &lockBarsSlider,
                     &transitionBarsSlider, &transitionSectionsSlider })
        s->setScrollWheelEnabled(false);

    // The corner grip guarantees a resize affordance even in hosts that offer
    // none of their own. fitEditorToScreen() then sets the size limits (capped
    // at the screen) and the size we open at; below the height floor the panel
    // scrolls rather than clipping.
    setResizable(true, true);
    fitEditorToScreen();
    timerCallback();
    startTimerHz(20);
}

AccompanimentEditor::~AccompanimentEditor()
{
    if (auto* genreParam = audioProcessorRef.getApvts().getParameter("genre"))
        genreParam->removeListener(&genreSwingListener);
    genreSwingListener.owner = nullptr;
    setLookAndFeel(nullptr);
}

void AccompanimentEditor::applyGenreDefaultSwing() noexcept
{
    const int g = genreCombo.getSelectedItemIndex();
    if (g < 0 || g >= Groove::presetCount())
        return;
    if (auto* p = audioProcessorRef.getApvts().getParameter("swing"))
        p->setValueNotifyingHost(p->convertTo0to1(Groove::presetFor(g).defaultSwing));
}

void AccompanimentEditor::timerCallback()
{
    playButton.setToggleState(audioProcessorRef.playActive.load(std::memory_order_acquire), juce::dontSendNotification);

    // ── Engine readout: one line, no per-field labels ────────────────────────
    const float bpm = audioProcessorRef.getDisplayBpm();
    static const char* kStyleNames[] = {"Palm Mute", "Open Chord", "Single Note", "Sustain", "Silence"};
    const int si = audioProcessorRef.getDisplayStyle();
    const juce::String style = (si >= 0 && si <= 4) ? juce::String(kStyleNames[si]) : juce::String("-");

    // A middle dot keeps the fields apart without four "Label:" prefixes. It has
    // to go through fromUTF8: juce::String(const char*) reads bytes as Latin-1,
    // so a UTF-8 literal would render as mojibake.
    static const juce::String kSep = juce::String::fromUTF8("\xc2\xb7");

    readoutLabel.setText(juce::String(bpm, 1) + " bpm" + kSep + " "
                             + juce::String(stateName(audioProcessorRef.getDisplayStateIndex()))
                             + kSep + " P" + juce::String(audioProcessorRef.getDisplayPatternIndex())
                             + kSep + " " + style,
                         juce::dontSendNotification);

    recordRiffButton.setToggleState(audioProcessorRef.isRiffCapturing(), juce::dontSendNotification);
    const bool riffLoopArmed = audioProcessorRef.hasLearnedRiff()
        || audioProcessorRef.isRiffCapturing()
        || audioProcessorRef.isGrooveLocked()
        || audioProcessorRef.isTransitionSectionActive();
    forgetRiffButton.setEnabled(riffLoopArmed);
    recordRiffButton.setButtonText(audioProcessorRef.isRiffCapturing() ? "Stop" : "Record riff");

    // ── One status row: phase dot + phase/section + bar-segment progress ─────
    // "Groove" and "Section" were two lines describing two axes of the same
    // moment (what the engine is doing, and where we are in the form). They fit
    // on one row, and the segment bar already draws the "bar X/Y" countdown, so
    // the text no longer repeats it.
    const int phase = audioProcessorRef.getSectionPhase();
    const int bar = audioProcessorRef.getSectionBar();
    const int tot = audioProcessorRef.getSectionBarsTotal();
    const int rem = audioProcessorRef.getSectionBarsRemaining();
    const float frac = audioProcessorRef.getSectionProgress();

    auto position = [bar, tot]() -> juce::String
    {
        return (bar > 0 && tot > 0) ? " " + juce::String(bar) + "/" + juce::String(tot)
                                    : juce::String();
    };

    juce::String statusText;
    juce::Colour statusColour;

    if (audioProcessorRef.isRiffCapturing())
    {
        const int n = audioProcessorRef.getRiffCaptureNoteCount();
        const int capBar = audioProcessorRef.getRiffCaptureBar();
        statusText = (capBar <= 0)
            ? juce::String("Count-in - play on 1")
            : "Recording " + juce::String(capBar) + "/4 - " + juce::String(n) + " hits";
        statusColour = juce::Colour(FuzzybandPalette::amber);
    }
    else if (audioProcessorRef.isPlayCountingIn())
    {
        // Play has the same 1-bar click count-in as Record riff; show the same
        // amber "COUNT-IN" status so the wait before the form starts is legible
        // (it previously read "Idle" throughout the count-in).
        statusText = "Count-in - play on 1";
        statusColour = juce::Colour(FuzzybandPalette::amber);
    }
    else if (phase == static_cast<int>(AccompanimentProcessor::SectionPhase::Transition))
    {
        int maxSections = 2;
        if (auto* raw = audioProcessorRef.getApvts().getRawParameterValue("transitionSections"))
            maxSections = juce::jlimit(1, 4, juce::roundToInt(raw->load()));

        juce::String tag = "Transition";
        if (maxSections > 1)
        {
            const int num = audioProcessorRef.getTransitionSectionNumber();
            tag += juce::String(" ") + static_cast<char>('B' + juce::jmax(0, num - 1));
        }
        statusText = tag + position();
        statusColour = juce::Colour(FuzzybandPalette::amber);
    }
    else if (phase == static_cast<int>(AccompanimentProcessor::SectionPhase::Lock))
    {
        statusText = "Locked - Riff A" + position();
        statusColour = juce::Colour(0xff9ade78);
    }
    else if (phase == static_cast<int>(AccompanimentProcessor::SectionPhase::Play))
    {
        statusText = audioProcessorRef.getCurrentSectionName() + position();
        statusColour = juce::Colour(FuzzybandPalette::moss);
    }
    else
    {
        statusText = "Idle";
        statusColour = juce::Colour(FuzzybandPalette::inkMuted);
    }

    statusLabel.setText(statusText, juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, statusColour);
    statusDot.setDotColour(statusColour);
    sectionProgressComponent.setProgress(bar, tot, rem, frac);

    // ── Loop-shape pill: the form the selected contrast count produces ───────
    int shapeSections = 2;
    if (auto* raw = audioProcessorRef.getApvts().getRawParameterValue("transitionSections"))
        shapeSections = juce::jlimit(1, 4, juce::roundToInt(raw->load()));
    formShapeComponent.setShape(transitionFormLetters(shapeSections));
    formShapeComponent.setTooltip("Loop after the lock: " + transitionFormShape(shapeSections)
                                  + "  A is the locked riff; B/C/D/E are contrast sections.");

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

    // Panel background — dark frosted panel matching the theme (fill only; the
    // outline is left to the beat grid drawn below).
    g.setColour(juce::Colour(0xd0081208));
    g.fillRoundedRectangle(bounds, 6.0f);

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

void AccompanimentEditor::StatusDot::paint(juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    // Soft halo + solid core, so the phase reads at a glance without a word.
    g.setColour(colour_.withAlpha(0.22f));
    g.fillEllipse(b);
    g.setColour(colour_);
    g.fillEllipse(b.reduced(b.getWidth() * 0.28f));
}

void AccompanimentEditor::SectionHeader::paint(juce::Graphics& g)
{
    auto b = getLocalBounds();

    // Measure the title so the rule starts right after it for either heading.
    // (Font::getStringWidth is deprecated in JUCE 8, so use TextLayout.)
    juce::AttributedString as(text_.toUpperCase());
    as.setFont(font_);
    juce::TextLayout layout;
    layout.createLayout(as, 1000.0f);
    const int textW = juce::roundToInt(std::ceil(layout.getWidth()));

    // Slim moss tick anchors the group; the title sits next to it.
    g.setColour(juce::Colour(FuzzybandPalette::moss));
    g.fillRoundedRectangle((float) b.getX(), (float) b.getCentreY() - 7.0f,
                           3.0f, 14.0f, 1.5f);

    g.setFont(font_);
    g.drawFittedText(text_.toUpperCase(), b.withTrimmedLeft(11).withWidth(textW + 4),
                     juce::Justification::centredLeft, 1);

    // Hairline rule out to the right edge.
    const float ruleX = (float) (b.getX() + 11 + textW + 10);
    if (ruleX < (float) b.getRight())
    {
        g.setColour(juce::Colour(0x446a9a50));
        g.drawLine(ruleX, (float) b.getCentreY(), (float) b.getRight(),
                   (float) b.getCentreY(), 1.0f);
    }
}

void AccompanimentEditor::FormShapeComponent::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();

    const int chipH = juce::jlimit(14, 20, bounds.getHeight() - 6);
    const int chipW = chipH + 2;
    const int n = letters_.length();
    constexpr int kPadX = 10;
    constexpr int kCaptionW = 38;
    constexpr int kSepW = 12;

    // Size the pill to its content so a 1-contrast shape does not sit in a wide
    // empty box. It is left-aligned under the slider it belongs to.
    const int neededW = kPadX + kCaptionW + n * (chipW + kSepW) + kPadX;
    const juce::Rectangle<int> pill(bounds.getX(), bounds.getY(),
                                    juce::jmin(bounds.getWidth(), neededW),
                                    bounds.getHeight());

    g.setColour(juce::Colour(0xcc0e1a0c));
    g.fillRoundedRectangle(pill.toFloat(), 6.0f);
    g.setColour(juce::Colour(0x556a9a50));
    g.drawRoundedRectangle(pill.toFloat().reduced(0.5f), 6.0f, 1.0f);

    auto content = pill.reduced(kPadX, 3);

    // "LOOP" caption, then one chip per step.
    g.setColour(juce::Colour(FuzzybandPalette::inkMuted));
    g.setFont(captionFont_);
    g.drawFittedText("LOOP", content.removeFromLeft(kCaptionW), juce::Justification::centredLeft, 1);

    if (letters_.isEmpty())
        return;

    const int chipY = content.getCentreY() - chipH / 2;
    int x = content.getX() + 4;

    for (int i = 0; i < n; ++i)
    {
        const juce::juce_wchar c = letters_[i];
        const bool home = (c == 'A');
        const juce::Colour col = home ? juce::Colour(FuzzybandPalette::moss)
                                      : juce::Colour(FuzzybandPalette::amber);

        const juce::Rectangle<int> chip(x, chipY, chipW, chipH);
        g.setColour(col.withAlpha(0.20f));
        g.fillRoundedRectangle(chip.toFloat(), 4.0f);
        g.setColour(col);
        g.setFont(chipFont_.withHeight((float) chipH * 0.66f));
        g.drawFittedText(juce::String::charToString(c), chip, juce::Justification::centred, 1);
        x += chipW;

        // Interpunct between steps, loop ellipsis after the last one.
        g.setColour(juce::Colour(FuzzybandPalette::inkMuted));
        g.setFont(captionFont_);
        const bool last = (i + 1 == n);
        const juce::String sep = last ? juce::String::fromUTF8("\xe2\x80\xa6")   // …
                                      : juce::String::fromUTF8("\xc2\xb7");  // ·
        g.drawFittedText(sep, juce::Rectangle<int>(x, chipY, kSepW, chipH),
                         juce::Justification::centred, 1);
        x += kSepW;
    }
}

void AccompanimentEditor::SectionProgressComponent::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Track background only — the lit segments carry the progress, so the
    // rounded outline just added another box to the stack.
    g.setColour(juce::Colour(0x66050f04));
    g.fillRoundedRectangle(bounds, 5.0f);

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

int AccompanimentEditor::LayoutMetrics::fixedHeight() const noexcept
{
    // The six group gaps and six label+control rows, the two mode headings and
    // the form-shape pill, then the diagnostics: gap to the status row, the status
    // row itself, a gap, the scope, a 4px pad and the single engine readout line.
    return 6 * rowH + 6 * gap
         + 2 * headerH + 2 * headerGap
         + shapeGap + shapeH
         + diagGap + statusH + gap + scopeH + 4 + readoutH;
}

AccompanimentEditor::LayoutMetrics
AccompanimentEditor::metricsForBodyHeight(int bodyH, int listContent) noexcept
{
    // Three candidates, most generous first. Each is accepted only when the
    // whole panel (including the section list) genuinely fits the height we were
    // given, so "does it need to scroll?" stays monotonic in the window height —
    // a taller window never suddenly needs a scrollbar the shorter one did not.
    //
    // The waveform / beat-count scope stays in every tier. Dropping it to zero
    // so a short window would not scroll made the visualizer vanish on the
    // height a laptop DAW actually opens at (~800px), with no scrollbar to
    // show that anything was missing.
    LayoutMetrics compact;
    compact.rowH     = 40;
    compact.statusH  = 24;
    compact.scopeH   = 56;
    compact.diagGap  = 8;
    compact.gap      = 8;
    compact.readoutH = 19;
    compact.headerH  = 20;
    compact.headerGap= 4;
    compact.shapeH   = 26;
    compact.shapeGap = 3;

    // Fits the 6-section default form in an 800px window with the scope shown.
    LayoutMetrics fitted;
    fitted.rowH      = 42;
    fitted.statusH   = 26;
    fitted.scopeH    = 64;
    fitted.diagGap   = 8;
    fitted.gap       = 8;
    fitted.readoutH  = 20;
    fitted.headerH   = 22;
    fitted.headerGap = 4;
    fitted.shapeH    = 26;
    fitted.shapeGap  = 4;

    LayoutMetrics full;
    full.scopeH = 80;

    if (bodyH >= full.fixedHeight() + listContent)   return full;
    if (bodyH >= fitted.fixedHeight() + listContent) return fitted;
    return compact;
}

void AccompanimentEditor::fitEditorToScreen()
{
    // Ask for a height that shows the whole panel, but never more than the
    // display can actually show: the old 1100px default on a 956px laptop
    // desktop opened with the bottom of the UI past the edge of the screen, and
    // some hosts (AU on macOS in particular) do not clamp that for us.
    constexpr int kDesignW    = 520;
    constexpr int kMinW       = 520;
    constexpr int kMaxW       = 720;
    constexpr int kDesignH    = 860;
    constexpr int kMinH       = 460;
    constexpr int kHostChrome = 96;    // host window frame + title bar

    // The usable height (menu bar and dock excluded) caps both the size we ask
    // for and the maximum we advertise. A VST3 host clamps a restored window
    // size to these limits via checkSizeConstraint, so a session saved on a
    // large monitor cannot re-open taller than the screen it is opened on.
    int maxH = 1800;

    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        if (display->userArea.getHeight() > 0)
            maxH = juce::jmax(kMinH, display->userArea.getHeight() - kHostChrome);

    setResizeLimits(kMinW, kMinH, kMaxW, maxH);
    setSize(kDesignW, juce::jmin(kDesignH, maxH));
}

void AccompanimentEditor::ContentComponent::paint(juce::Graphics& g)
{
    owner.paintContent(g);
}

void AccompanimentEditor::ContentComponent::resized()
{
    owner.layoutContent(getLocalBounds());
}

void AccompanimentEditor::paint(juce::Graphics& g)
{
    // The viewport paints nothing and the content covers it completely; this is
    // only a fallback so no frame is ever left unpainted.
    g.fillAll(juce::Colour(0xff111a10));
}

void AccompanimentEditor::paintContent(juce::Graphics& g)
{
    const auto bounds = content.getLocalBounds().toFloat();

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

    // ── 3. Control panel — a frosted surface, no outline ─────────────────────
    // The panel used to carry a 1px sage stroke, and so did every row, the scope
    // and the progress bar — four nested boxes in the same pen. The fill alone
    // separates the panel from the photograph; the horizontal rule that used to
    // sit under it separated nothing and is gone.
    g.setColour(juce::Colour(0xc4081208));
    g.fillRoundedRectangle(userPolicyArea.toFloat(), 6.0f);
}

void AccompanimentEditor::resized()
{
    contentViewport.setBounds(getLocalBounds());

    const int listContent = sectionListEditor ? sectionListEditor->getHeightHint() : 0;
    const int viewH = juce::jmax(1, contentViewport.getHeight());

    // Choose the metrics once per resize and reuse them in layoutContent, so the
    // tier cannot change between deciding the content height and laying it out.
    metrics = metricsForBodyHeight(viewH - kEditorChromeH, listContent);

    const int naturalH = metrics.fixedHeight() + listContent + kEditorChromeH;
    const int contentH = juce::jmax(naturalH, viewH);

    // Reserve the scrollbar's width up front. The content height does not depend
    // on the content width, so predicting whether the scrollbar will appear
    // avoids a feedback loop that would leave the panel one scrollbar too wide.
    const bool willScroll = contentH > viewH;
    const int contentW = juce::jmax(1, contentViewport.getWidth()
                                         - (willScroll ? contentViewport.getScrollBarThickness() : 0));

    content.setSize(contentW, contentH);
}

void AccompanimentEditor::layoutContent(juce::Rectangle<int> bounds)
{
    auto r = bounds.reduced(kEditorMargin);

    // ── Title row (fixed) ────────────────────────────────────────────────────
    auto titleRow = r.removeFromTop(kTitleRowH);
    playButton.setBounds(titleRow.removeFromRight(92));
    titleRow.removeFromRight(6);
    recordRiffButton.setBounds(titleRow.removeFromRight(100));
    titleRow.removeFromRight(6);
    forgetRiffButton.setBounds(titleRow.removeFromRight(56));
    titleRow.removeFromRight(6);
    versionLabel.setBounds(titleRow.removeFromRight(64));
    titleLabel.setBounds(titleRow);
    r.removeFromTop(kTitleGap);

    const int userTop = r.getY();

    // ── Adaptive vertical layout ─────────────────────────────────────────────
    // Fixed heights for the rows and the diagnostics; the editable sections list
    // is the only variable-height group. It is given its *full* content height so
    // the panel has exactly one scrollbar (the page) rather than a nested pair
    // that would fight over the mouse wheel; it still grows to absorb spare
    // height so the panel fills a taller window.
    const int listContent = sectionListEditor ? sectionListEditor->getHeightHint() : 0;
    const int sectionsH = juce::jmax(listContent, r.getHeight() - metrics.fixedHeight());

    auto row = r.removeFromTop(metrics.rowH);
    genreLabel.setBounds(row.removeFromLeft(kRowLabelW));
    genreCombo.setBounds(row);
    r.removeFromTop(metrics.gap);

    row = r.removeFromTop(metrics.rowH);
    swingLabel.setBounds(row.removeFromLeft(kRowLabelW));
    swingSlider.setBounds(row);
    r.removeFromTop(metrics.gap);

    row = r.removeFromTop(metrics.rowH);
    humanizeLabel.setBounds(row.removeFromLeft(kRowLabelW));
    humanizeSlider.setBounds(row);
    r.removeFromTop(metrics.gap);

    // ── PLAY MODE: the editable song form ────────────────────────────────────
    playModeHeader.setBounds(r.removeFromTop(metrics.headerH));
    r.removeFromTop(metrics.headerGap);

    auto listArea = r.removeFromTop(sectionsH);
    songSectionsViewport.setBounds(listArea);
    if (sectionListEditor)
        sectionListEditor->setSize(juce::jmax(1, songSectionsViewport.getMaximumVisibleWidth()),
                                   juce::jmax(listArea.getHeight(), sectionListEditor->getHeightHint()));
    r.removeFromTop(metrics.gap);

    // ── RECORD RIFF: lock hold + post-lock transition grammar ────────────────
    recordRiffHeader.setBounds(r.removeFromTop(metrics.headerH));
    r.removeFromTop(metrics.headerGap);

    row = r.removeFromTop(metrics.rowH);
    lockBarsLabel.setBounds(row.removeFromLeft(kRowLabelW));
    lockBarsSlider.setBounds(row);
    r.removeFromTop(metrics.gap);

    row = r.removeFromTop(metrics.rowH);
    transitionBarsLabel.setBounds(row.removeFromLeft(kRowLabelW));
    transitionBarsSlider.setBounds(row);
    r.removeFromTop(metrics.gap);

    row = r.removeFromTop(metrics.rowH);
    transitionSectionsLabel.setBounds(row.removeFromLeft(kRowLabelW));
    transitionSectionsSlider.setBounds(row);

    // The loop-shape pill sits directly under the slider it describes, indented
    // past the label column so it reads as part of that control.
    r.removeFromTop(metrics.shapeGap);
    formShapeComponent.setBounds(r.removeFromTop(metrics.shapeH).withTrimmedLeft(kRowLabelW));

    const int userBottom = r.getY();
    userPolicyArea = juce::Rectangle<int>(kEditorMargin, userTop,
                                          juce::jmax(1, bounds.getWidth() - 2 * kEditorMargin),
                                          juce::jmax(1, userBottom - userTop));

    r.removeFromTop(metrics.diagGap);

    // ── Status row: dot + phase/section text, section progress on the right ──
    auto statusRow = r.removeFromTop(metrics.statusH);
    const int progressW = juce::jmin(220, statusRow.getWidth() * 45 / 100);
    sectionProgressComponent.setBounds(statusRow.removeFromRight(progressW));
    statusRow.removeFromRight(12);
    statusDot.setBounds(statusRow.removeFromLeft(14).withSizeKeepingCentre(9, 9));
    statusRow.removeFromLeft(7);
    statusLabel.setBounds(statusRow);

    r.removeFromTop(metrics.gap);

    scopeComponent.setVisible(metrics.scopeH > 0);
    scopeComponent.setBounds(r.removeFromTop(metrics.scopeH));

    r.removeFromTop(4);

    // One quiet line of engine internals; leave the resize grip's corner clear.
    auto readoutRow = r.removeFromTop(metrics.readoutH);
    readoutRow.removeFromRight(18);
    readoutLabel.setBounds(readoutRow);
}
