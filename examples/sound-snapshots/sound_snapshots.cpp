// SPDX-License-Identifier: MIT
//
// oscairn.sound-snapshots (draft 4): instant Performance changes for a native instrument.
// When the next Performance's sounds belong to a plugin already playing, the host prepares
// them in that instance — beside the sounds still playing, while they ring out into the
// hidden four-channel ring-out buffer — and selects them at the cue, instead of loading the
// whole snapshot into an instance and cutting its tail. On the SDK this is one more table and
// one get_extension line over oscairn.sound-contexts; this example is that: it includes the
// sound-contexts example's engine and adds {prepareSnapshot, setRingOut, chokeRetiring}.
// See docs/rfcs/0005-sound-contexts.md "Preparing a snapshot beside", and plugins/basic-synth.
#define OSCAIRN_EXAMPLE_ID "org.oscairn.example.sound-snapshots"
#define OSCAIRN_EXAMPLE_NAME "Oscairn Example Sound Snapshots"
#define OSCAIRN_EXAMPLE_SUMMARY "A snapshot's sounds prepared beside the playing ones (oscairn.sound-snapshots)"
#define OSCAIRN_EXAMPLE_SNAPSHOTS 1
#include "../sound-contexts/sound_contexts.cpp"
