// SPDX-License-Identifier: MIT
// oscairn.effect-contexts: make a second context from a running one, give each its own gain,
// and check both render, report resources, and refuse create past the engine's capacity.
#include "../mini-host.hpp"

#include <algorithm>

int main(int argc, char** argv)
{
    if(argc < 2) { std::fprintf(stderr, "usage: %s PLUGIN.clap\n", argv[0]); return 2; }
    Plugin first(argv[1], "org.oscairn.example.effect-contexts");
    const auto* ext = first.extension<oscairn_plugin_effect_contexts_t>(OSCAIRN_EXT_EFFECT_CONTEXTS);
    check(ext && ext->create && ext->resources, "oscairn.effect-contexts/draft is offered complete");
    if(!ext) return finish("oscairn-example-effect-contexts");

    check(first.activate(), "the originating context activates");
    oscairn_effect_resources_t resources{};
    check(ext->resources(first.plugin, &resources) && resources.capacity == 8 && resources.prepared == 1 && !resources.tail_active,
          "resources report capacity 8, one context, no tail");
    const uint64_t shared = resources.shared_bytes;

    // A second context from the same engine, while the first one renders.
    const clap_plugin_t* second = ext->create(first.plugin, &first.host.host);
    check(second != nullptr, "create makes a second context");
    if(!second) return finish("oscairn-example-effect-contexts");
    check(second->init(second), "the new context initializes");
    check(second->activate(second, 48000, 256, 256) && second->start_processing(second), "and activates at the engine's rate");
    check(ext->resources(second, &resources) && resources.prepared == 2 && resources.shared_bytes == shared,
          "both contexts share the engine's shared bytes");

    // Independent parameters: set the first to silence and the second to unity; each keeps its own.
    float in[2][256], out[2][256]{};
    const float* input[]{in[0], in[1]};
    float* output[]{out[0], out[1]};
    for(uint32_t f = 0; f < 256; ++f) in[0][f] = in[1][f] = .5f;
    auto setGain = [&](const clap_plugin_t* p, double v)
    {
        EventList e;
        clap_event_param_value_t ev;
        paramEvent(ev, 0, 1, v);
        e.add(ev);
        static_cast<const clap_plugin_params_t*>(p->get_extension(p, CLAP_EXT_PARAMS))->flush(p, &e.input, &e.output);
    };
    setGain(first.plugin, 0);
    setGain(second, 1);
    EventList idle;
    first.process(256, idle, output, input);
    double firstPeak = 0;
    for(uint32_t f = 0; f < 256; ++f) firstPeak = std::max(firstPeak, double(std::abs(out[0][f])));
    check(firstPeak == 0, "the first context passes silence at gain 0");
    for(auto& c : out) std::fill(std::begin(c), std::end(c), 0.f);
    clap_audio_buffer_t inPort{}, outPort{};
    inPort.channel_count = 2;
    inPort.data32 = const_cast<float**>(const_cast<const float**>(input));
    outPort.channel_count = 2;
    outPort.data32 = output;
    clap_process_t block{};
    block.frames_count = 256;
    block.audio_inputs = &inPort;
    block.audio_inputs_count = 1;
    block.audio_outputs = &outPort;
    block.audio_outputs_count = 1;
    block.in_events = &idle.input;
    block.out_events = &idle.output;
    second->process(second, &block);
    double secondPeak = 0;
    for(uint32_t f = 0; f < 256; ++f) secondPeak = std::max(secondPeak, double(std::abs(out[0][f])));
    check(std::abs(secondPeak - .5) < 1e-6, "the second amplifies at its own gain");
    check(ext->resources(second, &resources) && resources.tail_active, "a context that passed signal reports its tail");

    // Fill the engine: capacity 8 including the originating context.
    std::vector<const clap_plugin_t*> more;
    for(uint32_t i = 0; i < 16 && more.size() + 2 < 8; ++i)
        if(const clap_plugin_t* c = ext->create(first.plugin, &first.host.host)) more.push_back(c);
    check(more.size() + 2 == 8, "the engine takes exactly capacity contexts");
    check(ext->create(first.plugin, &first.host.host) == nullptr, "create refuses past the capacity");
    check(ext->resources(first.plugin, &resources) && resources.prepared == 8, "resources count them all");

    // Destroying a context cuts its tail and frees a slot again.
    for(auto* c : more) { c->deactivate(c); c->destroy(c); }
    check(ext->resources(first.plugin, &resources) && resources.prepared == 2, "destroyed contexts free their slots");
    second->deactivate(second);
    second->destroy(second);
    return finish("oscairn-example-effect-contexts");
}
