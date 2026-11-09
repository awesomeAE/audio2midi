// Unit tests:          Audio2MidiTests
// Real engine run:     Audio2MidiTests --e2e <audio file> [instrument ...]
#include "Transcription.h"

#include <iostream>

namespace
{
int countNotes (const juce::MidiMessageSequence& track)
{
    int notes = 0;

    for (const auto* event : track)
        notes += event->message.isNoteOn() ? 1 : 0;

    return notes;
}

juce::MidiFile readMidi (const juce::MemoryBlock& data)
{
    juce::MemoryInputStream in (data, false);
    juce::MidiFile file;
    file.readFrom (in);
    return file;
}

// Shaped like MuScriptor's output: a conductor track, then one named track per instrument.
juce::MemoryBlock engineStyleMidi()
{
    juce::MidiMessageSequence conductor, bass, drums, silent;
    conductor.addEvent (juce::MidiMessage::tempoMetaEvent (500000));
    conductor.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4));

    bass.addEvent (juce::MidiMessage::textMetaEvent (3, "electric bass"));
    drums.addEvent (juce::MidiMessage::textMetaEvent (3, "drums"));
    silent.addEvent (juce::MidiMessage::textMetaEvent (3, "acoustic piano"));

    for (int i = 0; i < 2; ++i)
    {
        bass.addEvent (juce::MidiMessage::noteOn (2, 40 + i, (juce::uint8) 100), i * 480.0);
        bass.addEvent (juce::MidiMessage::noteOff (2, 40 + i), i * 480.0 + 240.0);
    }

    for (int i = 0; i < 3; ++i)
    {
        drums.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 100), i * 240.0);
        drums.addEvent (juce::MidiMessage::noteOff (10, 36), i * 240.0 + 30.0);
    }

    juce::MidiFile file;
    file.setTicksPerQuarterNote (480);

    for (const auto* track : { &conductor, &bass, &drums, &silent })
        file.addTrack (*track);

    juce::MemoryOutputStream out;
    file.writeTo (out, 1);
    return out.getMemoryBlock();
}

struct SplitPartsTest final : juce::UnitTest
{
    SplitPartsTest() : juce::UnitTest ("splitParts", "Audio2MIDI") {}

    void runTest() override
    {
        beginTest ("one file per instrument, all parts first, empty tracks dropped");
        const auto source = engineStyleMidi();
        const auto parts = a2m::splitParts (source);

        expectEquals ((int) parts.size(), 3);
        expectEquals (parts[0].name, juce::String ("All parts"));
        expectEquals (parts[0].notes, 5);
        expect (parts[0].midi == source);
        expectEquals (parts[1].name, juce::String ("Electric bass"));
        expectEquals (parts[1].notes, 2);
        expectEquals (parts[2].name, juce::String ("Drums"));
        expectEquals (parts[2].notes, 3);

        beginTest ("each part keeps the conductor track and its timing");
        const auto bass = readMidi (parts[1].midi);
        expectEquals (bass.getNumTracks(), 2);
        expectEquals ((int) bass.getTimeFormat(), 480);
        juce::MidiMessageSequence tempos;
        bass.findAllTempoEvents (tempos);
        expectEquals (tempos.getNumEvents(), 1);
        expectEquals (countNotes (*bass.getTrack (1)), 2);
        juce::Array<double> onsets;

        for (const auto* event : *bass.getTrack (1))
            if (event->message.isNoteOn())
                onsets.add (event->message.getTimeStamp());

        expect (onsets == juce::Array<double> { 0.0, 480.0 });

        beginTest ("unreadable or note-free input gives no parts");
        expect (a2m::splitParts (juce::MemoryBlock ("nope", 4)).empty());

        juce::MidiFile tempoOnly;
        tempoOnly.setTicksPerQuarterNote (480);
        juce::MidiMessageSequence conductor, empty;
        conductor.addEvent (juce::MidiMessage::tempoMetaEvent (500000));
        tempoOnly.addTrack (conductor);
        tempoOnly.addTrack (empty);
        juce::MemoryOutputStream out;
        tempoOnly.writeTo (out, 1);
        expect (a2m::splitParts (out.getMemoryBlock()).empty());
    }
};

struct StreamTest final : juce::UnitTest
{
    StreamTest() : juce::UnitTest ("handleStreamLine", "Audio2MIDI") {}

    void runTest() override
    {
        beginTest ("progress, notes and the final MIDI are picked up");
        a2m::StreamState state;
        a2m::handleStreamLine (R"(data: {"type": "progress", "completed": 1, "total": 3})", state);
        a2m::handleStreamLine (R"(data: {"type": "start", "pitch": 54, "start_time": 0.15, "index": 0, "instrument": "drums"})", state);
        a2m::handleStreamLine (R"(data: {"type": "end", "end_time": 0.16, "start_event_index": 0})", state);
        a2m::handleStreamLine (R"(data: {"type": "start", "pitch": 30, "start_time": 0.2, "index": 1, "instrument": "electric_bass"})", state);
        expectEquals (state.completedChunks, 1);
        expectEquals (state.totalChunks, 3);
        expectEquals (state.notes, 2);
        expect (! state.midi.has_value());

        const auto midi = engineStyleMidi();
        a2m::handleStreamLine ("data: {\"type\": \"transcription_complete\", \"data\": \"" + juce::Base64::toBase64 (midi.getData(), midi.getSize())
                               + "\", \"beat_grid\": null}", state);
        expect (state.midi.has_value() && *state.midi == midi);

        beginTest ("blank lines, comments and junk are ignored");
        a2m::StreamState untouched;

        for (const auto* line : { "", ": keep-alive", "event: ping", "data: not json", "data: {\"type\": \"unknown\"}" })
            a2m::handleStreamLine (line, untouched);

        expectEquals (untouched.notes, 0);
        expectEquals (untouched.totalChunks, 0);
        expect (! untouched.midi.has_value());
    }
};

SplitPartsTest splitPartsTest;
StreamTest streamTest;

int runEndToEnd (const juce::File& audio, const juce::StringArray& instruments)
{
    juce::SharedResourcePointer<a2m::Engine> engine;
    const auto outputDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Audio2MidiE2E-" + juce::Uuid().toString());

    juce::CriticalSection lock;
    juce::WaitableEvent finished;
    a2m::Status last;
    const auto started = juce::Time::getMillisecondCounterHiRes();

    {
        a2m::TranscriptionJob job (*engine, audio, instruments, "e2e", outputDir, [&] (const a2m::Status& status)
        {
            const juce::ScopedLock sl (lock);

            if (status.stage != last.stage || status.message != last.message)
                std::cout << juce::String ((juce::Time::getMillisecondCounterHiRes() - started) / 1000.0, 1) << "s  "
                          << status.message << std::endl;

            last = status;

            if (status.stage == a2m::Status::Stage::done || status.stage == a2m::Status::Stage::failed)
                finished.signal();
        });

        job.startThread();
        finished.wait (30 * 60 * 1000);
    }

    if (last.stage != a2m::Status::Stage::done || last.parts.empty())
    {
        std::cerr << "FAILED: " << last.message << std::endl;
        return 1;
    }

    auto ok = true;

    for (const auto& part : last.parts)
    {
        const auto file = readMidi ([&] { juce::MemoryBlock data; part.file.loadFileAsData (data); return data; }());
        int notes = 0;

        for (int i = 1; i < file.getNumTracks(); ++i)
            notes += countNotes (*file.getTrack (i));

        ok = ok && notes == part.notes && notes > 0;
        std::cout << "  " << part.name << ": " << part.notes << " notes -> " << part.file.getFullPathName()
                  << (notes == part.notes ? "" : "  (MISMATCH: file has " + juce::String (notes) + ")") << std::endl;
    }

    if (! instruments.isEmpty())
        for (size_t i = 1; i < last.parts.size(); ++i)
        {
            const auto name = last.parts[i].name.toLowerCase().replaceCharacter (' ', '_');
            ok = ok && instruments.contains (name);
        }

    std::cout << (ok ? "PASS" : "FAIL") << std::endl;
    return ok ? 0 : 1;
}
} // namespace

int main (int argc, char* argv[])
{
    if (argc >= 3 && juce::String (argv[1]) == "--e2e")
    {
        juce::StringArray instruments;

        for (int i = 3; i < argc; ++i)
            instruments.add (argv[i]);

        return runEndToEnd (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]), instruments);
    }

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTests ({ &splitPartsTest, &streamTest });

    int failures = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    return failures == 0 ? 0 : 1;
}
