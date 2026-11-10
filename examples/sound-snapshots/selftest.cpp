// SPDX-License-Identifier: MIT
// oscairn.sound-snapshots: play a sound, prepare a whole snapshot beside it, check the new
// sounds get fresh IDs and the selected[] map, that the playing sound is untouched, and that
// once a channel stops selecting it, its output lands in the ring-out buffer, not the main port.
#include "../mini-host.hpp"

int main(int argc, char** argv)
{
    if(argc < 2) { std::fprintf(stderr, "usage: %s PLUGIN.clap\n", argv[0]); return 2; }
    Plugin plugin(argv[1], "org.oscairn.example.sound-snapshots");
    const auto* sounds = plugin.extension<oscairn_plugin_sound_contexts_t>(OSCAIRN_EXT_SOUND_CONTEXTS);
    const auto* snaps = plugin.extension<oscairn_plugin_sound_snapshots_t>(OSCAIRN_EXT_SOUND_SNAPSHOTS);
    check(sounds && snaps && snaps->prepare, "sound-contexts and sound-snapshots/draft-4 are both offered");
    check(snaps && (snaps->ring_out != nullptr) == (snaps->choke_retiring != nullptr),
          "ring_out and choke_retiring come together (the host refuses one without the other)");
    if(!sounds || !snaps) return finish("oscairn-example-sound-snapshots");

    char message[256]{};
    const oscairn_sound_parameter_t loud{1, 1.0};
    const uint32_t playing = sounds->prepare(plugin.plugin, "Playing", &loud, 1, nullptr, nullptr, message, sizeof message);
    check(playing != CLAP_INVALID_ID, "one sound is prepared");
    check(sounds->select(plugin.plugin, playing, 0), "and selected on channel 0");
    check(plugin.activate(), "activates and starts processing");

    float main0[256]{}, main1[256]{}, ring[4][256]{};
    float* mainChannels[2]{main0, main1};
    float* ringChannels[4]{ring[0], ring[1], ring[2], ring[3]};
    EventList drain;
    plugin.process(64, drain, mainChannels);
    EventList starts;
    clap_event_note_t n;
    noteEvent(n, 0, CLAP_EVENT_NOTE_ON, 1, 0, 60);
    starts.add(n);
    plugin.process(256, starts, mainChannels);
    for(uint32_t b = 0; b < 4; ++b) plugin.process(256, drain, mainChannels); // its attack is underway

    // A snapshot as state->save writes it, holding the playing sound plus one more.
    OutStream saved;
    check(sounds->state->save(plugin.plugin, &saved.stream), "the snapshot saves");
    check(saved.data.find("Playing") != std::string::npos, "the snapshot names its sound");

    // The next Performance's sounds, prepared beside it while it plays: nothing is selected,
    // the playing sound keeps its voices, and each channel's new ID comes back in selected[].
    uint32_t selected[16]{};
    InStream input{saved.data, 0};
    check(snaps->prepare(plugin.plugin, &input.stream, selected, message, sizeof message), "the snapshot's sounds prepare beside it");
    check(selected[0] != playing && selected[0] != CLAP_INVALID_ID, "channel 0's snapshot sound has a fresh ID");
    check(selected[1] != 0 && selected[1] != playing, "channel 1 takes the snapshot's Default under a fresh ID too");
    oscairn_sound_context_info_t info{};
    uint32_t prepared = 0, withVoices = 0;
    for(uint32_t slot = 0; slot < sounds->capacity(plugin.plugin); ++slot)
        if(sounds->get(plugin.plugin, slot, &info)) { ++prepared; withVoices += info.active_voices != 0; }
    check(prepared == sounds->capacity(plugin.plugin), "the snapshot's two sounds joined the instance beside the playing one");
    check(withVoices == 1, "the held voice still belongs to the sound that was playing");
    InStream broken{std::string("not a snapshot"), 0};
    check(!snaps->prepare(plugin.plugin, &broken.stream, selected, message, sizeof message) && message[0],
          std::string("a snapshot that does not parse is refused with a reason: ") + message);

    // The cue: channel 0 selects the incoming sound and a new note starts on it. The old sound
    // is selected by no channel now — it retires — and with a ring-out buffer its main output
    // goes to ring channels 0 and 1 instead of the main port, while the new note plays on main.
    check(sounds->select(plugin.plugin, selected[0], 0), "the cue selects the incoming sound");
    check(snaps->choke_retiring(plugin.plugin), "choke_retiring accepts its request");
    EventList cue;
    clap_event_note_t on;
    noteEvent(on, 0, CLAP_EVENT_NOTE_ON, 2, 0, 64);
    cue.add(on);
    double onMain = 0, onRing = 0;
    for(uint32_t b = 0; b < 8; ++b)
    {
        std::fill(std::begin(main0), std::end(main0), 0.f);
        std::fill(std::begin(main1), std::end(main1), 0.f);
        for(auto& c : ring) std::fill(std::begin(c), std::end(c), 0.f);
        clap_audio_buffer_t buffer{};
        buffer.channel_count = 4;
        buffer.data32 = ringChannels;
        snaps->ring_out(plugin.plugin, &buffer); // only this process() sees it
        plugin.process(256, b ? drain : cue, mainChannels);
        for(uint32_t f = 0; f < 256; ++f)
        {
            onMain += std::abs(main0[f]);
            // choke_retiring faded the old voice, so it renders into ring channels 2 and 3;
            // a retired-but-not-choked sound would use 0 and 1 (see retiring() in the helper).
            onRing += std::abs(ring[2][f]) + std::abs(ring[3][f]);
        }
    }
    check(onRing > 1e-3, "the retiring sound's main output arrives in the ring-out buffer");
    check(onMain > 1e-3, "the incoming sound plays on the main port meanwhile");
    return finish("oscairn-example-sound-snapshots");
}
