// SPDX-License-Identifier: MIT
//
// oscairn.harmony: the key and scale of the part a plugin is in, and the chord held on it.
// Two ways to read it: events, by asking for the event space OSCAIRN_EVENT_SPACE_HARMONY
// through CLAP's event registry (one arrives in the first block and in every block where the
// harmony changes, at the sample of the change); and the host extension OSCAIRN_EXT_HARMONY,
// on the main thread, for the last one as of the last block processed. This one plays a sine
// per key held at the part's input, and saves the host extension's reading as its state.
// See docs/rfcs/0033-script-block.md and core/engine/tests/follower_fixture.cpp.
#include <clap/clap.h>
#include <oscairn-clap/harmony.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{
    const char* const features[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
    const clap_plugin_descriptor_t descriptor{
        CLAP_VERSION_INIT, "org.oscairn.example.harmony", "Oscairn Example Harmony", "Oscairn", "", "", "", "0.1.0",
        "Follows the part's key, scale and held chord (oscairn.harmony)", features};

    struct Plugin
    {
        clap_plugin_t api{};
        const clap_host_t* host = nullptr;
        uint16_t harmonySpace = UINT16_MAX; // the id the host's event registry returned
        double rate = 48000, phase = 0;
        std::atomic<double> level{.5};
        std::atomic<uint32_t> events{0}; // harmony events seen; the self-test reads it back
        oscairn_harmony_t last{};        // audio thread: the latest reading, which holds between events
    };
    Plugin& self(const clap_plugin_t* p) { return *static_cast<Plugin*>(p->plugin_data); }

    const clap_plugin_params_t params{
        [](const clap_plugin_t*) -> uint32_t { return 1; },
        [](const clap_plugin_t*, uint32_t i, clap_param_info_t* out) -> bool
        {
            if(i) return false;
            *out = {};
            out->id = 1;
            out->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
            out->max_value = 1;
            out->default_value = .5;
            std::strcpy(out->name, "Level");
            return true;
        },
        [](const clap_plugin_t* p, clap_id id, double* v) -> bool
        {
            if(id != 1) return false;
            *v = self(p).level;
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
                    if(e->param_id == 1 && std::isfinite(e->value)) self(p).level = std::clamp(e->value, 0.0, 1.0);
                }
        }};

    const clap_plugin_audio_ports_t audio{
        [](const clap_plugin_t*, bool input) -> uint32_t { return input ? 0u : 1u; },
        [](const clap_plugin_t*, uint32_t i, bool input, clap_audio_port_info_t* out) -> bool
        {
            if(i || input) return false;
            *out = {};
            out->id = 0;
            out->flags = CLAP_AUDIO_PORT_IS_MAIN;
            out->channel_count = 2;
            out->port_type = CLAP_PORT_STEREO;
            out->in_place_pair = CLAP_INVALID_ID;
            std::strcpy(out->name, "Main");
            return true;
        }};
    const clap_plugin_note_ports_t notes{
        [](const clap_plugin_t*, bool input) -> uint32_t { return input ? 1u : 0u; },
        [](const clap_plugin_t*, uint32_t i, bool input, clap_note_port_info_t* out) -> bool
        {
            if(i || !input) return false;
            *out = {};
            out->id = 0;
            out->supported_dialects = out->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
            std::strcpy(out->name, "Notes");
            return true;
        }};

    // Its state is the level plus the harmony the host extension gives on the main thread —
    // no harmony yet, and no harmony bytes are saved; the plugin resumes from events anyway.
    const clap_plugin_state_t state{
        [](const clap_plugin_t* p, const clap_ostream_t* stream) -> bool
        {
            auto& s = self(p);
            const std::string head = "oscairn-example-harmony-1 " + std::to_string(s.level.load()) + '\n';
            if(stream->write(stream, head.data(), uint64_t(head.size())) != int64_t(head.size())) return false;
            const auto* ext = static_cast<const oscairn_host_harmony_t*>(s.host->get_extension(s.host, OSCAIRN_EXT_HARMONY));
            oscairn_harmony_t h{};
            if(!ext || !ext->get(s.host, &h)) return true; // just the level
            return stream->write(stream, &h, sizeof h) == int64_t(sizeof h);
        },
        [](const clap_plugin_t* p, const clap_istream_t* stream) -> bool
        {
            std::string data;
            char chunk[128];
            for(int64_t got; (got = stream->read(stream, chunk, sizeof chunk)) > 0;)
            {
                data.append(chunk, size_t(got));
                if(data.size() > 4096) return false;
            }
            char header[32]{};
            double level = -1;
            int used = 0;
            if(std::sscanf(data.c_str(), "%31s %lf%n", header, &level, &used) != 2 || std::strcmp(header, "oscairn-example-harmony-1") ||
               level < 0 || level > 1)
                return false;
            self(p).level = level; // reopening keeps the edit; harmony bytes, if any, resume from events
            return true;
        }};

    const clap_plugin_t* create(const clap_host_t* host)
    {
        auto* s = new Plugin;
        s->host = host;
        auto& p = s->api;
        p.desc = &descriptor;
        p.plugin_data = s;
        p.init = [](const clap_plugin_t* pl)
        {
            auto& s = self(pl);
            const auto* registry = static_cast<const clap_host_event_registry_t*>(s.host->get_extension(s.host, CLAP_EXT_EVENT_REGISTRY));
            // A host that does not know the space answers false; the plugin then never gets one.
            if(registry && registry->query) registry->query(s.host, OSCAIRN_EVENT_SPACE_HARMONY, &s.harmonySpace);
            return true;
        };
        p.destroy = [](const clap_plugin_t* pl) { delete &self(pl); };
        p.activate = [](const clap_plugin_t* pl, double rate, uint32_t, uint32_t)
        {
            if(!std::isfinite(rate) || rate < 1) return false;
            self(pl).rate = rate;
            return true;
        };
        p.deactivate = [](const clap_plugin_t*) {};
        p.start_processing = [](const clap_plugin_t*) { return true; };
        p.stop_processing = [](const clap_plugin_t*) {};
        p.reset = [](const clap_plugin_t* pl) { auto& s = self(pl); s.last = {}; s.events = 0; };
        p.on_main_thread = [](const clap_plugin_t*) {};
        p.process = [](const clap_plugin_t* pl, const clap_process_t* block) -> clap_process_status
        {
            auto& s = self(pl);
            params.flush(pl, block->in_events, block->out_events);
            uint32_t next = 0;
            const uint32_t count = block->in_events ? block->in_events->size(block->in_events) : 0;
            for(uint32_t i = 0; i < block->frames_count; ++i)
                for(; next < count; ++next)
                {
                    const auto* h = block->in_events->get(block->in_events, next);
                    if(h->time > i) break;
                    if(h->space_id != s.harmonySpace || h->type != OSCAIRN_EVENT_HARMONY) continue;
                    s.last = reinterpret_cast<const oscairn_event_harmony_t*>(h)->harmony; // the reading holds until the next
                    ++s.events;
                }
            const bool held = s.last.held[0] | s.last.held[1];
            for(uint32_t i = 0; i < block->frames_count; ++i)
            {
                double sum = 0;
                if(held)
                    for(int key = 24; key < 108; ++key) // a sine per held key
                        if(s.last.held[key / 64] >> (key % 64) & 1) sum += std::sin(s.phase * (1.0 + key / 64.0));
                s.phase = std::fmod(s.phase + 220.0 * 2 * M_PI / s.rate, 2 * M_PI);
                const auto out = float(sum * s.level.load() / 4);
                block->audio_outputs[0].data32[0][i] = block->audio_outputs[0].data32[1][i] = out;
            }
            return held ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_SLEEP;
        };
        p.get_extension = [](const clap_plugin_t*, const char* id) -> const void*
        {
            if(!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &audio;
            if(!std::strcmp(id, CLAP_EXT_NOTE_PORTS)) return &notes;
            if(!std::strcmp(id, CLAP_EXT_PARAMS)) return &params;
            if(!std::strcmp(id, CLAP_EXT_STATE)) return &state;
            return nullptr;
        };
        return &p;
    }
    const clap_plugin_factory_t factory{
        [](const clap_plugin_factory_t*) -> uint32_t { return 1; },
        [](const clap_plugin_factory_t*, uint32_t i) -> const clap_plugin_descriptor_t* { return i ? nullptr : &descriptor; },
        [](const clap_plugin_factory_t*, const clap_host_t* host, const char* id) -> const clap_plugin_t*
        { return std::strcmp(id, descriptor.id) ? nullptr : create(host); }};
}

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry{
    CLAP_VERSION_INIT, [](const char*) { return true; }, []() {},
    [](const char* id) -> const void* { return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? nullptr : &factory; }};
