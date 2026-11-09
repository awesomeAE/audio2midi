#pragma once

#include "Transcription.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

class Audio2MidiProcessor final : public juce::AudioProcessor
{
public:
    Audio2MidiProcessor();
    ~Audio2MidiProcessor() override;

    void prepareToPlay (double, int) override;
    void releaseResources() override;
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
    /** Returns why the take could not be used, or an empty string. */
    juce::String stopRecording();
    bool isRecording() const { return recorder != nullptr; }
    bool isRecordingPaused() const { return ! hostPlaying; }
    bool hasReachedRecordingLimit() const { return recordingFull; }
    double recordedSeconds() const;
    void transcribe();
    void cancelTranscription();
    /** With alignToBeats set and an aligned version available, `parts` holds the aligned files. */
    a2m::Status getStatus() const;

    // Preview, message thread only. It plays through the plugin's output, replacing the input.
    void setPreviewMidi (const juce::File&);
    const juce::MidiMessageSequence& getPreviewSequence() const { return sequence; } // timestamps in seconds
    void startPreview();
    void stopPreview();
    bool isPreviewing() const { return previewPlaying; }
    void seekPreview (double seconds);
    double getPreviewPosition() const { return previewSeconds; }
    double getPreviewLength() const { return previewLength; }
    /** True while previewing but the host has stopped calling processBlock, e.g. Logic with the transport stopped. */
    bool isPreviewWaitingForHost() const;
    bool canPreviewMidi() const;

    juce::StringArray instruments; // empty: let the model decide
    std::atomic<int> model { a2m::modelNames().indexOf (a2m::defaultModel) }; // index into a2m::modelNames()
    std::atomic<bool> alignToBeats { false };
    std::atomic<float> previewMix { 0.5f }; // 0: source audio only, 1: MIDI only

private:
    void record (const juce::AudioBuffer<float>&);
    void renderPreview (juce::AudioBuffer<float>&);
    void createSynth();
    void updatePreviewLength();

    juce::SharedResourcePointer<a2m::Engine> engine;
    const juce::String clientId = juce::Uuid().toString();
    const juce::File workDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Audio2MIDI").getChildFile (clientId);
    juce::File source;

    juce::TimeSliceThread ioThread { "Audio2MIDI disk I/O" }; // recorder writes and preview reads
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> recorder;
    juce::SpinLock recorderLock; // the audio thread only ever try-locks it
    juce::AudioFormatWriter::ThreadedWriter* activeRecorder = nullptr; // guarded by recorderLock
    int recordingChannels = 0;                                          // guarded by recorderLock
    juce::int64 recordingLimit = 0;                                     // guarded by recorderLock, in samples
    std::atomic<juce::int64> recordedSamples { 0 };
    std::atomic<int> droppedBlocks { 0 };
    std::atomic<bool> recordingFull { false }, hostPlaying { true };
    juce::File recordingFile;

    juce::AudioFormatManager formats;
    std::unique_ptr<juce::AudioFormatReaderSource> sourceReader;
    double sourceSeconds = 0.0;
    juce::AudioTransportSource transport;
    juce::MidiMessageSequence sequence;
    juce::SpinLock previewLock; // the audio thread only ever try-locks it
    std::unique_ptr<juce::AudioPluginInstance> synth; // assigned under previewLock
    juce::MidiMessageSequence playingSequence;        // guarded by previewLock
    bool hasSourceAudio = false;                      // guarded by previewLock
    int nextEvent = 0;                                // audio thread: index into playingSequence
    juce::AudioBuffer<float> sourceBuffer, synthBuffer;
    juce::MidiBuffer synthMidi;
    std::atomic<bool> previewPlaying { false };
    std::atomic<double> previewSeconds { 0.0 }, previewLength { 0.0 };
    std::atomic<double> seekTarget { 0.0 }; // negative: none pending; the audio thread restarts the synth there
    std::atomic<juce::uint32> lastBlockTime { 0 };

    mutable juce::CriticalSection statusLock;
    a2m::Status status;
    juce::String resultName; // guarded by statusLock: names the part files of a restored result
    std::unique_ptr<a2m::TranscriptionJob> job; // declared last: destroyed (and joined) first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Audio2MidiProcessor)
};
