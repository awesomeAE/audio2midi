#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

namespace a2m
{
// Pinned: the HTTP contract and instrumentNames() below match this MuScriptor release.
inline constexpr auto engineVersion = "0.4.0";
inline constexpr int enginePort = 8722;

/** The instrument groups MuScriptor can be told to transcribe. */
const juce::StringArray& instrumentNames();

struct Part
{
    juce::String name;
    int notes = 0;
    juce::File file;
};

/** A note as the /transcribe stream reports it, before the MIDI file moves it by the onset delay. */
struct Note
{
    juce::String instrument;
    int pitch = 0;
    double start = 0.0, end = -1.0; // end stays negative until the stream closes the note
};

struct Status
{
    enum class Stage { idle, startingEngine, transcribing, done, failed };

    Stage stage = Stage::idle;
    juce::String message;
    double progress = 0.0; // 0..1 while transcribing, -1 while the engine starts
    int notes = 0;
    std::vector<Part> parts;        // when done: all parts first, then one per instrument
    std::vector<Part> alignedParts; // the same, snapped to the beat grid; empty when that failed
    juce::String alignError;        // why alignedParts is empty
    juce::MemoryBlock midi, alignedMidi; // what the parts were split from
};

/** One MIDI file per part, as written by splitParts(). */
struct PartData
{
    juce::String name;
    juce::MemoryBlock midi;
    int notes = 0;
};

/** Splits the engine's type-1 MIDI file (track 0 holds tempo and meter, then one track per
    instrument) into an "All parts" file followed by one file per instrument track. Every file
    keeps the conductor track so it lands on the DAW's grid. Empty when no notes were found. */
std::vector<PartData> splitParts (const juce::MemoryBlock& midi);

/** Writes splitParts (midi) into `dir` as "<baseName> - <part>.mid", appending one Part per file. */
juce::Result writeParts (const juce::MemoryBlock& midi, const juce::File& dir, const juce::String& baseName, std::vector<Part>& parts);

/** A finished transcription: `midi` (and `alignedMidi`, when not empty) written as part files under `dir`. */
Status finishedStatus (const juce::MemoryBlock& midi, const juce::MemoryBlock& alignedMidi, const juce::String& alignError,
                       const juce::File& dir, const juce::String& baseName);

/** The JSON body for POST /export that rewrites `notes` with every onset and release snapped to `beatGrid`. */
juce::String alignRequest (const std::vector<Note>& notes, const juce::var& beatGrid);

/** What the /transcribe event stream has delivered so far. */
struct StreamState
{
    std::vector<Note> notes; // indexed by the stream's note index
    int completedChunks = 0;
    int totalChunks = 0;
    std::optional<juce::MemoryBlock> midi; // set by the final "transcription_complete" event
    juce::var beatGrid;                     // set with it; void when no beat was found
};

/** Applies one line of the server-sent event stream. Lines other than "data: {...}" are ignored. */
void handleStreamLine (const juce::String& line, StreamState& state);

/** The local MuScriptor server, shared by every plugin instance in the host process.
    Use it through juce::SharedResourcePointer; the server is stopped with the last instance,
    and only if this process started it. */
class Engine
{
public:
    Engine() = default;
    ~Engine();

    /** Returns once the server answers, launching it through uv first if needed.
        Blocks, so call it from `caller`'s thread; gives up when that thread is asked to exit. */
    juce::Result ensureRunning (juce::Thread& caller, const std::function<void (const juce::String&)>& report);

    static juce::URL url (const juce::String& path);

private:
    static bool isServing();
    juce::Result launch (juce::Thread& caller, const std::function<void (const juce::String&)>& report);
    juce::String recentOutput() const;
    void stop();

    juce::CriticalSection launchLock;
    std::unique_ptr<juce::ChildProcess> server;
    std::thread outputReader; // drains the server's output so a full pipe never blocks it

    mutable juce::CriticalSection outputLock;
    juce::String output;

    JUCE_DECLARE_NON_COPYABLE (Engine)
};

/** Transcribes one audio file on a background thread, reporting through `onUpdate`
    (called on that thread). Destroying the job cancels it. */
class TranscriptionJob final : public juce::Thread
{
public:
    TranscriptionJob (Engine&, juce::File audio, juce::StringArray instruments, juce::String clientId,
                      juce::File outputDir, std::function<void (const Status&)> onUpdate);
    ~TranscriptionJob() override;

    void run() override;

private:
    juce::Result transcribe (StreamState&);
    juce::Result align (const StreamState&, juce::MemoryBlock& aligned);
    void publish (Status);
    void publish (Status::Stage, const juce::String& message, double progress, int notes = 0);

    Engine& engine;
    const juce::File audio;
    const juce::StringArray instruments;
    const juce::String clientId;
    const juce::File outputDir;
    const std::function<void (const Status&)> onUpdate;

    juce::CriticalSection streamLock;
    juce::WebInputStream* activeStream = nullptr; // guarded by streamLock, so the destructor can cancel it

    JUCE_DECLARE_NON_COPYABLE (TranscriptionJob)
};
} // namespace a2m
