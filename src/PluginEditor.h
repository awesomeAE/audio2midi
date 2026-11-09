#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

class Audio2MidiEditor final : public juce::AudioProcessorEditor,
                               public juce::FileDragAndDropTarget,
                               public juce::DragAndDropContainer,
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

protected:
    bool shouldDropFilesWhenDraggedExternally (const juce::DragAndDropTarget::SourceDetails&,
                                               juce::StringArray& files, bool& canMoveFiles) override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    juce::String getNameForRow (int row) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;

    void timerCallback() override;
    void chooseFile();
    void toggleRecording();
    void showInstrumentMenu();
    void refreshInstrumentButton();

    Audio2MidiProcessor& audioProcessor;
    a2m::Status status;
    juce::String notice;
    bool dragOver = false;
    double progress = 0.0;
    juce::Rectangle<int> titleArea, dropZone, partsHeader;

    juce::TextButton chooseButton { "Choose file" }, recordButton { "Record" },
                     instrumentsButton, transcribeButton { "Transcribe" }, revealButton { "Show files" };
    juce::ProgressBar progressBar { progress };
    juce::Label statusLabel;
    juce::ListBox partsList { "Parts", this };
    juce::TextEditor details;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Audio2MidiEditor)
};
