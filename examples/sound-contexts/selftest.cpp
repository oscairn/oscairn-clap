// SPDX-License-Identifier: MIT
// oscairn.sound-contexts: prepare two sounds, select them on different channels, and show
// each channel's notes taking their own sound; check the info, selection pins, discard rules
// and the snapshot round-trip through the extension's state.
#include "../mini-host.hpp"

int main(int argc, char** argv)
{
    if(argc < 2) { std::fprintf(stderr, "usage: %s PLUGIN.clap\n", argv[0]); return 2; }
    Plugin plugin(argv[1], "org.oscairn.example.sound-contexts");
    const auto* sounds = plugin.extension<oscairn_plugin_sound_contexts_t>(OSCAIRN_EXT_SOUND_CONTEXTS);
    check(sounds && sounds->capacity && sounds->get && sounds->prepare && sounds->select && sounds->discard && sounds->state,
          "oscairn.sound-contexts/draft-5 is offered complete");
    if(!sounds) return finish("oscairn-example-sound-contexts");
    check(sounds->capacity(plugin.plugin) == 4, "capacity is 4, the Default included");

    // The Default exists and is selected everywhere; it cannot be discarded.
    oscairn_sound_context_info_t info{};
    check(sounds->get(plugin.plugin, 0, &info) && info.id == 0 && info.selected_channels == 0xFFFF && !strcmp(info.name, "Default"),
          "slot 0 is the Default, selected on all channels");
    check(!sounds->discard(plugin.plugin, 0), "the Default cannot be discarded");

    // A sound with bad values or bad names is refused with a diagnostic.
    char message[256]{};
    const oscairn_sound_parameter_t bad{9, .5};
    check(sounds->prepare(plugin.plugin, "Bad", &bad, 1, nullptr, nullptr, message, sizeof message) == CLAP_INVALID_ID && message[0],
          std::string("a bad parameter is refused: ") + message);

    // Prepare a loud sound and a quiet one on the free slots.
    const oscairn_sound_parameter_t loud{1, 1.0}, quiet{1, .05};
    const uint32_t loudId = sounds->prepare(plugin.plugin, "Loud", &loud, 1, nullptr, nullptr, message, sizeof message);
    const uint32_t quietId = sounds->prepare(plugin.plugin, "Quiet", &quiet, 1, nullptr, nullptr, message, sizeof message);
    check(loudId != CLAP_INVALID_ID && quietId != CLAP_INVALID_ID && loudId != quietId, "two sounds are prepared with fresh IDs");
    // Selecting them pins them: Loud on channel 0, Quiet on channel 1.
    check(sounds->select(plugin.plugin, loudId, 0) && sounds->select(plugin.plugin, quietId, 1), "both selections are accepted");
    check(!sounds->discard(plugin.plugin, loudId), "a selected sound refuses discard");

    check(plugin.activate(), "activates and starts processing");
    // Drain selections, then hold a note on each channel; the loud one must dominate.
    float out[2][256]{};
    float* channels[2]{out[0], out[1]};
    EventList drain;
    plugin.process(64, drain, channels);

    constexpr uint32_t blocksPerSecond = 48000 / 256;
    auto playOne = [&](uint32_t seconds, int16_t channel)
    {
        double loudest = 0;
        EventList starts;
        clap_event_note_t n;
        noteEvent(n, 0, CLAP_EVENT_NOTE_ON, 1, channel, 60);
        starts.add(n);
        plugin.process(256, starts, channels);
        for(uint32_t b = 0; b < seconds * blocksPerSecond; ++b)
        {
            EventList in;
            plugin.process(256, in, channels);
            loudest = std::max(loudest, peak(out[0], 256));
        }
        EventList stops;
        noteEvent(n, 0, CLAP_EVENT_NOTE_OFF, 1, channel, 60);
        stops.add(n);
        plugin.process(256, stops, channels);
        return loudest;
    };
    const double loudPeak = playOne(1, 0), quietPeak = playOne(1, 1);
    check(loudPeak > .1, "channel 0 plays the loud sound");
    check(quietPeak > 1e-4 && quietPeak < loudPeak / 4, "channel 1 plays the quiet one beside it");

    // The info reports the selections.
    bool loudOnChannel0 = false;
    for(uint32_t slot = 0; slot < 4; ++slot)
        if(sounds->get(plugin.plugin, slot, &info) && info.id == loudId) loudOnChannel0 = info.selected_channels == 1;
    check(loudOnChannel0, "the loud sound reports channel 0 selected");

    // Once its notes have ended (playOne released them), discard: an idle sound goes, and its ID
    // is never reused.
    for(uint32_t b = 0; b < 20; ++b) { EventList in; plugin.process(256, in, channels); }
    // A channel keeps selecting it until a selection replaces it, so select the Default first.
    check(sounds->select(plugin.plugin, 0, -1), "selecting the Default everywhere is accepted");
    plugin.process(256, drain, channels);
    check(sounds->discard(plugin.plugin, loudId) && sounds->discard(plugin.plugin, quietId), "unselected idle sounds discard");
    const uint32_t nextId = sounds->prepare(plugin.plugin, "Again", nullptr, 0, nullptr, nullptr, message, sizeof message);
    check(nextId != CLAP_INVALID_ID && nextId > quietId, "IDs are never reused");

    // The snapshot holds every prepared patch and selection, and restores them with fresh IDs.
    check(sounds->select(plugin.plugin, nextId, 3), "select the new sound on channel 3");
    plugin.process(256, drain, channels);
    OutStream saved;
    check(sounds->state->save(plugin.plugin, &saved.stream), "the snapshot saves");
    check(sounds->select(plugin.plugin, 0, 3), "channel 3 goes back to the Default");
    plugin.process(256, drain, channels);
    InStream input{saved.data, 0};
    check(sounds->state->load(plugin.plugin, &input.stream), "the snapshot loads");
    bool restoredSelection = false;
    for(uint32_t slot = 0; slot < 4; ++slot)
    {
        oscairn_sound_context_info_t i{};
        if(sounds->get(plugin.plugin, slot, &i) && (i.selected_channels & 8)) restoredSelection = true;
    }
    check(restoredSelection, "the saved selection is back after the load");
    return finish("oscairn-example-sound-contexts");
}
