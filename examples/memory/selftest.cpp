// SPDX-License-Identifier: MIT
// oscairn.memory: estimate and current must both answer, agree with what the plugin actually
// allocates, and grow with the sample rate — that is the whole of what a host can trust.
#include "../mini-host.hpp"

int main(int argc, char** argv)
{
    if(argc < 2) { std::fprintf(stderr, "usage: %s PLUGIN.clap\n", argv[0]); return 2; }
    Plugin plugin(argv[1], "org.oscairn.example.memory");
    const auto* memory = plugin.extension<oscairn_plugin_memory_t>(OSCAIRN_EXT_MEMORY);
    check(memory && memory->estimate && memory->current, "oscairn.memory/draft is offered with both functions");
    if(!memory) return finish("oscairn-example-memory");

    oscairn_memory_t low{}, high{};
    check(memory->estimate(plugin.plugin, 44100, 512, &low) && low.context_bytes > 0 && low.asset_bytes == 0,
          "the estimate names the delay line's bytes and no assets");
    check(memory->estimate(plugin.plugin, 96000, 512, &high) && high.context_bytes > low.context_bytes,
          "the estimate grows with the sample rate");

    // current before activating: not known. After: what it holds, what it estimated at that
    // rate (Plugin::activate uses 48000).
    oscairn_memory_t at48{};
    check(memory->estimate(plugin.plugin, 48000, 256, &at48), "the estimate answers at 48 kHz");
    check(!memory->current(plugin.plugin, &high), "current refuses before activating");
    check(plugin.activate(), "activates");
    check(memory->current(plugin.plugin, &high) && high.context_bytes == at48.context_bytes,
          "what it holds after activating is what it estimated at that rate");

    // And the plugin works: Mix 1 must hand the delayed signal through.
    float in[2][256], out[2][256]{};
    for(uint32_t f = 0; f < 256; ++f) in[0][f] = in[1][f] = (f % 64 < 32 ? .5f : -.5f);
    const float* input[]{in[0], in[1]};
    float* output[]{out[0], out[1]};
    EventList events;
    clap_event_param_value_t e;
    paramEvent(e, 0, 1, 1.0); // Mix full
    events.add(e);
    bool changed = false;
    for(uint32_t b = 0; b < 600 && !changed; ++b)
    {
        EventList empty;
        if(b == 0) plugin.process(256, events, output, input);
        else plugin.process(256, empty, output, input);
        for(uint32_t f = 0; f < 256; ++f) changed |= std::abs(out[0][f]) > 1e-6;
    }
    check(changed, "with the wet mix up it passes the delayed signal");
    return finish("oscairn-example-memory");
}
