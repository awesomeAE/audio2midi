#pragma once

#include "Transcription.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

class Audio2MidiProcessor final : public juce::AudioProcessor
{
public:
    Audio2MidiProcessor();
    ~Audio2MidiProcessor() override;

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Message thread only.
    void setSource (const juce::File&);
    juce::File getSource() const { return source; }
    bool startRecording();
    void stopRecording();
    bool isRecording() const { return recorder != nullptr; }
    double recordedSeconds() const;
    void transcribe();
    void cancelTranscription();
    a2m::Status getStatus() const;

    juce::StringArray instruments; // empty: let the model decide

private:
    juce::SharedResourcePointer<a2m::Engine> engine;
    const juce::String clientId = juce::Uuid().toString();
    const juce::File workDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Audio2MIDI").getChildFile (clientId);
    juce::File source;

    juce::TimeSliceThread writerThread { "Audio2MIDI recorder" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> recorder;
    juce::SpinLock recorderLock; // the audio thread only ever try-locks it
    juce::AudioFormatWriter::ThreadedWriter* activeRecorder = nullptr; // guarded by recorderLock
    int recordingChannels = 0;                                          // guarded by recorderLock
    std::atomic<juce::int64> recordedSamples { 0 };
    juce::File recordingFile;

    mutable juce::CriticalSection statusLock;
    a2m::Status status;
    std::unique_ptr<a2m::TranscriptionJob> job; // declared last: destroyed (and joined) first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Audio2MidiProcessor)
};
