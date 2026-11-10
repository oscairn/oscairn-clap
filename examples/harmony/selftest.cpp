// SPDX-License-Identifier: MIT
// oscairn.harmony: feed harmony events on the queried space, check the plugin sounds while keys
// are held and sleeps when they are not, keeps the last reading between events, and saves the
// host extension's reading as its state.
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
    cMajor.scale = 0b1010101010101; // C D E F G A B over two octaves
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

    oscairn_harmony_t released = cMajor;
    released.held[0] = released.held[1] = 0;
    released.chord_type = OSCAIRN_CHORD_NONE;
    const auto [stopped, quiet] = render(&released);
    check(stopped == CLAP_PROCESS_SLEEP && quiet == 0, "when the keys let go it stops");

    // Its state names the level, then the harmony the host extension reported for the last block.
    plugin.host.harmony = cMajor; // what the host says the part's harmony was
    OutStream saved;
    check(state->save(plugin.plugin, &saved.stream), "the state saves");
    const auto tail = saved.data.find('\n');
    check(tail != std::string::npos && saved.data.compare(0, 25, "oscairn-example-harmony-1") == 0 &&
              saved.data.size() == tail + 1 + sizeof(oscairn_harmony_t),
          "a level line, then the host's harmony reading verbatim");
    const auto* savedHarmony = reinterpret_cast<const oscairn_harmony_t*>(saved.data.data() + tail + 1);
    check(savedHarmony->chord_type == OSCAIRN_CHORD_MAJOR && savedHarmony->key_root == 0,
          "the mini host's harmony (C major) is what was saved");
    InStream back{saved.data, 0};
    check(state->load(plugin.plugin, &back.stream), "and loads its own state");

    // A host with no harmony extension: the level line still saves, and loads.
    plugin.host.hasHarmony = false;
    OutStream empty;
    check(state->save(plugin.plugin, &empty.stream) && empty.data.find('\n') == empty.data.size() - 1 &&
              empty.data.size() > 1,
          "with no harmony to report, only the level saves");
    InStream none{empty.data, 0};
    check(state->load(plugin.plugin, &none.stream), "and loads that level-only state");
    return finish("oscairn-example-harmony");
}
