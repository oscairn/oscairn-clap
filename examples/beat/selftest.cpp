// SPDX-License-Identifier: MIT
// oscairn.beat: ask for the event space, feed the plugin its beat events at sample 0 of a few
// blocks, and check the position it reports moves at the event's tempo — including through the
// blocks with no event, which is the whole point: the beat runs while the transport is stopped.
#include "../mini-host.hpp"

int main(int argc, char** argv)
{
    if(argc < 2) { std::fprintf(stderr, "usage: %s PLUGIN.clap\n", argv[0]); return 2; }
    Plugin plugin(argv[1], "org.oscairn.example.beat");
    const auto* state = plugin.extension<clap_plugin_state_t>(CLAP_EXT_STATE);
    check(state != nullptr, "the state interface is offered");
    check(plugin.activate(), "activates and starts processing");

    // The mini host hands out space ids from 1; the plugin queried oscairn.beat first.
    auto beatBlock = [&](double beatsAt, double tempo, bool withEvent)
    {
        EventList events;
        if(withEvent)
        {
            oscairn_event_beat_t e{};
            e.header = {sizeof(e), 0, 1, OSCAIRN_EVENT_BEAT, 0};
            e.beats = clap_beattime(beatsAt * CLAP_BEATTIME_FACTOR);
            e.tempo = tempo;
            events.add(e);
        }
        float out[2][256]{};
        float* channels[2]{out[0], out[1]};
        const auto status = plugin.process(256, events, channels);
        double peak = 0;
        for(uint32_t f = 0; f < 256; ++f) peak = std::max(peak, double(std::abs(out[0][f])));
        return std::pair{status, peak};
    };
    const auto [asleep, silent] = beatBlock(0, 120, false);
    check(asleep == CLAP_PROCESS_SLEEP && silent == 0, "before any beat event it is asleep and silent");

    beatBlock(4, 120, true); // the host's position: beat 4
    const auto [awake, loud] = beatBlock(0, 120, false);
    check(awake == CLAP_PROCESS_CONTINUE && loud > .1, "a beat event wakes it and it plays on the beat");

    // Its state is where the beat got to. Four silent 256-frame blocks at 120 BPM move it
    // 4*256/48000*2 = .04266 beats; the last event fixed beat 4 plus the block it arrived in.
    auto position = [&]
    {
        OutStream saved;
        check(state->save(plugin.plugin, &saved.stream), "the state saves");
        double beat = -1, tempo = 0;
        if(std::sscanf(saved.data.c_str() + sizeof("oscairn-example-beat-1"), "%lf %lf", &beat, &tempo) != 2) return -1.0;
        return beat;
    };
    const double first = position();
    check(first > 4 && first < 4.1, "the position came from the event and ran on through its block");
    beatBlock(0, 120, false);
    beatBlock(0, 120, false);
    const double later = position();
    const double expected = first + 2 * 256.0 / 48000 * (120 / 60.0); // two blocks at 120 BPM
    check(later > first, "the position never goes back and keeps moving with no new event");
    check(std::abs(later - expected) < 1e-9, "it moves at exactly the tempo the event carried");

    // A stopped transport changes nothing: still no event, still moving; a jump forward jumps.
    beatBlock(20, 60, true);
    const double jumped = position();
    check(jumped > 20, "a jump ahead moves the position to the host's new reading");
    beatBlock(10, 60, true); // a jump backwards does not move it back
    check(position() >= jumped, "the position never runs backwards, even when the host jumps back");
    return finish("oscairn-example-beat");
}
