// SPDX-License-Identifier: MIT
//
// oscairn.memory: a delay that says up front what activating would allocate, so a host near its
// memory limit can refuse it before the allocation instead of after. A delay line is the classic
// case (Oscairn's plugins/delay); this one is two seconds, mono, its output on both channels. The
// estimate must be honest: the host counts the larger of the declaration and its own measurement.
// See docs/rfcs/0025-resource-budgets.md#declarations.
#include <clap/clap.h>
#include <oscairn-clap/memory.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
    constexpr double seconds = 2.0; // the delay line's length
    constexpr double mixInitial = .5;

    const char* const features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
    const clap_plugin_descriptor_t descriptor{
        CLAP_VERSION_INIT, "org.oscairn.example.memory", "Oscairn Example Memory", "Oscairn", "", "", "", "0.1.0",
        "A delay that declares what activating allocates (oscairn.memory)", features};

    struct Plugin
    {
        clap_plugin_t api{};
        std::atomic<double> mix{mixInitial};
        std::vector<float> line; // allocated at activate, freed at deactivate
        size_t write = 0;
        bool active = false;
    };
    Plugin& self(const clap_plugin_t* p) { return *static_cast<Plugin*>(p->plugin_data); }

    // The delay line's length at a rate: activate allocates exactly this, and estimate declares it.
    size_t lineFrames(double rate) { return size_t(seconds * rate) + 1; }
    const oscairn_plugin_memory_t memory{
        // Before activating: what activating would allocate, the delay line and nothing else (no
        // assets). It comes from the rate alone, and allocates nothing.
        [](const clap_plugin_t*, double rate, uint32_t, oscairn_memory_t* out) -> bool
        {
            if(!out || !std::isfinite(rate) || rate < 1) return false;
            *out = {0, lineFrames(rate) * sizeof(float), 0};
            return true;
        },
        // After activating: what it holds now, measured from the line it made.
        [](const clap_plugin_t* p, oscairn_memory_t* out) -> bool
        {
            const auto& s = self(p);
            if(!s.active || !out) return false;
            *out = {0, s.line.capacity() * sizeof(float), 0};
            return true;
        }};

    const clap_plugin_params_t params{
        [](const clap_plugin_t*) -> uint32_t { return 1; },
        [](const clap_plugin_t*, uint32_t i, clap_param_info_t* out) -> bool
        {
            if(i) return false;
            *out = {};
            out->id = 1;
            out->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
            out->max_value = 1;
            out->default_value = mixInitial;
            std::strcpy(out->name, "Mix");
            return true;
        },
        [](const clap_plugin_t* p, clap_id id, double* v) -> bool
        {
            if(id != 1) return false;
            *v = self(p).mix;
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
            if(end == text || !std::isfinite(parsed) || parsed < 0 || parsed > 1) return false;
            *v = parsed;
            return true;
        },
        [](const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*)
        {
            for(uint32_t i = 0; in && i < in->size(in); ++i)
                if(const auto* h = in->get(in, i); h->space_id == CLAP_CORE_EVENT_SPACE_ID && h->type == CLAP_EVENT_PARAM_VALUE)
                {
                    const auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
                    if(e->param_id == 1 && std::isfinite(e->value)) self(p).mix = std::clamp(e->value, 0.0, 1.0);
                }
        }};

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

    constexpr char stateHeader[] = "oscairn-example-memory-1";
    const clap_plugin_state_t state{
        [](const clap_plugin_t* p, const clap_ostream_t* stream) -> bool
        {
            char data[64];
            const int n = std::snprintf(data, sizeof data, "%s %.17g\n", stateHeader, self(p).mix.load());
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
            char header[sizeof(stateHeader)]{};
            double mix = 0;
            if(std::sscanf(data, "%24s %lf", header, &mix) != 2 || std::strcmp(header, stateHeader) || !std::isfinite(mix) || mix < 0 || mix > 1)
                return false;
            self(p).mix = mix;
            return true;
        }};

    const clap_plugin_t* create()
    {
        auto* s = new Plugin;
        auto& p = s->api;
        p.desc = &descriptor;
        p.plugin_data = s;
        p.init = [](const clap_plugin_t*) { return true; };
        p.destroy = [](const clap_plugin_t* pl) { delete &self(pl); };
        p.activate = [](const clap_plugin_t* pl, double rate, uint32_t, uint32_t)
        {
            if(!std::isfinite(rate) || rate < 8000 || rate > 192000) return false;
            auto& s = self(pl);
            s.line.assign(lineFrames(rate), 0.f);
            s.write = 0;
            s.active = true;
            return true;
        };
        p.deactivate = [](const clap_plugin_t* pl) { auto& s = self(pl); s.active = false; std::vector<float>().swap(s.line); };
        p.start_processing = [](const clap_plugin_t*) { return true; };
        p.stop_processing = [](const clap_plugin_t*) {};
        p.reset = [](const clap_plugin_t* pl)
        {
            auto& s = self(pl);
            std::fill(s.line.begin(), s.line.end(), 0.f);
            s.write = 0;
        };
        p.on_main_thread = [](const clap_plugin_t*) {};
        p.process = [](const clap_plugin_t* pl, const clap_process_t* block) -> clap_process_status
        {
            auto& s = self(pl);
            if(block->audio_outputs_count < 1 || block->audio_outputs[0].channel_count < 2 || s.line.empty()) return CLAP_PROCESS_ERROR;
            params.flush(pl, block->in_events, block->out_events);
            const double mix = s.mix;
            const size_t size = s.line.size();
            const bool input = block->audio_inputs_count > 0 && block->audio_inputs[0].channel_count >= 2;
            const float* in = input ? block->audio_inputs[0].data32[0] : nullptr;
            for(uint32_t f = 0; f < block->frames_count; ++f)
            {
                const float dry = in ? in[f] : 0.f;
                const float wet = s.line[s.write]; // the oldest sample, the one being replaced
                s.line[s.write] = dry;
                const auto out = float(dry * (1 - mix) + wet * mix);
                block->audio_outputs[0].data32[0][f] = block->audio_outputs[0].data32[1][f] = out;
                s.write = (s.write + 1) % size;
            }
            return CLAP_PROCESS_CONTINUE;
        };
        p.get_extension = [](const clap_plugin_t*, const char* id) -> const void*
        {
            if(!std::strcmp(id, OSCAIRN_EXT_MEMORY)) return &memory;
            if(!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &audio;
            if(!std::strcmp(id, CLAP_EXT_PARAMS)) return &params;
            if(!std::strcmp(id, CLAP_EXT_STATE)) return &state;
            return nullptr;
        };
        return &p;
    }
    const clap_plugin_factory_t factory{
        [](const clap_plugin_factory_t*) -> uint32_t { return 1; },
        [](const clap_plugin_factory_t*, uint32_t i) -> const clap_plugin_descriptor_t* { return i ? nullptr : &descriptor; },
        [](const clap_plugin_factory_t*, const clap_host_t*, const char* id) -> const clap_plugin_t*
        { return std::strcmp(id, descriptor.id) ? nullptr : create(); }};
}

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry{
    CLAP_VERSION_INIT, [](const char*) { return true; }, []() {},
    [](const char* id) -> const void* { return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? nullptr : &factory; }};
