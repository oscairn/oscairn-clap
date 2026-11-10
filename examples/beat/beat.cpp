// SPDX-License-Identifier: MIT
//
// oscairn.beat: a beat position that keeps running while the transport is stopped, the same for
// every plugin in a block, so plugins keep time and start together. Ask for the event space
// through CLAP's event registry and an oscairn_event_beat arrives at sample 0 of every block; in
// the later process calls of a block the host splits, the position moves on at the event's
// tempo. Here it clicks on every beat.
// See docs/rfcs/0032-arpeggiator.md and core/engine/tests/follower_fixture.cpp.
#include <clap/clap.h>
#include <oscairn-clap/beat.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    const char* const features[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
    const clap_plugin_descriptor_t descriptor{
        CLAP_VERSION_INIT, "org.oscairn.example.beat", "Oscairn Example Beat", "Oscairn", "", "", "", "0.1.0",
        "Plays on the free-running beat position (oscairn.beat)", features};

    struct Plugin
    {
        clap_plugin_t api{};
        const clap_host_t* host = nullptr;
        uint16_t beatSpace = UINT16_MAX;
        double rate = 48000, beat = -1, perSample = 0; // beat < 0 until the first event
        std::atomic<double> level{.5};
        double phase = 0;
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

    // Its state is the Level parameter. Never the beat: the host's position is not the plugin's
    // to keep, and its origin means nothing (beat.h).
    const clap_plugin_state_t state{
        [](const clap_plugin_t* p, const clap_ostream_t* stream) -> bool
        {
            char data[128];
            const int n = std::snprintf(data, sizeof data, "oscairn-example-beat-2 %.17g\n", self(p).level.load());
            return n > 0 && stream->write(stream, data, uint64_t(n)) == n;
        },
        [](const clap_plugin_t* p, const clap_istream_t* stream) -> bool
        {
            char data[128]{};
            size_t n = 0;
            for(;;)
            {
                char byte;
                const auto got = stream->read(stream, &byte, 1);
                if(got < 0 || got > 1 || n == sizeof(data) - 1) return false;
                if(!got) break;
                data[n++] = byte;
            }
            char header[32]{};
            double level = -1;
            if(std::sscanf(data, "%31s %lf", header, &level) != 2 || std::strcmp(header, "oscairn-example-beat-2") || !(level >= 0 && level <= 1))
                return false;
            self(p).level = level;
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
            // No registry, or a host that does not know the space: never get a beat event.
            if(registry && registry->query) registry->query(s.host, OSCAIRN_EVENT_SPACE_BEAT, &s.beatSpace);
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
        p.reset = [](const clap_plugin_t* pl) { auto& s = self(pl); s.beat = -1; s.perSample = 0; };
        p.on_main_thread = [](const clap_plugin_t*) {};
        p.process = [](const clap_plugin_t* pl, const clap_process_t* block) -> clap_process_status
        {
            auto& s = self(pl);
            uint32_t next = 0;
            const uint32_t count = block->in_events ? block->in_events->size(block->in_events) : 0;
            for(uint32_t i = 0; i < block->frames_count; ++i)
            {
                for(; next < count; ++next)
                {
                    const auto* h = block->in_events->get(block->in_events, next);
                    if(h->time > i) break;
                    if(h->space_id == CLAP_CORE_EVENT_SPACE_ID && h->type == CLAP_EVENT_PARAM_VALUE)
                    {
                        const auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
                        if(e->param_id == 1 && std::isfinite(e->value)) s.level = std::clamp(e->value, 0.0, 1.0);
                        continue;
                    }
                    if(h->space_id != s.beatSpace || h->type != OSCAIRN_EVENT_BEAT) continue;
                    const auto& e = *reinterpret_cast<const oscairn_event_beat_t*>(h);
                    s.beat = double(e.beats) / CLAP_BEATTIME_FACTOR; // the host's reading wins
                    s.perSample = e.tempo / 60 / s.rate; // and from here, on our own
                }
                float out = 0;
                if(s.beat >= 0)
                {
                    // On the beat: a short click at the level parameter.
                    const double sinceBeat = s.beat - std::floor(s.beat);
                    if(sinceBeat < .05) out = float(std::sin(s.phase) * s.level.load());
                    s.phase = std::fmod(s.phase + 880.0 * 2 * M_PI / s.rate, 2 * M_PI);
                    s.beat += s.perSample;
                }
                block->audio_outputs[0].data32[0][i] = block->audio_outputs[0].data32[1][i] = out;
            }
            return s.beat >= 0 ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_SLEEP;
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
