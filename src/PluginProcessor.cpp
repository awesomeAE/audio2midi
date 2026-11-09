#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <BinaryData.h>

namespace
{
// The upload limit for one transcription (see TranscriptionJob).
constexpr juce::int64 maxRecordingBytes = 250 * 1024 * 1024;

// Shows the UI in the system's display language when i18n/ has a table for it, else in English.
void useSystemLanguage()
{
    [[maybe_unused]] static const auto loaded = []
    {
        const auto language = juce::SystemStats::getDisplayLanguage().upToFirstOccurrenceOf ("-", false, false).toLowerCase();
        int size = 0;

        if (const auto* table = BinaryData::getNamedResource ((language + "_txt").toRawUTF8(), size))
            juce::LocalisedStrings::setCurrentMappings (new juce::LocalisedStrings (juce::String::fromUTF8 (table, size), false));

        return true;
    }();
}
} // namespace

Audio2MidiProcessor::Audio2MidiProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    useSystemLanguage();
    formats.registerBasicFormats();
}

Audio2MidiProcessor::~Audio2MidiProcessor()
{
    job.reset();
    stopRecording();
    transport.setSource (nullptr);
    ioThread.stopThread (2000);
    workDir.deleteRecursively();
}

void Audio2MidiProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    transport.prepareToPlay (samplesPerBlock, sampleRate);
    sourceBuffer.setSize (2, samplesPerBlock);
    synthBuffer.setSize (2, samplesPerBlock);
    synthMidi.ensureSize (4096);

    const juce::SpinLock::ScopedLockType sl (previewLock);

    if (synth != nullptr)
        synth->prepareToPlay (sampleRate, samplesPerBlock);
}

void Audio2MidiProcessor::releaseResources()
{
    transport.releaseResources();
    const juce::SpinLock::ScopedLockType sl (previewLock);

    if (synth != nullptr)
        synth->releaseResources();
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
    lastBlockTime = juce::Time::getMillisecondCounter();

    // Audio passes through untouched unless the preview is playing.
    record (buffer);
    renderPreview (buffer);
}

void Audio2MidiProcessor::record (const juce::AudioBuffer<float>& buffer)
{
    const auto* playHead = getPlayHead();
    const auto position = playHead != nullptr ? playHead->getPosition() : juce::Optional<juce::AudioPlayHead::PositionInfo>();
    // The standalone app's playhead never reports playing: it records whatever comes in.
    hostPlaying = wrapperType == wrapperType_Standalone || ! position.hasValue() || position->getIsPlaying();

    const juce::SpinLock::ScopedTryLockType lock (recorderLock);

    if (! lock.isLocked() || activeRecorder == nullptr || ! hostPlaying || buffer.getNumChannels() < recordingChannels)
        return;

    const auto samples = buffer.getNumSamples();

    if (recordedSamples + samples > recordingLimit)
        recordingFull = true;
    else if (activeRecorder->write (buffer.getArrayOfReadPointers(), samples))
        recordedSamples += samples;
    else
        ++droppedBlocks;
}

void Audio2MidiProcessor::renderPreview (juce::AudioBuffer<float>& buffer)
{
    const auto sampleRate = getSampleRate();

    if (! previewPlaying || sampleRate <= 0)
        return;

    const juce::SpinLock::ScopedTryLockType lock (previewLock);

    if (! lock.isLocked())
        return;

    const auto samples = buffer.getNumSamples();
    const auto seek = seekTarget.exchange (-1.0);
    const auto start = seek >= 0.0 ? seek : previewSeconds.load();

    if (seek >= 0.0)
        nextEvent = (int) (std::lower_bound (playingSequence.begin(), playingSequence.end(), start,
                                             [] (const auto* event, double time) { return event->message.getTimeStamp() < time; })
                           - playingSequence.begin());
    const auto end = start + samples / sampleRate;
    const auto playMidi = synth != nullptr && playingSequence.getNumEvents() > 0;

    synthMidi.clear();

    if (seek >= 0.0)
    {
        for (int channel = 1; channel <= 16; ++channel)
        {
            synthMidi.addEvent (juce::MidiMessage::allSoundOff (channel), 0);
            synthMidi.addEvent (juce::MidiMessage::allNotesOff (channel), 0);
        }

        // Programs and controllers sent before the start point still apply.
        for (int i = 0; i < nextEvent; ++i)
            if (const auto& message = playingSequence.getEventPointer (i)->message; ! message.isNoteOnOrOff())
                synthMidi.addEvent (message, 0);
    }

    for (; nextEvent < playingSequence.getNumEvents(); ++nextEvent)
    {
        const auto& message = playingSequence.getEventPointer (nextEvent)->message;

        if (message.getTimeStamp() >= end)
            break;

        synthMidi.addEvent (message, juce::jlimit (0, samples - 1, (int) ((message.getTimeStamp() - start) * sampleRate)));
    }

    sourceBuffer.setSize (2, samples, false, false, true);
    sourceBuffer.clear();

    if (hasSourceAudio)
        transport.getNextAudioBlock (juce::AudioSourceChannelInfo (&sourceBuffer, 0, samples));

    if (playMidi)
    {
        synthBuffer.setSize (juce::jmax (2, synth->getTotalNumOutputChannels()), samples, false, false, true);
        synthBuffer.clear();
        synth->processBlock (synthBuffer, synthMidi);
    }

    const auto mix = previewMix.load();
    const auto sourceGain = playMidi ? 1.0f - mix : 1.0f;
    const auto midiGain = hasSourceAudio ? mix : 1.0f;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto from = juce::jmin (channel, 1);
        buffer.copyFrom (channel, 0, sourceBuffer.getReadPointer (from), samples, sourceGain);

        if (playMidi)
            buffer.addFrom (channel, 0, synthBuffer, from, 0, samples, midiGain);
    }

    previewSeconds = end;

    if (end >= previewLength)
        previewPlaying = false;
}

juce::AudioProcessorEditor* Audio2MidiProcessor::createEditor() { return new Audio2MidiEditor (*this); }

void Audio2MidiProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement state ("Audio2MIDI");
    state.setAttribute ("instruments", instruments.joinIntoString (","));
    state.setAttribute ("model", a2m::modelNames()[model]);
    state.setAttribute ("align", alignToBeats.load());
    state.setAttribute ("mix", previewMix.load());

    {
        // Hosts may save from any thread.
        const juce::ScopedLock sl (statusLock);
        state.setAttribute ("source", source.getFullPathName());

        if (status.stage == a2m::Status::Stage::done && ! status.parts.empty())
        {
            state.setAttribute ("name", resultName);
            state.setAttribute ("midi", status.midi.toBase64Encoding());
            state.setAttribute ("alignedMidi", status.alignedMidi.toBase64Encoding());
            state.setAttribute ("alignError", status.alignError);
        }
    }

    copyXmlToBinary (state, destData);
}

void Audio2MidiProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto state = getXmlFromBinary (data, sizeInBytes);

    if (state == nullptr || ! state->hasTagName ("Audio2MIDI"))
        return;

    cancelTranscription();
    juce::StringArray restored;

    // Ignore names a different engine version may not know.
    for (const auto& name : juce::StringArray::fromTokens (state->getStringAttribute ("instruments"), ",", {}))
        if (a2m::instrumentNames().contains (name))
            restored.add (name);

    instruments = restored;
    const auto restoredModel = a2m::modelNames().indexOf (state->getStringAttribute ("model", a2m::defaultModel));
    model = restoredModel >= 0 ? restoredModel : a2m::modelNames().indexOf (a2m::defaultModel);
    alignToBeats = state->getBoolAttribute ("align");
    previewMix = juce::jlimit (0.0f, 1.0f, (float) state->getDoubleAttribute ("mix", 0.5));

    if (const auto path = state->getStringAttribute ("source"); juce::File::isAbsolutePath (path) && juce::File (path).existsAsFile())
        setSource (juce::File (path));

    // The result travels with the project; its part files are written again, since the
    // previous session's temporary folder is gone.
    juce::MemoryBlock midi, alignedMidi;

    if (! midi.fromBase64Encoding (state->getStringAttribute ("midi")) || midi.isEmpty())
        return;

    if (! alignedMidi.fromBase64Encoding (state->getStringAttribute ("alignedMidi")))
        alignedMidi.reset();

    const auto name = state->getStringAttribute ("name", "Audio2MIDI");
    auto result = a2m::finishedStatus (midi, alignedMidi, state->getStringAttribute ("alignError"),
                                       workDir.getChildFile (juce::Uuid().toString()), name);

    const juce::ScopedLock sl (statusLock);
    status = std::move (result);
    resultName = name;
}

void Audio2MidiProcessor::setSource (const juce::File& file)
{
    cancelTranscription();
    stopPreview();

    {
        const juce::ScopedLock sl (statusLock);
        source = file;
    }

    transport.setSource (nullptr);
    sourceReader.reset();
    sourceSeconds = 0.0;

    if (auto* reader = formats.createReaderFor (file))
    {
        const auto rate = reader->sampleRate;
        sourceSeconds = rate > 0 ? (double) reader->lengthInSamples / rate : 0.0;
        sourceReader = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
        ioThread.startThread();
        transport.setSource (sourceReader.get(), 1 << 15, &ioThread, rate, 2);
    }

    {
        const juce::SpinLock::ScopedLockType sl (previewLock);
        hasSourceAudio = sourceReader != nullptr;
    }

    updatePreviewLength();
    seekPreview (0.0);
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
    stopPreview();
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

    ioThread.startThread();
    recorder = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), ioThread, 1 << 18);
    recordedSamples = 0;
    droppedBlocks = 0;
    recordingFull = false;

    const juce::SpinLock::ScopedLockType sl (recorderLock);
    activeRecorder = recorder.get();
    recordingChannels = channels;
    recordingLimit = maxRecordingBytes / (channels * 2);
    return true;
}

juce::String Audio2MidiProcessor::stopRecording()
{
    {
        const juce::SpinLock::ScopedLockType sl (recorderLock);
        activeRecorder = nullptr;
    }

    if (recorder == nullptr)
        return {};

    recorder.reset(); // flushes the FIFO and closes the file

    if (recordedSamples == 0)
    {
        recordingFile.deleteFile();
        return TRANS ("Nothing was recorded. Start playback in your DAW so the track's audio reaches the plug-in.");
    }

    setSource (recordingFile);

    if (droppedBlocks > 0)
        return TRANS ("The recording has gaps because the disk could not keep up. Record again for a clean take.");

    if (recordingFull)
        return TRANS ("Recording stopped at the 250 MB limit for one transcription.");

    return {};
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

    {
        const juce::ScopedLock sl (statusLock);
        resultName = source.getFileNameWithoutExtension();
    }

    job = std::make_unique<a2m::TranscriptionJob> (*engine, source, instruments, a2m::modelNames()[model], clientId,
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
    auto copy = status;

    if (alignToBeats && ! copy.alignedParts.empty())
        copy.parts = copy.alignedParts;

    return copy;
}

void Audio2MidiProcessor::setPreviewMidi (const juce::File& file)
{
    juce::MidiMessageSequence loaded;
    juce::FileInputStream in (file);
    juce::MidiFile midi;

    if (in.openedOk() && midi.readFrom (in))
    {
        midi.convertTimestampTicksToSeconds();

        for (int track = 0; track < midi.getNumTracks(); ++track)
            loaded.addSequence (*midi.getTrack (track), 0.0);

        // Meta events stay out: sent to a synth, their 0xff status byte would mean "reset".
        for (int i = loaded.getNumEvents(); --i >= 0;)
            if (loaded.getEventPointer (i)->message.isMetaEvent())
                loaded.deleteEvent (i, false);

        loaded.updateMatchedPairs();
    }

    if (loaded.getNumEvents() > 0 && synth == nullptr)
        createSynth();

    sequence = loaded;

    {
        const juce::SpinLock::ScopedLockType sl (previewLock);
        playingSequence.swapWith (loaded);
    }

    updatePreviewLength();
    seekPreview (previewSeconds);
}

void Audio2MidiProcessor::createSynth()
{
   #if JUCE_MAC
    // Apple's General MIDI synth ships with macOS, so the preview needs no sound download.
    juce::AudioUnitPluginFormatHeadless format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, "AudioUnit:Synths/aumu,dls ,appl");

    if (found.isEmpty())
        return;

    const auto sampleRate = getSampleRate() > 0 ? getSampleRate() : 44100.0;
    const auto blockSize = getBlockSize() > 0 ? getBlockSize() : 512;
    juce::String error;
    auto instance = format.createInstanceFromDescription (*found.getFirst(), sampleRate, blockSize, error);

    if (instance == nullptr)
        return;

    instance->prepareToPlay (sampleRate, blockSize);
    const juce::SpinLock::ScopedLockType sl (previewLock);
    synth = std::move (instance);
   #endif
}

bool Audio2MidiProcessor::canPreviewMidi() const { return synth != nullptr; }

void Audio2MidiProcessor::startPreview()
{
    if (getSampleRate() <= 0)
        return;

    seekPreview (previewSeconds >= previewLength ? 0.0 : previewSeconds.load());
    previewPlaying = true;
}

void Audio2MidiProcessor::stopPreview()
{
    // The transport keeps "playing": its stop() waits for an audio callback that, with the
    // preview off, never comes. Output is gated by previewPlaying alone.
    previewPlaying = false;
}

void Audio2MidiProcessor::seekPreview (double seconds)
{
    const auto target = juce::jlimit (0.0, juce::jmax (0.0, previewLength.load()), seconds);
    transport.setPosition (target);
    transport.start(); // restarts it after it ran off the end of the file
    previewSeconds = target;
    seekTarget = target;
}

bool Audio2MidiProcessor::isPreviewWaitingForHost() const
{
    return previewPlaying && juce::Time::getMillisecondCounter() - lastBlockTime > 300;
}

void Audio2MidiProcessor::updatePreviewLength()
{
    previewLength = juce::jmax (sourceSeconds, sequence.getEndTime());
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Audio2MidiProcessor(); }
