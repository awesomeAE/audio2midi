#include "PluginProcessor.h"
#include "PluginEditor.h"

Audio2MidiProcessor::Audio2MidiProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

Audio2MidiProcessor::~Audio2MidiProcessor()
{
    job.reset();
    stopRecording();
    writerThread.stopThread (2000);
    workDir.deleteRecursively();
}

bool Audio2MidiProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
        && layouts.getMainInputChannelSet() == out;
}

void Audio2MidiProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Audio passes through untouched; while recording, a copy goes to the writer's FIFO.
    const juce::SpinLock::ScopedTryLockType lock (recorderLock);

    if (lock.isLocked() && activeRecorder != nullptr && buffer.getNumChannels() >= recordingChannels
        && activeRecorder->write (buffer.getArrayOfReadPointers(), buffer.getNumSamples()))
        recordedSamples += buffer.getNumSamples();
}

juce::AudioProcessorEditor* Audio2MidiProcessor::createEditor() { return new Audio2MidiEditor (*this); }

void Audio2MidiProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement state ("Audio2MIDI");
    state.setAttribute ("instruments", instruments.joinIntoString (","));
    copyXmlToBinary (state, destData);
}

void Audio2MidiProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto state = getXmlFromBinary (data, sizeInBytes); state != nullptr && state->hasTagName ("Audio2MIDI"))
    {
        juce::StringArray restored;

        // Ignore names a different engine version may not know.
        for (const auto& name : juce::StringArray::fromTokens (state->getStringAttribute ("instruments"), ",", {}))
            if (a2m::instrumentNames().contains (name))
                restored.add (name);

        instruments = restored;
    }
}

void Audio2MidiProcessor::setSource (const juce::File& file)
{
    cancelTranscription();
    source = file;
}

void Audio2MidiProcessor::cancelTranscription()
{
    job.reset();
    const juce::ScopedLock sl (statusLock);
    status = {};
}

bool Audio2MidiProcessor::startRecording()
{
    const auto sampleRate = getSampleRate();
    const auto channels = getTotalNumInputChannels();

    if (sampleRate <= 0 || channels == 0 || ! workDir.createDirectory())
        return false;

    stopRecording();
    recordingFile = workDir.getChildFile ("Recording " + juce::Time::getCurrentTime().formatted ("%H-%M-%S") + ".wav");
    std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (recordingFile);

    if (static_cast<juce::FileOutputStream*> (out.get())->failedToOpen())
        return false;

    // 16-bit PCM: the engine's WAV reader is Python's wave module.
    auto writer = juce::WavAudioFormat().createWriterFor (out, juce::AudioFormatWriterOptions{}
                                                                   .withSampleRate (sampleRate)
                                                                   .withNumChannels (channels)
                                                                   .withBitsPerSample (16));
    if (writer == nullptr)
        return false;

    writerThread.startThread();
    recorder = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), writerThread, 1 << 18);
    recordedSamples = 0;

    const juce::SpinLock::ScopedLockType sl (recorderLock);
    activeRecorder = recorder.get();
    recordingChannels = channels;
    return true;
}

void Audio2MidiProcessor::stopRecording()
{
    {
        const juce::SpinLock::ScopedLockType sl (recorderLock);
        activeRecorder = nullptr;
    }

    if (recorder == nullptr)
        return;

    recorder.reset(); // flushes the FIFO and closes the file

    if (recordedSamples > 0)
        setSource (recordingFile);
}

double Audio2MidiProcessor::recordedSeconds() const
{
    const auto sampleRate = getSampleRate();
    return sampleRate > 0 ? (double) recordedSamples.load() / sampleRate : 0.0;
}

void Audio2MidiProcessor::transcribe()
{
    if (! source.existsAsFile())
        return;

    cancelTranscription();
    job = std::make_unique<a2m::TranscriptionJob> (*engine, source, instruments, clientId,
                                                   workDir.getChildFile (juce::Uuid().toString()),
                                                   [this] (const a2m::Status& update)
                                                   {
                                                       const juce::ScopedLock sl (statusLock);
                                                       status = update;
                                                   });
    job->startThread();
}

a2m::Status Audio2MidiProcessor::getStatus() const
{
    const juce::ScopedLock sl (statusLock);
    return status;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Audio2MidiProcessor(); }
