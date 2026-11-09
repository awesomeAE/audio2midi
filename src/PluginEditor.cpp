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
    return spaced.substring (0, 1).toUpperCase() + spaced.substring (1);
}

bool isBusy (const a2m::Status& status)
{
    return status.stage == a2m::Status::Stage::startingEngine || status.stage == a2m::Status::Stage::transcribing;
}
} // namespace

Audio2MidiEditor::Audio2MidiEditor (Audio2MidiProcessor& p)
    : AudioProcessorEditor (p), audioProcessor (p)
{
    for (auto* button : { &chooseButton, &recordButton, &instrumentsButton, &transcribeButton, &revealButton })
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

    addChildComponent (progressBar);
    addAndMakeVisible (statusLabel);
    addAndMakeVisible (partsList);
    partsList.setRowHeight (32);

    details.setMultiLine (true);
    details.setReadOnly (true);
    details.setScrollbarsShown (true);
    details.setTitle ("Error details");
    addChildComponent (details);

    refreshInstrumentButton();
    setSize (560, 460);
    timerCallback();
    startTimerHz (15);
}

Audio2MidiEditor::~Audio2MidiEditor() = default;

void Audio2MidiEditor::paint (juce::Graphics& g)
{
    auto& lf = getLookAndFeel();
    const auto text = lf.findColour (juce::Label::textColourId);
    g.fillAll (lf.findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("Audio2MIDI", titleArea, juce::Justification::centredLeft);

    const auto zone = dropZone.toFloat().reduced (1.0f);
    g.setColour (text.withAlpha (dragOver ? 0.12f : 0.04f));
    g.fillRoundedRectangle (zone, 8.0f);

    juce::Path border, dashed;
    border.addRoundedRectangle (zone, 8.0f);
    const float dashes[] = { 6.0f, 4.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, border, dashes, 2);
    g.setColour (text.withAlpha (dragOver ? 0.9f : 0.35f));
    g.fillPath (dashed);

    const auto source = audioProcessor.getSource();
    const auto line = audioProcessor.isRecording() ? "Recording  " + formatDuration (audioProcessor.recordedSeconds())
                    : source.existsAsFile()   ? source.getFileName()
                                              : juce::String ("Drop an audio file");
    g.setColour (text);
    g.setFont (juce::FontOptions (16.0f));
    g.drawFittedText (line, dropZone.reduced (16), juce::Justification::centred, 2);

    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("Parts", partsHeader, juce::Justification::centredLeft);
    g.setColour (text.withAlpha (0.6f));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("Drag into your DAW", partsHeader.withTrimmedLeft (48), juce::Justification::centredLeft);
}

void Audio2MidiEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    titleArea = area.removeFromTop (28);
    area.removeFromTop (8);
    dropZone = area.removeFromTop (96);
    area.removeFromTop (12);

    auto controls = area.removeFromTop (28);
    chooseButton.setBounds (controls.removeFromLeft (104));
    controls.removeFromLeft (8);
    recordButton.setBounds (controls.removeFromLeft (84));
    transcribeButton.setBounds (controls.removeFromRight (104));
    controls.removeFromRight (8);
    instrumentsButton.setBounds (controls.removeFromRight (180));

    area.removeFromTop (12);
    auto statusRow = area.removeFromTop (22);
    progressBar.setBounds (statusRow.removeFromRight (180));
    statusLabel.setBounds (statusRow);

    area.removeFromTop (8);
    partsHeader = area.removeFromTop (28);
    revealButton.setBounds (partsHeader.removeFromRight (104));
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

bool Audio2MidiEditor::shouldDropFilesWhenDraggedExternally (const juce::DragAndDropTarget::SourceDetails& dragged,
                                                             juce::StringArray& files, bool& canMoveFiles)
{
    const auto row = static_cast<int> (dragged.description);

    if (! juce::isPositiveAndBelow (row, (int) status.parts.size()))
        return false;

    files.add (status.parts[(size_t) row].file.getFullPathName());
    canMoveFiles = false;
    return true;
}

int Audio2MidiEditor::getNumRows() { return (int) status.parts.size(); }

void Audio2MidiEditor::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) status.parts.size()))
        return;

    const auto& part = status.parts[(size_t) row];
    const auto text = getLookAndFeel().findColour (juce::Label::textColourId);

    if (selected)
        g.fillAll (getLookAndFeel().findColour (juce::TextEditor::highlightColourId));

    const auto bounds = juce::Rectangle<int> (width, height).reduced (10, 0);
    g.setColour (text);
    g.setFont (juce::FontOptions (15.0f, row == 0 ? juce::Font::bold : juce::Font::plain));
    g.drawText (part.name, bounds, juce::Justification::centredLeft);
    g.setColour (text.withAlpha (0.6f));
    g.drawText (juce::String (part.notes) + " notes", bounds, juce::Justification::centredRight);
}

juce::String Audio2MidiEditor::getNameForRow (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) status.parts.size()))
        return {};

    const auto& part = status.parts[(size_t) row];
    return part.name + ", " + juce::String (part.notes) + " notes";
}

juce::var Audio2MidiEditor::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
    return rows.isEmpty() ? juce::var() : juce::var (rows[0]);
}

void Audio2MidiEditor::timerCallback()
{
    const auto previous = std::exchange (status, audioProcessor.getStatus());
    const auto busy = isBusy (status);
    const auto recording = audioProcessor.isRecording();
    const auto failed = status.stage == a2m::Status::Stage::failed;

    progress = status.progress;
    progressBar.setVisible (busy);
    chooseButton.setEnabled (! busy && ! recording);
    recordButton.setEnabled (! busy);
    recordButton.setButtonText (recording ? "Stop" : "Record");
    instrumentsButton.setEnabled (! busy);
    transcribeButton.setButtonText (busy ? "Cancel" : "Transcribe");
    transcribeButton.setEnabled (busy || (! recording && audioProcessor.getSource().existsAsFile()));
    revealButton.setEnabled (! status.parts.empty());

    const auto message = status.stage == a2m::Status::Stage::idle         ? notice
                       : status.stage == a2m::Status::Stage::transcribing ? "Transcribing, " + juce::String (status.notes) + " notes"
                       : failed                                           ? juce::String ("Transcription failed")
                                                                          : status.message;
    statusLabel.setText (message, juce::dontSendNotification);

    details.setVisible (failed);
    partsList.setVisible (! failed);

    if (failed && details.getText() != status.message)
        details.setText (status.message, false);

    if (previous.parts.size() != status.parts.size()
        || (! status.parts.empty() && previous.parts.front().file != status.parts.front().file))
        partsList.updateContent();

    if (recording)
        repaint (dropZone);
}

void Audio2MidiEditor::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose audio to transcribe", juce::File(),
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
        audioProcessor.stopRecording();
    else if (! audioProcessor.startRecording())
        notice = "Recording needs an audio input and a running audio engine.";

    repaint();
}

void Audio2MidiEditor::showInstrumentMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("Detect automatically", true, audioProcessor.instruments.isEmpty(), [this]
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
    instrumentsButton.setButtonText ("Instruments: " + (selected.isEmpty()   ? juce::String ("auto")
                                                        : selected.size() == 1 ? displayName (selected[0])
                                                                               : juce::String (selected.size()) + " selected"));
}
