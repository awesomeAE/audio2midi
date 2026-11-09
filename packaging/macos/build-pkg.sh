#!/bin/sh
# Builds Audio2MIDI.pkg, which installs the VST3 and AU into the current user's
# ~/Library/Audio/Plug-Ins without asking for an administrator password.
# Usage: packaging/macos/build-pkg.sh build/Audio2Midi_artefacts/Release 0.3.0 Audio2MIDI.pkg
set -eu

artefacts=$1
version=$2
out=$3
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

component() { # <bundle> <install folder> <id> <scripts>
    mkdir -p "$work/root-$3"
    cp -R "$1" "$work/root-$3/"
    pkgbuild --analyze --root "$work/root-$3" "$work/$3.plist" >/dev/null
    # Installer would otherwise "upgrade" any copy of the bundle it finds elsewhere on disk.
    plutil -replace 0.BundleIsRelocatable -bool NO "$work/$3.plist"
    pkgbuild --quiet --root "$work/root-$3" --component-plist "$work/$3.plist" --install-location "$2" \
             --identifier "org.audio2midi.plugin.$3" --version "$version" --scripts "$here/scripts/$3" "$work/$3.pkg"
}

component "$artefacts/VST3/Audio2MIDI.vst3" /Library/Audio/Plug-Ins/VST3 vst3
component "$artefacts/AU/Audio2MIDI.component" /Library/Audio/Plug-Ins/Components au

cat > "$work/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Audio2MIDI $version</title>
    <options customize="never" hostArchitectures="x86_64,arm64"/>
    <domains enable_anywhere="false" enable_currentUserHome="true" enable_localSystem="false"/>
    <choices-outline><line choice="default"/></choices-outline>
    <choice id="default" title="Audio2MIDI">
        <pkg-ref id="org.audio2midi.plugin.vst3"/>
        <pkg-ref id="org.audio2midi.plugin.au"/>
    </choice>
    <pkg-ref id="org.audio2midi.plugin.vst3" version="$version" auth="none">vst3.pkg</pkg-ref>
    <pkg-ref id="org.audio2midi.plugin.au" version="$version" auth="none">au.pkg</pkg-ref>
</installer-gui-script>
XML

productbuild --quiet --distribution "$work/distribution.xml" --package-path "$work" "$out"
