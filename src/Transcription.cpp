#include "Transcription.h"

namespace a2m
{
namespace
{
juce::Result fail (const juce::String& message) { return juce::Result::fail (message); }

int countNotes (const juce::MidiMessageSequence& track)
{
    int notes = 0;

    for (const auto* event : track)
        notes += event->message.isNoteOn() ? 1 : 0;

    return notes;
}

juce::String trackName (const juce::MidiMessageSequence& track, int index)
{
    for (const auto* event : track)
        if (event->message.isTrackNameEvent())
        {
            const auto name = event->message.getTextFromTextMetaEvent().trim();
            return name.substring (0, 1).toUpperCase() + name.substring (1);
        }

    return "Part " + juce::String (index);
}

juce::MemoryBlock writeTracks (int ticksPerQuarterNote, std::initializer_list<const juce::MidiMessageSequence*> tracks)
{
    juce::MidiFile file;
    file.setTicksPerQuarterNote (ticksPerQuarterNote);

    for (const auto* track : tracks)
        file.addTrack (*track);

    juce::MemoryOutputStream out;
    file.writeTo (out, 1);
    return out.getMemoryBlock();
}

juce::File findUvx()
{
   #if JUCE_WINDOWS
    const juce::String executable = "uvx.exe", separator = ";";
   #else
    const juce::String executable = "uvx", separator = ":";
   #endif

    // Hosts started from the Dock or Explorer don't inherit the shell's PATH, so also try
    // the places uv's installers use.
    auto dirs = juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("PATH", {}), separator, {});
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    dirs.add (home.getChildFile (".local/bin").getFullPathName());
    dirs.add (home.getChildFile (".cargo/bin").getFullPathName());
    dirs.add ("/opt/homebrew/bin");
    dirs.add ("/usr/local/bin");

    for (const auto& dir : dirs)
        if (juce::File::isAbsolutePath (dir))
            if (const auto candidate = juce::File (dir).getChildFile (executable); candidate.existsAsFile())
                return candidate;

    return {};
}
} // namespace

const juce::StringArray& instrumentNames()
{
    static const juce::StringArray names {
        "acoustic_piano", "electric_piano", "chromatic_percussion", "organ", "acoustic_guitar",
        "clean_electric_guitar", "distorted_electric_guitar", "acoustic_bass", "electric_bass", "violin",
        "viola", "cello", "contrabass", "orchestral_harp", "timpani", "string_ensemble", "synth_strings",
        "voice", "orchestra_hit", "trumpet", "trombone", "tuba", "french_horn", "brass_section",
        "soprano_and_alto_sax", "tenor_sax", "baritone_sax", "oboe", "english_horn", "bassoon",
        "clarinet", "flutes", "synth_lead", "synth_pad", "drums"
    };
    return names;
}

std::vector<PartData> splitParts (const juce::MemoryBlock& midi)
{
    juce::MemoryInputStream in (midi, false);
    juce::MidiFile file;

    if (! file.readFrom (in) || file.getNumTracks() < 2 || file.getTimeFormat() <= 0)
        return {};

    const auto* conductor = file.getTrack (0);
    std::vector<PartData> parts;
    int total = 0;

    for (int i = 1; i < file.getNumTracks(); ++i)
    {
        const auto* track = file.getTrack (i);
        const auto notes = countNotes (*track);

        if (notes == 0)
            continue;

        total += notes;
        parts.push_back ({ trackName (*track, i), writeTracks (file.getTimeFormat(), { conductor, track }), notes });
    }

    if (! parts.empty())
        parts.insert (parts.begin(), { "All parts", midi, total });

    return parts;
}

void handleStreamLine (const juce::String& line, StreamState& state)
{
    if (! line.startsWith ("data:"))
        return;

    const auto event = juce::JSON::parse (line.substring (5).trim());
    const auto type = event["type"].toString();

    if (type == "start")
    {
        ++state.notes;
    }
    else if (type == "progress")
    {
        state.completedChunks = static_cast<int> (event["completed"]);
        state.totalChunks = static_cast<int> (event["total"]);
    }
    else if (type == "transcription_complete")
    {
        juce::MemoryOutputStream decoded;

        if (juce::Base64::convertFromBase64 (decoded, event["data"].toString()))
            state.midi = decoded.getMemoryBlock();
    }
}

//==============================================================================
Engine::~Engine() { stop(); }

juce::URL Engine::url (const juce::String& path)
{
    return juce::URL ("http://127.0.0.1:" + juce::String (enginePort) + path);
}

bool Engine::isServing()
{
    int status = 0;
    const auto stream = url ("/instruments").createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                                    .withConnectionTimeoutMs (2000)
                                                                    .withStatusCode (&status));
    if (stream == nullptr || status != 200)
        return false;

    // Something else could own the port; only a MuScriptor server lists its instruments.
    juce::MemoryBlock body; // capped: whatever answers on the port is not trusted
    stream->readIntoMemoryBlock (body, 1 << 16);
    const auto json = juce::JSON::parse (body.toString());
    const auto* list = json["instruments"].getArray();
    return list != nullptr && list->contains ("acoustic_piano");
}

juce::Result Engine::ensureRunning (juce::Thread& caller, const std::function<void (const juce::String&)>& report)
{
    // Instances that start at the same time queue here instead of each launching a server.
    while (! launchLock.tryEnter())
    {
        if (caller.threadShouldExit())
            return fail ("Cancelled.");

        caller.wait (100);
    }

    const juce::ScopeGuard unlock { [this] { launchLock.exit(); } };

    if (isServing())
        return juce::Result::ok();

    if (server == nullptr || ! server->isRunning())
    {
        stop();

        if (const auto launched = launch (caller, report); launched.failed())
            return launched;
    }

    report ("Loading the model. The first run downloads the weights.");

    while (! isServing())
    {
        if (caller.threadShouldExit())
            return fail ("Cancelled.");

        if (! server->isRunning())
        {
            stop(); // joins the output reader, so the output below is complete
            const auto details = recentOutput();
            const auto error = details.lastIndexOf ("Error:");
            return fail ("The transcription engine stopped.\n\n" + (error >= 0 ? details.substring (error) : details).trim());
        }

        caller.wait (500);
    }

    return juce::Result::ok();
}

juce::Result Engine::launch (juce::Thread& caller, const std::function<void (const juce::String&)>& report)
{
    const auto uvx = findUvx();

    if (uvx == juce::File())
        return fail ("uv was not found. Install it from https://docs.astral.sh/uv/ and try again.");

    // Resolve the engine's Python environment, then run that Python directly: killing a uvx
    // wrapper would leave the server orphaned on the port. 3.12 has PyTorch wheels everywhere,
    // including Intel Macs.
    report ("Installing the transcription engine. The first run downloads about 1 GB.");
    juce::ChildProcess installer;

    if (! installer.start (juce::StringArray { uvx.getFullPathName(), "--quiet", "--python", "3.12",
                                               "--from", juce::String ("muscriptor==") + engineVersion,
                                               "python", "-I", "-X", "utf8", "-c", "import sys; print(sys.executable)" }))
        return fail ("Could not run " + uvx.getFullPathName());

    while (installer.isRunning())
    {
        if (caller.threadShouldExit())
        {
            installer.kill();
            return fail ("Cancelled.");
        }

        installer.waitForProcessToFinish (200);
    }

    const auto installOutput = installer.readAllProcessOutput();
    juce::File python;

    for (const auto& line : juce::StringArray::fromLines (installOutput))
        if (const auto path = line.trim(); juce::File::isAbsolutePath (path) && juce::File (path).existsAsFile())
            python = juce::File (path);

    if (installer.getExitCode() != 0 || python == juce::File())
        return fail ("Installing the transcription engine failed.\n\n" + installOutput.trim());

    report ("Starting the transcription engine.");
    server = std::make_unique<juce::ChildProcess>();

    // -I: never import modules from the host's working directory or PYTHON* variables.
    if (! server->start (juce::StringArray { python.getFullPathName(), "-I", "-X", "utf8", "-m", "muscriptor", "serve",
                                             "--host", "127.0.0.1", "--port", juce::String (enginePort) }))
    {
        server.reset();
        return fail ("Could not start " + python.getFullPathName());
    }

    {
        const juce::ScopedLock sl (outputLock);
        output.clear();
    }

    outputReader = std::thread ([this, process = server.get()]
    {
        char buffer[1024];

        for (int n; (n = process->readProcessOutput (buffer, sizeof (buffer))) > 0;)
        {
            const juce::ScopedLock sl (outputLock);
            output = (output + juce::String::fromUTF8 (buffer, n)).getLastCharacters (8000);
        }
    });

    return juce::Result::ok();
}

juce::String Engine::recentOutput() const
{
    const juce::ScopedLock sl (outputLock);
    return output;
}

void Engine::stop()
{
    if (server != nullptr)
        server->kill();

    if (outputReader.joinable())
        outputReader.join();

    server.reset();
}

//==============================================================================
TranscriptionJob::TranscriptionJob (Engine& e, juce::File audioFile, juce::StringArray instrumentsToUse, juce::String id,
                                    juce::File outputDirectory, std::function<void (const Status&)> callback)
    : juce::Thread ("Audio2MIDI transcription"),
      engine (e),
      audio (std::move (audioFile)),
      instruments (std::move (instrumentsToUse)),
      clientId (std::move (id)),
      outputDir (std::move (outputDirectory)),
      onUpdate (std::move (callback))
{
}

TranscriptionJob::~TranscriptionJob()
{
    signalThreadShouldExit();

    {
        const juce::ScopedLock sl (streamLock);

        if (activeStream != nullptr)
            activeStream->cancel();
    }

    stopThread (10000);
}

void TranscriptionJob::run()
{
    publish (Status::Stage::startingEngine, "Starting the transcription engine.", -1.0);

    const auto ready = engine.ensureRunning (*this, [this] (const juce::String& message)
    {
        publish (Status::Stage::startingEngine, message, -1.0);
    });

    if (threadShouldExit())
        return;

    if (ready.failed())
        return publish (Status::Stage::failed, ready.getErrorMessage(), 0.0);

    publish (Status::Stage::transcribing, "Transcribing", 0.0);
    StreamState state;

    if (const auto result = transcribe (state); result.failed())
    {
        if (! threadShouldExit())
            publish (Status::Stage::failed, result.getErrorMessage(), 0.0, state.notes);

        return;
    }

    std::vector<Part> parts;

    if (const auto written = writeParts (*state.midi, parts); written.failed())
        return publish (Status::Stage::failed, written.getErrorMessage(), 0.0);

    if (parts.empty())
        return publish (Status::Stage::done, "No notes found.", 1.0);

    const auto total = parts.front().notes;
    const auto summary = juce::String (total) + " notes in " + juce::String (parts.size() - 1) + (parts.size() == 2 ? " part" : " parts");
    publish (Status::Stage::done, summary, 1.0, total, std::move (parts));
}

juce::Result TranscriptionJob::transcribe (StreamState& state)
{
    // The upload is buffered in memory inside the host, so keep it far from what could take a DAW down.
    if (audio.getSize() > 250 * 1024 * 1024)
        return fail ("The audio file is larger than 250 MB. Transcribe a shorter section.");

    auto url = Engine::url ("/transcribe").withFileToUpload ("file", audio, "application/octet-stream");

    for (const auto& name : instruments)
        url = url.withParameter ("instruments", name);

    juce::WebInputStream stream (url, true);
    // Long, dense recordings can go minutes between events on a slow machine.
    stream.withConnectionTimeout (10 * 60 * 1000).withExtraHeaders ("X-Client-Id: " + clientId);

    {
        const juce::ScopedLock sl (streamLock);

        if (threadShouldExit())
            return fail ("Cancelled.");

        activeStream = &stream;
    }

    const juce::ScopeGuard forget { [this] { const juce::ScopedLock sl (streamLock); activeStream = nullptr; } };

    if (! stream.connect (nullptr))
        return fail ("Could not reach the transcription engine.");

    if (const auto status = stream.getStatusCode(); status != 200)
    {
        if (status == 503)
            return fail ("The engine is busy with another transcription. Try again when it has finished.");

        const auto body = stream.readEntireStreamAsString();
        const auto detail = juce::JSON::parse (body)["detail"].toString();
        return fail ("The engine rejected the audio (HTTP " + juce::String (status) + "): " + (detail.isNotEmpty() ? detail : body));
    }

    auto reportedNotes = -1, reportedChunks = -1;

    while (! threadShouldExit() && ! stream.isExhausted() && ! state.midi.has_value())
    {
        handleStreamLine (stream.readNextLine(), state);

        if (state.notes != reportedNotes || state.completedChunks != reportedChunks)
        {
            reportedNotes = state.notes;
            reportedChunks = state.completedChunks;
            const auto progress = state.totalChunks > 0 ? (double) state.completedChunks / state.totalChunks : 0.0;
            publish (Status::Stage::transcribing, "Transcribing", progress, state.notes);
        }
    }

    if (threadShouldExit())
        return fail ("Cancelled.");

    if (! state.midi.has_value())
        return fail ("The engine stopped before the transcription finished.");

    return juce::Result::ok();
}

juce::Result TranscriptionJob::writeParts (const juce::MemoryBlock& midi, std::vector<Part>& parts) const
{
    if (! outputDir.createDirectory())
        return fail ("Could not create " + outputDir.getFullPathName());

    // A leading "~" would make JUCE treat the name as a home-relative path; a short base keeps
    // createLegalFileName from truncating part names into each other.
    const auto base = audio.getFileNameWithoutExtension().trimCharactersAtStart ("~.").substring (0, 60);

    for (const auto& part : splitParts (midi))
    {
        const auto file = outputDir.getNonexistentChildFile (juce::File::createLegalFileName (base + " - " + part.name), ".mid", false);

        if (! file.replaceWithData (part.midi.getData(), part.midi.getSize()))
            return fail ("Could not write " + file.getFullPathName());

        parts.push_back ({ part.name, part.notes, file });
    }

    return juce::Result::ok();
}

void TranscriptionJob::publish (Status::Stage stage, const juce::String& message, double progress, int notes, std::vector<Part> parts)
{
    onUpdate ({ stage, message, progress, notes, std::move (parts) });
}
} // namespace a2m
