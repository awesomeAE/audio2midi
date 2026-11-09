<p align="center"><img src="assets/logo.png" alt="awesomeAE Audio-to-MIDI" width="280"></p>

# Audio2MIDI

**A self-hosted, open-source alternative to Mirelo Audio-to-MIDI.** No account, no credits, your audio never leaves your machine.

Audio-to-MIDI plugin (VST3, AU, Standalone). Put it on an audio track, drop in a file or record the track, check the result in the piano roll and by ear, and drag the transcribed MIDI parts back into your DAW. Full mixes work: you get one MIDI file per instrument plus one with every part.

Transcription runs locally with [MuScriptor](https://github.com/muscriptor/muscriptor) by Kyutai and Mirelo. This project is not affiliated with either.

<p align="center"><img src="assets/preview.png" alt="Audio2MIDI plugin with a transcribed song in the piano roll" width="520"></p>

## How it works

```
DAW ── Audio2MIDI plugin (JUCE, C++) ──HTTP──▶ MuScriptor engine (Python, PyTorch) on 127.0.0.1:8722
```

The model is a PyTorch transformer, so the plugin does not embed it. It starts the engine itself on the first transcription (`uv` installs a pinned `muscriptor==0.4.0` in an isolated Python 3.12 environment) and stops it when the last plugin instance closes. The engine also exits on its own if the DAW crashes. An engine that is already running on port 8722 is reused.

## Requirements

- macOS 11+ (Apple Silicon or Intel) or Windows 10+ x64
- [uv](https://docs.astral.sh/uv/getting-started/installation/)
- A free Hugging Face account. The model weights are gated under CC BY-NC 4.0:
  1. Accept the licence at [MuScriptor/muscriptor-medium](https://huggingface.co/MuScriptor/muscriptor-medium).
  2. Run `uvx hf auth login` once.
- About 2 GB of disk space for PyTorch and the weights, which download on the first run

## Install (macOS)

1. Download `Audio2MIDI-macOS.pkg` from the [latest release](../../releases/latest) (Universal, macOS 11+).
2. Open it. The installer is not notarized, so macOS blocks it the first time: open **System Settings → Privacy & Security** and click **Open Anyway**.
3. Install. It puts the VST3 and AU into `~/Library/Audio/Plug-Ins` and needs no administrator password.
4. Restart or rescan your DAW. In Ableton Live, check that **Settings → Plug-Ins → Use VST3 Plug-In System Folders** is on.

`Audio2MIDI-macOS.zip` holds the same plugins for manual copying. Files copied from it keep the download quarantine; clear it with `xattr -dr com.apple.quarantine <plugin>`.

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
5. Select a part and press **Play** to hear it. The preview plays through the track: the slider mixes the original audio with the MIDI, played by macOS's built-in General MIDI sounds. Click the piano roll to jump. Some DAWs, such as Logic Pro, only run a track's plugins while the transport plays; the plugin tells you when that is the case.
6. Turn on **Align to beats** to snap every note to the detected beat grid. Leave it off to keep the performance as played.
7. Drag **All parts** or a single instrument into your DAW. **Show files** opens the MIDI files.

The MIDI files carry the detected tempo and meter, so they land on the grid. The result is saved with your project and comes back when you reopen it.

## Test

```bash
ctest --test-dir build                                    # unit tests
build/Audio2MidiTests_artefacts/Release/Audio2MidiTests --e2e song.mp3 [drums electric_bass]
pluginval --strictness-level 10 --validate ~/Library/Audio/Plug-Ins/VST3/Audio2MIDI.vst3
packaging/macos/build-pkg.sh build/Audio2Midi_artefacts/Release 0.2.0 Audio2MIDI-macOS.pkg
```

`--e2e` runs a real transcription through the same engine code the plugin uses. It starts and stops the engine when none is running.

## Limits

- No AAX, so no Pro Tools.
- Windows builds from source but is untested. The engine runs PyTorch on the CPU there, the MIDI preview is silent, and the engine outlives a crashed DAW.
- Release binaries are not signed or notarized.
- Recordings stay in a temporary folder. The transcription is saved with the project, a recorded take is not.

## License

The plugin source is [AGPL-3.0](LICENSE), as required by JUCE's open-source licence. The MuScriptor model weights are CC BY-NC 4.0 and are downloaded at runtime, never bundled.
