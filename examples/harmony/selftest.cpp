// SPDX-License-Identifier: MIT
// oscairn.harmony: feed harmony events on the queried space, check the plugin sounds while keys
// are held and sleeps when they are not, and keeps the last reading between events, through a
// reset, and (from the host extension) through a new activation.
#include "../mini-host.hpp"

int main(int argc, char** argv)
{
    if(argc < 2) { std::fprintf(stderr, "usage: %s PLUGIN.clap\n", argv[0]); return 2; }
    Plugin plugin(argv[1], "org.oscairn.example.harmony");
    const auto* state = plugin.extension<clap_plugin_state_t>(CLAP_EXT_STATE);
    check(state != nullptr, "the state interface is offered");
    check(plugin.activate(), "activates and starts processing");

    auto render = [&](const oscairn_harmony_t* harmony)
    {
        EventList events;
        if(harmony)
        {
            oscairn_event_harmony_t e{};
            e.header = {sizeof(e), 0, 1 /* the mini host's first space id */, OSCAIRN_EVENT_HARMONY, 0};
            e.harmony = *harmony;
            events.add(e);
        }
        float out[2][256]{};
        float* channels[2]{out[0], out[1]};
        const auto status = plugin.process(256, events, channels);
        double peak = 0;
        for(uint32_t f = 0; f < 256; ++f) peak = std::max(peak, double(std::abs(out[0][f])));
        return std::pair{status, peak};
    };
    // C major, C-E-G held: keys 60, 64, 67.
    oscairn_harmony_t cMajor{};
    cMajor.key_root = 0;
    cMajor.scale = 0b101010110101; // C D E F G A B: pitch classes 0 2 4 5 7 9 11
    cMajor.chord_root = 0;
    cMajor.chord_type = OSCAIRN_CHORD_MAJOR;
    cMajor.chord_bass = 0;
    cMajor.held[0] = 1ull << 60;      // held[k / 64]'s bit k % 64: key 60
    cMajor.held[1] = (1ull << 4) | (1ull << 7); // and 64, 67

    const auto [asleep, silent] = render(nullptr);
    check(asleep == CLAP_PROCESS_SLEEP && silent == 0, "with nothing held it is asleep and silent");
    const auto [awake, loud] = render(&cMajor);
    check(awake == CLAP_PROCESS_CONTINUE && loud > .05, "the held chord sounds");
    const auto [stillHeld, stillLoud] = render(nullptr); // no new event: the reading holds
    check(stillHeld == CLAP_PROCESS_CONTINUE && stillLoud > .05, "the last harmony holds between events");
    plugin.plugin->reset(plugin.plugin);
    check(render(nullptr).second > .05, "and through a reset");

    oscairn_harmony_t released = cMajor;
    released.held[0] = released.held[1] = 0;
    released.chord_type = OSCAIRN_CHORD_NONE;
    const auto [stopped, quiet] = render(&released);
    check(stopped == CLAP_PROCESS_SLEEP && quiet == 0, "when the keys let go it stops");

    // Activating again, it starts from the host extension's reading: no event comes until the
    // harmony changes.
    plugin.deactivate();
    plugin.host.harmony = cMajor;
    check(plugin.activate(), "activates again");
    check(render(nullptr).second > .05, "it resumes the chord the host last sent");

    // The state is the Level parameter alone.
    OutStream saved;
    check(state->save(plugin.plugin, &saved.stream), "the state saves");
    InStream back{saved.data, 0};
    check(state->load(plugin.plugin, &back.stream), "and loads back");
    return finish("oscairn-example-harmony");
}
