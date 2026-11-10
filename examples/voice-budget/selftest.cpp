// SPDX-License-Identifier: MIT
// oscairn.voice-budget: hold more notes than voices, set a limit with set_limits, and check the
// plugin gives voices up itself — fading, reported through active_voices and notes, the count
// shrinking to the limit — instead of the host cutting whole notes.
#include "../mini-host.hpp"

int main(int argc, char** argv)
{
    if(argc < 2) { std::fprintf(stderr, "usage: %s PLUGIN.clap\n", argv[0]); return 2; }
    Plugin plugin(argv[1], "org.oscairn.example.voice-budget");
    const auto* budget = plugin.extension<oscairn_plugin_voice_budget_t>(OSCAIRN_EXT_VOICE_BUDGET);
    check(budget && budget->honors_volume && budget->set_limits && budget->active_voices && budget->notes,
          "oscairn.voice-budget/draft-1 is offered complete");
    if(!budget) return finish("oscairn-example-voice-budget");
    check(budget->honors_volume(plugin.plugin), "it says it honors the volume expression (checked below)");
    check(plugin.activate(), "activates and starts processing");

    float out[2][256]{};
    float* channels[2]{out[0], out[1]};
    auto holdNotes = [&](uint32_t blocks)
    {
        EventList starts;
        for(int i = 0; i < 6; ++i) // six notes, four voices
        {
            clap_event_note_t n;
            noteEvent(n, 0, CLAP_EVENT_NOTE_ON, 10 + i, 0, int16_t(60 + i));
            starts.add(n);
        }
        plugin.process(256, starts, channels);
        for(uint32_t b = 1; b < blocks; ++b)
        {
            EventList empty;
            plugin.process(256, empty, channels);
        }
    };
    holdNotes(30);
    double cost = 0;
    const uint32_t playing = budget->active_voices(plugin.plugin, &cost);
    check(playing > 0 && playing <= 4, "it reports the voices it sounds, never more than its pool");
    check(cost > 0, "it estimates a voice's cost");

    oscairn_voice_budget_note_t notes[16]{};
    const uint32_t reported = budget->notes(plugin.plugin, notes, 16);
    check(reported > 0 && reported <= playing, "every sounding note is reported, with its address");
    bool loudness = true;
    for(uint32_t i = 0; i < reported; ++i) loudness &= notes[i].voices >= 1 && notes[i].loudness > 0 && notes[i].loudness <= 1;
    check(loudness, "each note names its voices and a loudness in range");

    // The host's budget: two voices. The plugin must get there itself, fading what it gives up:
    // a voice given up fades for 3 ms (144 frames), counted until it has gone.
    budget->set_limits(plugin.plugin, 2, nullptr, 0); // between blocks, never during process
    EventList empty;
    plugin.process(64, empty, channels); // the limit lands at this block's start
    check(budget->active_voices(plugin.plugin, nullptr) > 2, "64 frames on, the voices it gave up are still fading");
    plugin.process(256, empty, channels);
    check(budget->active_voices(plugin.plugin, nullptr) <= 2, "a block later it keeps at most two voices");
    holdNotes(10);
    check(budget->active_voices(plugin.plugin, nullptr) <= 2, "and stays within it while playing on");

    // Note the limit does not change the notes it reports: a fading voice still sounds its note.
    const uint32_t during = budget->notes(plugin.plugin, notes, 16);
    check(during > 0, "notes still sounds its notes, fading ones reported");

    // Release everything: the voices go, and the plugin stops.
    budget->set_limits(plugin.plugin, 0, nullptr, 0);
    EventList releases;
    for(int i = 0; i < 6; ++i)
    {
        clap_event_note_t n;
        noteEvent(n, 0, CLAP_EVENT_NOTE_OFF, 10 + i, 0, int16_t(60 + i));
        releases.add(n);
    }
    plugin.process(256, releases, channels);
    for(uint32_t b = 0; b < 40; ++b) plugin.process(256, empty, channels);
    check(budget->active_voices(plugin.plugin, nullptr) == 0, "released voices end and stop being counted");
    check(plugin.process(256, empty, channels) == CLAP_PROCESS_SLEEP, "an idle instance reports sleep");

    // honors_volume: a note's volume expression at 0 silences it, and its loudness says so.
    EventList start;
    clap_event_note_t n;
    noteEvent(n, 0, CLAP_EVENT_NOTE_ON, 50, 0, 70);
    start.add(n);
    plugin.process(256, start, channels);
    EventList mute;
    clap_event_note_expression_t e{};
    e.header = {sizeof(e), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_EXPRESSION, 0};
    e.expression_id = CLAP_NOTE_EXPRESSION_VOLUME;
    e.note_id = 50;
    e.port_index = e.channel = e.key = -1;
    e.value = 0;
    mute.add(e);
    plugin.process(256, mute, channels);
    double peak = 0;
    for(uint32_t f = 0; f < 256; ++f) peak = std::max(peak, double(std::abs(out[0][f])));
    check(peak == 0, "volume 0 silences the note");
    check(budget->notes(plugin.plugin, notes, 16) == 1 && notes[0].loudness == 0, "and its reported loudness is 0");
    return finish("oscairn-example-voice-budget");
}
