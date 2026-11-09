# Audio2MIDI

**A self-hosted, open-source alternative to Mirelo Audio-to-MIDI.** No account, no credits, your audio never leaves your machine.

Audio-to-MIDI plugin (VST3, AU, Standalone). Put it on an audio track, drop in a file or record the track, and drag the transcribed MIDI parts back into your DAW. Full mixes work: you get one MIDI file per instrument plus one with every part.

Transcription runs locally with [MuScriptor](https://github.com/muscriptor/muscriptor) by Kyutai and Mirelo. This project is not affiliated with either.

## How it works

```
DAW ── Audio2MIDI plugin (JUCE, C++) ──HTTP──▶ MuScriptor engine (Python, PyTorch) on 127.0.0.1:8722
```

The model is a PyTorch transformer, so the plugin does not embed it. It starts the engine itself on the first transcription (`uv` installs a pinned `muscriptor==0.4.0` in an isolated Python 3.12 environment) and stops it when the last plugin instance closes. An engine that is already running on port 8722 is reused.

## Requirements

- macOS 11+ (Apple Silicon or Intel) or Windows 10+ x64
- [uv](https://docs.astral.sh/uv/getting-started/installation/)
- A free Hugging Face account. The model weights are gated under CC BY-NC 4.0:
  1. Accept the licence at [MuScriptor/muscriptor-medium](https://huggingface.co/MuScriptor/muscriptor-medium).
  2. Run `uvx hf auth login` once.
- About 2 GB of disk space for PyTorch and the weights, which download on the first run

## Install (macOS)

1. Download `Audio2MIDI-macOS.zip` from the [latest release](../../releases/latest) (Universal, macOS 11+).
2. Copy `Audio2MIDI.vst3` to `~/Library/Audio/Plug-Ins/VST3` and `Audio2MIDI.component` to `~/Library/Audio/Plug-Ins/Components`.
3. The binaries are not notarized. Clear the download quarantine once:
   ```bash
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Audio2MIDI.vst3 ~/Library/Audio/Plug-Ins/Components/Audio2MIDI.component
   ```
4. Restart or rescan your DAW.

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

JUCE 9.0.5 is fetched by CMake. The VST3 and AU are copied to `~/Library/Audio/Plug-Ins` after building. Rescan plugins in your DAW.

## Use

1. Insert **Audio2MIDI** on an audio track.
2. Drop an audio file (wav, aiff, flac, mp3, ogg) on the plugin, use **Choose file**, or press **Record** and play the track.
3. Optional: limit **Instruments** to the parts you expect. The default detects them automatically.
4. Press **Transcribe**. The first run takes a few minutes while the engine and weights download.
5. Drag **All parts** or a single instrument into your DAW. **Show files** opens the MIDI files.

The MIDI files carry the detected tempo and meter, so they land on the grid.

## Test

```bash
ctest --test-dir build                                    # unit tests
build/Audio2MidiTests_artefacts/Release/Audio2MidiTests --e2e song.mp3 [drums electric_bass]
pluginval --strictness-level 10 --validate ~/Library/Audio/Plug-Ins/VST3/Audio2MIDI.vst3
```

`--e2e` runs a real transcription through the same engine code the plugin uses. It starts and stops the engine when none is running.

## Limits

- No preview playback or piano roll in the plugin. Listen to the parts in your DAW.
- No AAX, so no Pro Tools.
- Windows builds from source but is untested. The engine runs PyTorch on the CPU there.
- If the DAW crashes, the engine keeps running in the background. The next session reuses it. Quit it with `pkill -f "muscriptor serve"`.
- Release binaries are not signed or notarized.

## License

The plugin source is [AGPL-3.0](LICENSE), as required by JUCE's open-source licence. The MuScriptor model weights are CC BY-NC 4.0 and are downloaded at runtime, never bundled.
