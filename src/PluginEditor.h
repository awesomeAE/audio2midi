#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

/** The preview's notes over time; click or use the arrow keys to move the playhead. */
class PianoRoll final : public juce::Component
{
public:
    explicit PianoRoll (Audio2MidiProcessor&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void seekTo (float x);

    Audio2MidiProcessor& processor;
};

class Audio2MidiEditor final : public juce::AudioProcessorEditor,
                               public juce::FileDragAndDropTarget,
                               private juce::ListBoxModel,
                               private juce::Timer
{
public:
    explicit Audio2MidiEditor (Audio2MidiProcessor&);
    ~Audio2MidiEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    juce::String getNameForRow (int row) override;
    void selectedRowsChanged (int lastRowSelected) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;

    void timerCallback() override;
    void chooseFile();
    void toggleRecording();
    void showInstrumentMenu();
    void refreshInstrumentButton();
    void previewSelectedPart();

    Audio2MidiProcessor& audioProcessor;
    a2m::Status status;
    juce::String notice;
    bool dragOver = false, draggingPart = false;
    double progress = 0.0;
    juce::Rectangle<int> titleArea, dropZone, partsHeader, mixArea;

    juce::TextButton chooseButton { "Choose file" }, recordButton { "Record" },
                     instrumentsButton, transcribeButton { "Transcribe" }, revealButton { "Show files" },
                     playButton { "Play" };
    juce::ComboBox modelBox;
    int shownModel = -1; // the processor's model as modelBox last showed it
    juce::ToggleButton alignButton { "Align to beats" };
    juce::Slider mixSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label positionLabel;
    juce::ProgressBar progressBar { progress };
    juce::Label statusLabel;
    PianoRoll pianoRoll { audioProcessor };
    juce::ListBox partsList { "Parts", this };
    juce::TextEditor details;
    juce::TooltipWindow tooltips { this };
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Audio2MidiEditor)
};
