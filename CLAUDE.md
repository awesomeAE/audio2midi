# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Projekt

Audio2MIDI: JUCE-9-Plugin (VST3, AU, Standalone; C++20, CMake) für Audio-zu-MIDI-Transkription. Das Modell (MuScriptor, PyTorch) ist nicht eingebettet. Das Plugin startet es als lokalen Python-HTTP-Server und spricht per HTTP mit ihm. Veröffentlicht wird ausschließlich unter `awesomeAE/audio2midi` mit der repo-lokalen Git-Identität `awesomeAE`.

## Befehle

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # JUCE 9.0.5 wird per FetchContent geholt
cmake --build build                                        # kopiert VST3/AU nach ~/Library/Audio/Plug-Ins (nur macOS)
ctest --test-dir build                                     # Unit-Tests (Target Audio2MidiTests)
build/Audio2MidiTests_artefacts/Release/Audio2MidiTests --e2e song.mp3 [drums electric_bass]   # echter Engine-Lauf, startet/stoppt die Engine selbst
pluginval --strictness-level 10 --validate ~/Library/Audio/Plug-Ins/VST3/Audio2MIDI.vst3
auval -strict -v aufx A2md A2Mo
```

Es gibt keinen Lint-Schritt und kein Einzeltest-Flag: `Audio2MidiTests` ohne Argumente führt beide `juce::UnitTest`-Klassen (`splitParts`, `handleStreamLine`) aus. Build-Verzeichnisse `build*/` sind gitignored.

## Architektur

Drei Schichten, alles in `src/`:

- **`Transcription.{h,cpp}`** (Namespace `a2m`, kein GUI-Code): der gesamte Engine-Kontakt.
  - `Engine` wird über `juce::SharedResourcePointer` von allen Plugin-Instanzen im Host-Prozess geteilt. Der Server stoppt mit der letzten Instanz und nur, wenn dieser Prozess ihn gestartet hat. Läuft auf Port 8722 schon ein Server, wird er wiederverwendet.
  - Start: `uvx --from muscriptor==<engineVersion>` löst die Python-3.12-Umgebung auf, danach wird das ermittelte Python direkt mit `-I -X utf8 -m muscriptor serve --host 127.0.0.1 --port 8722` gestartet. Absichtlich nicht über den `uvx`-Wrapper, weil ein Kill des Wrappers den Server verwaist zurückließe.
  - `TranscriptionJob` (eigener `juce::Thread`) lädt die Audiodatei per `POST /transcribe` hoch und liest den Server-Sent-Event-Stream (`handleStreamLine` → `StreamState`). Das Ergebnis ist eine Type-1-MIDI-Datei; `splitParts` zerlegt sie in „All parts“ plus eine Datei pro Instrument, jeweils mit Tempo-/Taktspur, damit sie im DAW-Raster landen.
  - `engineVersion` und `instrumentNames()` sind an `muscriptor==0.4.0` gekoppelt. Bei einem Versions-Bump beides gemeinsam prüfen.
- **`PluginProcessor.{h,cpp}`**: `processBlock` reicht Audio unverändert durch und schreibt beim Aufnehmen eine Kopie über `ThreadedWriter` (der Audio-Thread nimmt nur `try-lock` auf `recorderLock`). Aufnahmen sind 16-Bit-WAV, weil die Engine WAV über Pythons `wave`-Modul liest. Der State (`getStateInformation`) enthält nur die Instrumentenauswahl. Das Member `job` steht absichtlich zuletzt, damit es zuerst zerstört und gejoint wird.
- **`PluginEditor.{h,cpp}`**: UI per Timer-Polling von `getStatus()`. Datei-Drop rein, MIDI-Parts per externem Drag raus (`shouldDropFilesWhenDraggedExternally`).

Thread-Modell: Alles in `Audio2MidiProcessor` mit Kommentar „Message thread only“ nur vom Message-Thread aufrufen. `TranscriptionJob` meldet über `onUpdate` auf seinem eigenen Thread; `statusLock` schützt den Status.

`tests/Tests.cpp` kompiliert dasselbe `src/Transcription.cpp` wie das Plugin, daher testet `--e2e` exakt den Plugin-Code. Der Test-Target linkt kein GUI.

## Fallstricke

- Port **8722** (`a2m::enginePort`) ist bewusst gewählt, um nicht mit anderen lokalen Servern zu kollidieren. Nicht ändern.
- Upload-Limit 250 MB (Datei wird im Host-Speicher gepuffert), Verbindungs-Timeout 10 Minuten.
- Das HuggingFace-Token liegt in `~/.cache/huggingface/token`. Die Modellgewichte sind gated (CC BY-NC 4.0) und werden nie gebündelt.
- Nativer `NSOpenPanel` ist per Accessibility nicht erreichbar; GUI-Tests brauchen `--headed`.
- Release-Binaries sind unsigniert und nicht notarisiert. Windows baut, ist aber ungetestet.
- Lizenz AGPL-3.0 (wegen JUCE-Open-Source-Lizenz).
