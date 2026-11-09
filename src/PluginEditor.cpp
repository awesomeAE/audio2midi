#include "PluginEditor.h"

namespace
{
// What the engine can decode (Python's wave module, then libsndfile).
const juce::String audioExtensions = "wav;aif;aiff;flac;mp3;ogg";

juce::String formatDuration (double seconds)
{
    const auto total = (int) seconds;
    return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
}

juce::String displayName (const juce::String& instrument)
{
    const auto spaced = instrument.replaceCharacter ('_', ' ');
    return TRANS (spaced.substring (0, 1).toUpperCase() + spaced.substring (1));
}

const juce::StringArray themeNames { NEEDS_TRANS ("Default"), "Graphite" };
const juce::StringArray modelLabels { NEEDS_TRANS ("Small model"), NEEDS_TRANS ("Medium model"), NEEDS_TRANS ("Large model") };

juce::String noteCount (int notes)
{
    return TRANS ("%count% notes").replace ("%count%", juce::String (notes));
}

bool isBusy (const a2m::Status& status)
{
    return status.stage == a2m::Status::Stage::startingEngine || status.stage == a2m::Status::Stage::transcribing;
}
} // namespace

PianoRoll::PianoRoll (Audio2MidiProcessor& p) : processor (p)
{
    setTitle (TRANS ("Notes"));
    setDescription (TRANS ("Left and right arrow keys move the playhead by 5 seconds."));
    setWantsKeyboardFocus (true);
}

void PianoRoll::paint (juce::Graphics& g)
{
    const auto text = getLookAndFeel().findColour (juce::Label::textColourId);
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (text.withAlpha (0.05f));
    g.fillRoundedRectangle (bounds, 6.0f);

    const auto& sequence = processor.getPreviewSequence();
    const auto length = processor.getPreviewLength();
    int low = 127, high = 0;

    for (const auto* event : sequence)
        if (event->message.isNoteOn())
        {
            low = juce::jmin (low, event->message.getNoteNumber());
            high = juce::jmax (high, event->message.getNoteNumber());
        }

    if (length <= 0.0 || low > high)
    {
        g.setColour (text.withAlpha (0.4f));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (TRANS ("No notes to show"), getLocalBounds(), juce::Justification::centred);
    }
    else
    {
        const auto area = bounds.reduced (0.0f, 4.0f);
        const auto rowHeight = area.getHeight() / (float) (high - low + 1);
        const auto secondsToX = [&] (double seconds) { return area.getX() + (float) (seconds / length) * area.getWidth(); };

        for (const auto* event : sequence)
        {
            const auto& note = event->message;

            if (! note.isNoteOn())
                continue;

            const auto start = note.getTimeStamp();
            const auto end = event->noteOffObject != nullptr ? event->noteOffObject->message.getTimeStamp() : start + 0.1;
            const auto x = secondsToX (start);
            // One colour per MIDI channel, which the engine assigns per instrument.
            g.setColour (juce::Colour::fromHSV (std::fmod (0.58f + 0.17f * (float) note.getChannel(), 1.0f),
                                                pastelNotes ? 0.38f : 0.55f, pastelNotes ? 0.92f : 0.9f, 0.9f));
            g.fillRect (x, area.getY() + rowHeight * (float) (high - note.getNoteNumber()),
                        juce::jmax (1.5f, secondsToX (end) - x), juce::jmax (1.0f, rowHeight - 0.5f));
        }
    }

    if (length > 0.0)
    {
        g.setColour (text);
        g.fillRect ((float) (processor.getPreviewPosition() / length) * bounds.getWidth(), 0.0f, 1.5f, bounds.getHeight());
    }
}

void PianoRoll::mouseDown (const juce::MouseEvent& e) { seekTo ((float) e.x); }
void PianoRoll::mouseDrag (const juce::MouseEvent& e) { seekTo ((float) e.x); }

bool PianoRoll::keyPressed (const juce::KeyPress& key)
{
    const auto step = key == juce::KeyPress::leftKey ? -5.0 : key == juce::KeyPress::rightKey ? 5.0 : 0.0;

    if (step == 0.0)
        return false;

    processor.seekPreview (processor.getPreviewPosition() + step);
    repaint();
    return true;
}

void PianoRoll::seekTo (float x)
{
    processor.seekPreview (juce::jlimit (0.0f, 1.0f, x / (float) getWidth()) * processor.getPreviewLength());
    repaint();
}

GraphiteLookAndFeel::GraphiteLookAndFeel()
    : juce::LookAndFeel_V4 ({ 0xff141414, 0xff1f1f1f, 0xff1c1c1c, 0xff353535, 0xfff2f2f2,
                              0xffececec, 0xffffffff, 0xff2b2b2b, 0xfff2f2f2 })
{
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1f1f1f));
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff1f1f1f));
    setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff141414));
    setColour (juce::TextEditor::highlightColourId, juce::Colour (0xff3d5f8a));
    setColour (juce::Slider::trackColourId, juce::Colour (0xffececec));
    setColour (juce::Slider::backgroundColourId, juce::Colour (0xff353535));
    setColour (juce::ProgressBar::backgroundColourId, juce::Colour (0xff1f1f1f));
    setColour (juce::ProgressBar::foregroundColourId, juce::Colour (0xff8db4e2));
}

void GraphiteLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& background,
                                                bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto fill = background.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f);

    if (down || highlighted)
        fill = fill.brighter (down ? 0.25f : 0.12f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (findColour (juce::ComboBox::outlineColourId).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);
}

Audio2MidiEditor::Audio2MidiEditor (Audio2MidiProcessor& p)
    : AudioProcessorEditor (p), audioProcessor (p)
{
    for (auto* button : { &chooseButton, &recordButton, &instrumentsButton, &transcribeButton, &revealButton, &playButton })
        addAndMakeVisible (button);

    chooseButton.onClick = [this] { chooseFile(); };
    recordButton.onClick = [this] { toggleRecording(); };
    instrumentsButton.onClick = [this] { showInstrumentMenu(); };
    transcribeButton.onClick = [this] { isBusy (status) ? audioProcessor.cancelTranscription() : audioProcessor.transcribe(); };
    revealButton.onClick = [this]
    {
        if (! status.parts.empty())
            status.parts.front().file.revealToUser();
    };
    playButton.onClick = [this] { audioProcessor.isPreviewing() ? audioProcessor.stopPreview() : audioProcessor.startPreview(); };

    jassert (modelLabels.size() == a2m::modelNames().size());
    for (const auto& label : modelLabels)
        modelBox.addItem (TRANS (label), modelBox.getNumItems() + 1);

    modelBox.setTitle (TRANS ("Model size"));
    modelBox.setTooltip (TRANS ("Larger models are more accurate but slower. Each size downloads its own weights."));
    modelBox.onChange = [this] { audioProcessor.model = shownModel = modelBox.getSelectedItemIndex(); };
    addAndMakeVisible (modelBox);

    for (const auto& name : themeNames)
        themeBox.addItem (TRANS (name), themeBox.getNumItems() + 1);
    themeBox.setTitle (TRANS ("Theme"));
    themeBox.onChange = [this]
    {
        // The untranslated name, so the choice survives a change of system language.
        settings.setValue ("theme", themeNames[themeBox.getSelectedItemIndex()]);
        applyTheme (themeBox.getSelectedItemIndex());
    };
    addAndMakeVisible (themeBox);

    alignButton.setToggleState (audioProcessor.alignToBeats, juce::dontSendNotification);
    alignButton.onClick = [this] { audioProcessor.alignToBeats = alignButton.getToggleState(); };
    addAndMakeVisible (alignButton);

    mixSlider.setTitle (TRANS ("Audio and MIDI mix"));
    mixSlider.setRange (0.0, 1.0);
    mixSlider.setValue (audioProcessor.previewMix, juce::dontSendNotification);
    mixSlider.onValueChange = [this] { audioProcessor.previewMix = (float) mixSlider.getValue(); };
    addAndMakeVisible (mixSlider);
    addAndMakeVisible (positionLabel);
    addAndMakeVisible (pianoRoll);

    addChildComponent (progressBar);
    addAndMakeVisible (statusLabel);
    addAndMakeVisible (partsList);
    partsList.setRowHeight (32);

    details.setMultiLine (true);
    details.setReadOnly (true);
    details.setScrollbarsShown (true);
    details.setTitle (TRANS ("Error details"));
    addChildComponent (details);

    refreshInstrumentButton();
    themeBox.setSelectedItemIndex (juce::jmax (0, themeNames.indexOf (settings.getValue ("theme"))), juce::dontSendNotification);
    applyTheme (themeBox.getSelectedItemIndex());
    setSize (640, 620);
    timerCallback();
    startTimerHz (30);
}

Audio2MidiEditor::~Audio2MidiEditor() { setLookAndFeel (nullptr); }

void Audio2MidiEditor::applyTheme (int index)
{
    const auto graphiteTheme = themeNames[index] == "Graphite";
    setLookAndFeel (graphiteTheme ? &graphite : nullptr);
    pianoRoll.pastelNotes = graphiteTheme;
    // Graphite draws each part as a separate outlined row with a gap below it.
    partsList.setRowHeight (graphiteTheme ? 33 : 32);
    repaint();
}

void Audio2MidiEditor::paint (juce::Graphics& g)
{
    auto& lf = getLookAndFeel();
    const auto text = lf.findColour (juce::Label::textColourId);
    g.fillAll (lf.findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("Audio2MIDI", titleArea, juce::Justification::centredLeft);

    const auto zone = dropZone.toFloat().reduced (1.0f);
    const auto radius = isGraphite() ? 12.0f : 8.0f;
    g.setColour (text.withAlpha (dragOver ? 0.12f : 0.04f));
    g.fillRoundedRectangle (zone, radius);

    juce::Path border, dashed;
    border.addRoundedRectangle (zone, radius);
    const float dashes[] = { 6.0f, 4.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, border, dashes, 2);
    g.setColour (text.withAlpha (dragOver ? 0.9f : 0.35f));
    g.fillPath (dashed);

    const auto source = audioProcessor.getSource();
    const auto recording = (audioProcessor.isRecordingPaused() ? TRANS ("Recording paused until playback starts") : TRANS ("Recording"))
                         + "  " + formatDuration (audioProcessor.recordedSeconds());
    const auto line = audioProcessor.isRecording() ? recording
                    : source.existsAsFile()   ? source.getFileName()
                                              : TRANS ("Drop an audio file");
    g.setColour (text);
    g.setFont (juce::FontOptions (16.0f));
    g.drawFittedText (line, dropZone.reduced (16), juce::Justification::centred, 2);

    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    const auto partsTitle = TRANS ("Parts");
    g.drawText (partsTitle, partsHeader, juce::Justification::centredLeft);
    const auto titleWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), partsTitle));
    g.setColour (text.withAlpha (0.6f));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (TRANS ("Drag into your DAW"), partsHeader.withTrimmedLeft (titleWidth + 12), juce::Justification::centredLeft);

    g.setColour (text.withAlpha (mixSlider.isEnabled() ? 0.8f : 0.4f));
    g.drawText (TRANS ("Audio"), mixArea.withWidth (44), juce::Justification::centredRight);
    g.drawText ("MIDI", mixArea.withTrimmedLeft (mixArea.getWidth() - 40), juce::Justification::centredLeft);
}

void Audio2MidiEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    titleArea = area.removeFromTop (28);
    themeBox.setBounds (titleArea.removeFromRight (116).withSizeKeepingCentre (116, 26));
    area.removeFromTop (8);
    dropZone = area.removeFromTop (72);
    area.removeFromTop (12);

    auto controls = area.removeFromTop (28);
    chooseButton.setBounds (controls.removeFromLeft (104));
    controls.removeFromLeft (8);
    recordButton.setBounds (controls.removeFromLeft (84));
    transcribeButton.setBounds (controls.removeFromRight (104));
    controls.removeFromRight (8);
    instrumentsButton.setBounds (controls.removeFromRight (168));
    controls.removeFromRight (8);
    modelBox.setBounds (controls.removeFromRight (116));

    area.removeFromTop (12);
    auto statusRow = area.removeFromTop (22);
    progressBar.setBounds (statusRow.removeFromRight (180));
    statusLabel.setBounds (statusRow);

    area.removeFromTop (12);
    auto previewRow = area.removeFromTop (28);
    playButton.setBounds (previewRow.removeFromLeft (72));
    previewRow.removeFromLeft (8);
    positionLabel.setBounds (previewRow.removeFromLeft (96));
    alignButton.setBounds (previewRow.removeFromRight (128));
    previewRow.removeFromRight (12);
    mixArea = previewRow;
    mixSlider.setBounds (previewRow.reduced (48, 0));

    area.removeFromTop (8);
    pianoRoll.setBounds (area.removeFromTop (150));

    area.removeFromTop (8);
    partsHeader = area.removeFromTop (28);
    revealButton.setBounds (partsHeader.removeFromRight (128));
    area.removeFromTop (4);
    partsList.setBounds (area);
    details.setBounds (area);
}

bool Audio2MidiEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && ! audioProcessor.isRecording() && juce::File (files[0]).hasFileExtension (audioExtensions);
}

void Audio2MidiEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    dragOver = true;
    repaint();
}

void Audio2MidiEditor::fileDragExit (const juce::StringArray&)
{
    dragOver = false;
    repaint();
}

void Audio2MidiEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    audioProcessor.setSource (juce::File (files[0]));
    repaint();
}

int Audio2MidiEditor::getNumRows() { return (int) status.parts.size(); }

void Audio2MidiEditor::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) status.parts.size()))
        return;

    const auto& part = status.parts[(size_t) row];
    const auto text = getLookAndFeel().findColour (juce::Label::textColourId);

    if (isGraphite())
    {
        const auto card = juce::Rectangle<float> ((float) width, (float) height - 4.0f).reduced (0.5f);
        g.setColour (juce::Colour (0xff2b2b2b).withAlpha (selected ? 1.0f : 0.0f));
        g.fillRoundedRectangle (card, 8.0f);
        g.setColour (findColour (juce::ComboBox::outlineColourId).withAlpha (selected ? 0.0f : 1.0f));
        g.drawRoundedRectangle (card, 8.0f, 1.0f);

        juce::AttributedString label;
        label.append (TRANS (part.name) + " ", juce::FontOptions (14.0f), text);
        label.append ("(" + noteCount (part.notes) + ")", juce::FontOptions (12.0f), text.withAlpha (0.5f));
        label.setJustification (juce::Justification::centredLeft);
        label.draw (g, card.reduced (14.0f, 0.0f));
        return;
    }

    if (selected)
        g.fillAll (getLookAndFeel().findColour (juce::TextEditor::highlightColourId));

    const auto bounds = juce::Rectangle<int> (width, height).reduced (10, 0);
    g.setColour (text);
    g.setFont (juce::FontOptions (15.0f, row == 0 ? juce::Font::bold : juce::Font::plain));
    g.drawText (TRANS (part.name), bounds, juce::Justification::centredLeft);
    g.setColour (text.withAlpha (0.6f));
    g.drawText (noteCount (part.notes), bounds, juce::Justification::centredRight);
}

juce::String Audio2MidiEditor::getNameForRow (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) status.parts.size()))
        return {};

    const auto& part = status.parts[(size_t) row];
    return TRANS (part.name) + ", " + noteCount (part.notes);
}

void Audio2MidiEditor::selectedRowsChanged (int) { previewSelectedPart(); }

juce::var Audio2MidiEditor::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
    // Start the system file drag as soon as the row is dragged, the way a file dragged out of
    // Finder arrives, instead of turning an internal drag into one at the window's edge.
    // Returning no description keeps the list from starting an internal drag as well.
    if (draggingPart || rows.isEmpty() || ! juce::isPositiveAndBelow (rows[0], (int) status.parts.size()))
        return {};

    draggingPart = juce::DragAndDropContainer::performExternalDragDropOfFiles ({ status.parts[(size_t) rows[0]].file.getFullPathName() },
                                                                              false, this, [this] { draggingPart = false; });
    return {};
}

void Audio2MidiEditor::previewSelectedPart()
{
    const auto row = partsList.getSelectedRow();
    audioProcessor.setPreviewMidi (juce::isPositiveAndBelow (row, (int) status.parts.size()) ? status.parts[(size_t) row].file : juce::File());
    pianoRoll.repaint();
}

void Audio2MidiEditor::timerCallback()
{
    const auto previous = std::exchange (status, audioProcessor.getStatus());
    const auto busy = isBusy (status);
    const auto recording = audioProcessor.isRecording();
    const auto failed = status.stage == a2m::Status::Stage::failed;
    const auto previewing = audioProcessor.isPreviewing();
    const auto length = audioProcessor.getPreviewLength();

    if (recording && audioProcessor.hasReachedRecordingLimit())
        notice = audioProcessor.stopRecording();

    progress = status.progress;
    progressBar.setVisible (busy);
    chooseButton.setEnabled (! busy && ! recording);
    recordButton.setEnabled (! busy);
    recordButton.setButtonText (recording ? TRANS ("Stop") : TRANS ("Record"));
    instrumentsButton.setEnabled (! busy);
    modelBox.setEnabled (! busy);

    // Only follow real changes, e.g. a restored state: onChange arrives asynchronously, so a
    // selection would otherwise be reset to the old model before it reaches the processor.
    if (audioProcessor.model != shownModel)
        modelBox.setSelectedItemIndex (shownModel = audioProcessor.model, juce::dontSendNotification);

    transcribeButton.setButtonText (busy ? TRANS ("Cancel") : TRANS ("Transcribe"));
    transcribeButton.setEnabled (busy || (! recording && audioProcessor.getSource().existsAsFile()));
    revealButton.setEnabled (! status.parts.empty());

    playButton.setButtonText (previewing ? TRANS ("Stop") : TRANS ("Play"));
    playButton.setEnabled (previewing || (! recording && length > 0.0 && (audioProcessor.canPreviewMidi() || audioProcessor.getSource().existsAsFile())));
    positionLabel.setText (length > 0.0 ? formatDuration (audioProcessor.getPreviewPosition()) + " / " + formatDuration (length) : juce::String(),
                           juce::dontSendNotification);
    mixSlider.setEnabled (audioProcessor.canPreviewMidi() && ! status.parts.empty() && audioProcessor.getSource().existsAsFile());
    alignButton.setEnabled (! status.alignedParts.empty());
    // The host can restore a state while the editor is open.
    alignButton.setToggleState (audioProcessor.alignToBeats, juce::dontSendNotification);

    if (! mixSlider.isMouseButtonDown())
        mixSlider.setValue (audioProcessor.previewMix, juce::dontSendNotification);
    alignButton.setTooltip (status.alignedParts.empty() && status.alignError.isNotEmpty() ? status.alignError
                                                                                        : TRANS ("Moves every note onto the detected beat grid."));

    const auto message = audioProcessor.isPreviewWaitingForHost()     ? TRANS ("Start playback in your DAW to hear the preview.")
                       : status.stage == a2m::Status::Stage::idle         ? notice
                       : status.stage == a2m::Status::Stage::transcribing ? TRANS ("Transcribing, %notes% notes").replace ("%notes%", juce::String (status.notes))
                       : failed                                           ? TRANS ("Transcription failed")
                                                                          : status.message;
    statusLabel.setText (message, juce::dontSendNotification);

    details.setVisible (failed);
    partsList.setVisible (! failed);

    if (failed && details.getText() != status.message)
        details.setText (status.message, false);

    if (previous.parts.size() != status.parts.size()
        || (! status.parts.empty() && previous.parts.front().file != status.parts.front().file))
    {
        partsList.updateContent();

        if (status.parts.empty())
            partsList.deselectAllRows();
        else
            partsList.selectRow (juce::jlimit (0, (int) status.parts.size() - 1, partsList.getSelectedRow()));

        previewSelectedPart();
    }

    if (previewing || previous.stage != status.stage)
        pianoRoll.repaint();

    if (recording)
        repaint (dropZone);
}

void Audio2MidiEditor::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> (TRANS ("Choose audio to transcribe"), juce::File(),
                                                   "*." + audioExtensions.replace (";", ";*."));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (const auto file = fc.getResult(); file.existsAsFile())
                              {
                                  audioProcessor.setSource (file);
                                  repaint();
                              }
                          });
}

void Audio2MidiEditor::toggleRecording()
{
    notice.clear();

    if (audioProcessor.isRecording())
        notice = audioProcessor.stopRecording();
    else if (! audioProcessor.startRecording())
        notice = TRANS ("Recording needs an audio input and a running audio engine.");

    repaint();
}

void Audio2MidiEditor::showInstrumentMenu()
{
    juce::PopupMenu menu;
    menu.addItem (TRANS ("Detect automatically"), true, audioProcessor.instruments.isEmpty(), [this]
    {
        audioProcessor.instruments.clear();
        refreshInstrumentButton();
    });
    menu.addSeparator();

    for (const auto& name : a2m::instrumentNames())
        menu.addItem (displayName (name), true, audioProcessor.instruments.contains (name), [this, name]
        {
            if (audioProcessor.instruments.contains (name))
                audioProcessor.instruments.removeString (name);
            else
                audioProcessor.instruments.add (name);

            refreshInstrumentButton();
            showInstrumentMenu(); // reopen so several instruments can be picked in a row
        });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (instrumentsButton).withDeletionCheck (*this));
}

void Audio2MidiEditor::refreshInstrumentButton()
{
    const auto& selected = audioProcessor.instruments;
    instrumentsButton.setButtonText (selected.isEmpty()   ? TRANS ("Instruments: auto")
                                   : selected.size() == 1 ? TRANS ("Instruments: %name%").replace ("%name%", displayName (selected[0]))
                                                          : TRANS ("Instruments: %count% selected").replace ("%count%", juce::String (selected.size())));
}
