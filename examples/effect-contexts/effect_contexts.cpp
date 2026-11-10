// SPDX-License-Identifier: MIT
//
// oscairn.effect-contexts: one gain engine, many independent effect slots. Each context is a
// separately scheduled interface with its own parameters, state and host callbacks, made with
// create() while the others render; the engine outlives its first context, and create refuses
// past the engine's capacity. The audio is one gain stage, so a context's sound is one value.
// See docs/rfcs/0007-effect-contexts.md and plugins/delay.
#include <clap/clap.h>
#include <oscairn-clap/effect-contexts.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace
{
    constexpr uint32_t capacity = 8;
    constexpr double gainMinimum = 0, gainMaximum = 2, gainInitial = .5;

    const char* const features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
    const clap_plugin_descriptor_t descriptor{
        CLAP_VERSION_INIT, "org.oscairn.example.effect-contexts", "Oscairn Example Effect Contexts", "Oscairn", "", "", "", "0.1.0",
        "Independent effect slots sharing one engine (oscairn.effect-contexts)", features};

    struct Engine
    {
        double sampleRate = 48000;
        uint32_t contexts = 0;
        std::atomic<uint64_t> sharedBytes{4096}; // the engine's own tables, counted once
    };

    struct Context
    {
        clap_plugin_t api{};
        std::shared_ptr<Engine> engine;
        std::atomic<double> gain{gainInitial};
        std::atomic<bool> tail{false}; // true while it has passed anything through
        explicit Context(std::shared_ptr<Engine> e) : engine(std::move(e)) { if(engine) ++engine->contexts; }
        ~Context() { if(engine) --engine->contexts; }
    };
    Context& self(const clap_plugin_t* p) { return *static_cast<Context*>(p->plugin_data); }

    void applyGain(Context& s, const clap_event_header_t* h)
    {
        if(!h || h->space_id != CLAP_CORE_EVENT_SPACE_ID || h->type != CLAP_EVENT_PARAM_VALUE) return;
        const auto& e = *reinterpret_cast<const clap_event_param_value_t*>(h);
        if(e.param_id != 1 || !std::isfinite(e.value)) return;
        s.gain = std::clamp(e.value, gainMinimum, gainMaximum);
    }

    const clap_plugin_params_t params{
        [](const clap_plugin_t*) -> uint32_t { return 1; },
        [](const clap_plugin_t*, uint32_t i, clap_param_info_t* out) -> bool
        {
            if(i) return false;
            *out = {};
            out->id = 1;
            out->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
            out->min_value = gainMinimum;
            out->max_value = gainMaximum;
            out->default_value = gainInitial;
            std::strcpy(out->name, "Gain");
            return true;
        },
        [](const clap_plugin_t* p, clap_id id, double* v) -> bool
        {
            if(id != 1) return false;
            *v = self(p).gain;
            return true;
        },
        [](const clap_plugin_t*, clap_id id, double v, char* text, uint32_t size) -> bool
        {
            if(id != 1 || !std::isfinite(v)) return false;
            std::snprintf(text, size, "%.0f%%", v * 100);
            return true;
        },
        [](const clap_plugin_t*, clap_id id, const char* text, double* v) -> bool
        {
            if(id != 1) return false;
            char* end = nullptr;
            const double parsed = std::strtod(text, &end);
            if(end == text || !std::isfinite(parsed) || parsed < gainMinimum || parsed > gainMaximum) return false;
            *v = parsed;
            return true;
        },
        [](const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*)
        { for(uint32_t i = 0; in && i < in->size(in); ++i) applyGain(self(p), in->get(in, i)); }};

    const clap_plugin_audio_ports_t audio{
        [](const clap_plugin_t*, bool) -> uint32_t { return 1; },
        [](const clap_plugin_t*, uint32_t i, bool input, clap_audio_port_info_t* out) -> bool
        {
            if(i) return false;
            *out = {};
            out->id = 0;
            out->flags = CLAP_AUDIO_PORT_IS_MAIN;
            out->channel_count = 2;
            out->port_type = CLAP_PORT_STEREO;
            out->in_place_pair = CLAP_INVALID_ID;
            std::strcpy(out->name, input ? "Input" : "Output");
            return true;
        }};

    // Text state: independent of byte order and locale.
    constexpr char stateHeader[] = "oscairn-example-effect-contexts-1";
    const clap_plugin_state_t state{
        [](const clap_plugin_t* p, const clap_ostream_t* stream) -> bool
        {
            char data[64];
            const int n = std::snprintf(data, sizeof data, "%s %.17g\n", stateHeader, self(p).gain.load());
            return n > 0 && stream->write(stream, data, uint64_t(n)) == n;
        },
        [](const clap_plugin_t* p, const clap_istream_t* stream) -> bool
        {
            char data[64]{};
            size_t n = 0;
            for(;;)
            {
                char byte;
                const auto got = stream->read(stream, &byte, 1);
                if(got < 0 || got > 1 || n == sizeof(data) - 1) return false;
                if(!got) break;
                data[n++] = byte;
            }
            double gain = 0;
            char header[sizeof(stateHeader)]{};
            if(std::sscanf(data, "%33s %lf", header, &gain) != 2 || std::strcmp(header, stateHeader) || !std::isfinite(gain) ||
               gain < gainMinimum || gain > gainMaximum)
                return false;
            self(p).gain = gain;
            return true;
        }};

    const clap_plugin_t* create(std::shared_ptr<Engine> engine = {});

    // The extension: create makes a new uninitialized context of the same engine; resources
    // reports the engine's shared bytes (identical for every node, so a host counts them once)
    // and this context's own, with the tail state.
    const oscairn_plugin_effect_contexts_t contexts{
        [](const clap_plugin_t* p, const clap_host_t*) -> const clap_plugin_t*
        {
            auto engine = self(p).engine;
            if(!engine || engine->contexts >= capacity) return nullptr; // refuse past the capacity
            return create(std::move(engine));
        },
        [](const clap_plugin_t* p, oscairn_effect_resources_t* out) -> bool
        {
            const auto& s = self(p);
            if(!out || !s.engine) return false;
            *out = {s.engine->sharedBytes.load(), sizeof(Context) + 4096, capacity, s.engine->contexts, s.tail.load()};
            return true;
        }};

    const clap_plugin_t* create(std::shared_ptr<Engine> engine)
    {
        auto* s = new Context(std::move(engine));
        auto& p = s->api;
        p.desc = &descriptor;
        p.plugin_data = s;
        p.init = [](const clap_plugin_t*) { return true; };
        p.destroy = [](const clap_plugin_t* pl) { delete &self(pl); };
        p.activate = [](const clap_plugin_t* pl, double rate, uint32_t, uint32_t)
        {
            if(!std::isfinite(rate) || rate < 1) return false;
            auto& s = self(pl);
            if(!s.engine || s.engine->sampleRate != rate)
            {
                if(s.engine && s.engine->contexts > 1) return false; // a live engine keeps its rate
                auto fresh = std::make_shared<Engine>();
                fresh->sampleRate = rate;
                fresh->contexts = s.engine ? s.engine->contexts : 0;
                s.engine = std::move(fresh);
            }
            return true;
        };
        p.deactivate = [](const clap_plugin_t*) {};
        p.start_processing = [](const clap_plugin_t*) { return true; };
        p.stop_processing = [](const clap_plugin_t*) {};
        p.reset = [](const clap_plugin_t* pl) { self(pl).tail = false; };
        p.on_main_thread = [](const clap_plugin_t*) {};
        p.process = [](const clap_plugin_t* pl, const clap_process_t* block) -> clap_process_status
        {
            auto& s = self(pl);
            if(block->audio_outputs_count < 1 || block->audio_outputs[0].channel_count < 2) return CLAP_PROCESS_ERROR;
            params.flush(pl, block->in_events, block->out_events);
            for(uint32_t i = 0; i < block->in_events->size(block->in_events); ++i) applyGain(s, block->in_events->get(block->in_events, i));
            const double gain = s.gain;
            const bool input = block->audio_inputs_count > 0 && block->audio_inputs[0].channel_count >= 2;
            for(uint32_t c = 0; c < 2; ++c)
            {
                const float* in = input ? block->audio_inputs[0].data32[c] : nullptr;
                float* out = block->audio_outputs[0].data32[c];
                for(uint32_t f = 0; f < block->frames_count; ++f)
                {
                    const auto value = float((in ? in[f] : 0.f) * gain);
                    out[f] = value;
                    if(value != 0) s.tail = true;
                }
            }
            return CLAP_PROCESS_CONTINUE;
        };
        p.get_extension = [](const clap_plugin_t*, const char* id) -> const void*
        {
            if(!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &audio;
            if(!std::strcmp(id, CLAP_EXT_PARAMS)) return &params;
            if(!std::strcmp(id, CLAP_EXT_STATE)) return &state;
            if(!std::strcmp(id, OSCAIRN_EXT_EFFECT_CONTEXTS)) return &contexts;
            return nullptr;
        };
        return &p;
    }
    const clap_plugin_factory_t factory{
        [](const clap_plugin_factory_t*) -> uint32_t { return 1; },
        [](const clap_plugin_factory_t*, uint32_t i) -> const clap_plugin_descriptor_t* { return i ? nullptr : &descriptor; },
        [](const clap_plugin_factory_t*, const clap_host_t*, const char* id) -> const clap_plugin_t*
        { return std::strcmp(id, descriptor.id) ? nullptr : create(std::make_shared<Engine>()); }};
}

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry{
    CLAP_VERSION_INIT, [](const char*) { return true; }, []() {},
    [](const char* id) -> const void* { return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? nullptr : &factory; }};
