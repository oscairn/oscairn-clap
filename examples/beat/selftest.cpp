// SPDX-License-Identifier: MIT
// oscairn.beat: feed the plugin a beat event, then blocks without one (the later calls of a block
// the host splits), and check it clicks exactly when the position crosses the next whole beat at
// the event's tempo; a new reading replaces its own position.
#include "../mini-host.hpp"

int main(int argc, char** argv)
{
    if(argc < 2) { std::fprintf(stderr, "usage: %s PLUGIN.clap\n", argv[0]); return 2; }
    Plugin plugin(argv[1], "org.oscairn.example.beat");
    const auto* state = plugin.extension<clap_plugin_state_t>(CLAP_EXT_STATE);
    check(state != nullptr, "the state interface is offered");
    check(plugin.activate(), "activates and starts processing");

    float out[2][256]{};
    float* channels[2]{out[0], out[1]};
    EventList none;
    check(plugin.process(256, none, channels) == CLAP_PROCESS_SLEEP && out[0][0] == 0 && out[0][255] == 0,
          "before any beat event it is asleep and silent");

    // One event at `beats` and `tempo`, then blocks without: the frame of the first click, or -1.
    auto firstClick = [&](double beats, double tempo) -> long
    {
        EventList events;
        oscairn_event_beat_t e{};
        e.header = {sizeof(e), 0, 1 /* the mini host's first space id */, OSCAIRN_EVENT_BEAT, 0};
        e.beats = clap_beattime(beats * CLAP_BEATTIME_FACTOR);
        e.tempo = tempo;
        events.add(e);
        for(long block = 0; block < 200; ++block)
        {
            plugin.process(256, block ? none : events, channels);
            for(long f = 0; f < 256; ++f)
                if(out[0][f] != 0) return block * 256 + f;
        }
        return -1;
    };
    // From beat 3.71 at 120 BPM (24000 frames a beat at 48 kHz), beat 4 is .29 beats on.
    check(std::abs(firstClick(3.71, 120) - 6960) <= 1, "it clicks at the next beat, moving at the event's tempo between events");
    // A new reading replaces the position: from 7.9 at 60 BPM, beat 8 is 4800 frames on.
    check(std::abs(firstClick(7.9, 60) - 4800) <= 1, "a new reading and tempo replace its own position");

    // The state is the Level parameter alone.
    OutStream saved;
    check(state->save(plugin.plugin, &saved.stream), "the state saves");
    InStream back{saved.data, 0};
    check(state->load(plugin.plugin, &back.stream), "and loads back");
    return finish("oscairn-example-beat");
}
