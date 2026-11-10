// SPDX-License-Identifier: MIT
//
// oscairn.param-target: a modulator with no sound of its own. It has numbered output lanes and
// sends oscairn_event_param_target events on them; the host routes each (plugin, lane) to the
// parameter the user mapped, in the same block, at the event's exact sample. The plugin never
// knows what it controls. Ask for the event space through CLAP's event registry: a host that
// does not know it answers false, and the plugin sends nothing. List the lanes through
// oscairn.param-target-lanes so a host can offer them for mapping.
// See docs/rfcs/0002-param-target.md and plugins/lfo.
#include <clap/clap.h>
#include <oscairn-clap/param-target.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    constexpr uint32_t laneCount = 2;
    const char* const laneNames[] = {"Slow", "Fast"};

    const char* const features[] = {CLAP_PLUGIN_FEATURE_NOTE_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr};
    const clap_plugin_descriptor_t descriptor{
        CLAP_VERSION_INIT, "org.oscairn.example.param-target", "Oscairn Example Param Target", "Oscairn", "", "", "", "0.1.0",
        "Two modulator lanes that move other plugins' parameters (oscairn.param-target)", features};

    struct Plugin
    {
        clap_plugin_t api{};
        const clap_host_t* host = nullptr;
        bool canSend = false; // the host knows the event space
        uint16_t space = 0;
        double rate = 48000, phase[2]{};
        std::atomic<double> rateHz{1}, depth{1};
    };
    Plugin& self(const clap_plugin_t* p) { return *static_cast<Plugin*>(p->plugin_data); }

    const clap_plugin_params_t params{
        [](const clap_plugin_t*) -> uint32_t { return 2; },
        [](const clap_plugin_t*, uint32_t i, clap_param_info_t* out) -> bool
        {
            if(i > 1) return false;
            *out = {};
            out->id = i + 1;
            out->flags = CLAP_PARAM_IS_AUTOMATABLE;
            out->max_value = i ? 1 : 8;
            out->default_value = 1;
            std::strcpy(out->name, i ? "Depth" : "Rate");
            return true;
        },
        [](const clap_plugin_t* p, clap_id id, double* v) -> bool
        {
            if(id < 1 || id > 2) return false;
            *v = id == 1 ? self(p).rateHz : self(p).depth;
            return true;
        },
        [](const clap_plugin_t*, clap_id id, double v, char* text, uint32_t size) -> bool
        {
            if(id < 1 || id > 2 || !std::isfinite(v)) return false;
            if(id == 1) std::snprintf(text, size, "%.2f Hz", v);
            else std::snprintf(text, size, "%.0f%%", v * 100);
            return true;
        },
        [](const clap_plugin_t*, clap_id id, const char* text, double* v) -> bool
        {
            if(id < 1 || id > 2) return false;
            char* end = nullptr;
            const double parsed = std::strtod(text, &end);
            const double maximum = id == 1 ? 8 : 1;
            if(end == text || !std::isfinite(parsed) || parsed < 0 || parsed > maximum) return false;
            *v = parsed;
            return true;
        },
        [](const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*)
        {
            for(uint32_t i = 0; in && i < in->size(in); ++i)
                if(const auto* h = in->get(in, i); h->space_id == CLAP_CORE_EVENT_SPACE_ID && h->type == CLAP_EVENT_PARAM_VALUE)
                {
                    const auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
                    if(e->param_id == 1 && std::isfinite(e->value)) self(p).rateHz = std::clamp(e->value, 0.0, 8.0);
                    if(e->param_id == 2 && std::isfinite(e->value)) self(p).depth = std::clamp(e->value, 0.0, 1.0);
                }
        }};

    // A note input, so a host plays it like an LFO; its notes do nothing but keep it awake.
    const clap_plugin_note_ports_t notes{
        [](const clap_plugin_t*, bool input) -> uint32_t { return input ? 1u : 0u; },
        [](const clap_plugin_t*, uint32_t i, bool input, clap_note_port_info_t* out) -> bool
        {
            if(i || !input) return false;
            *out = {};
            out->id = 0;
            out->supported_dialects = out->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
            std::strcpy(out->name, "Retrigger");
            return true;
        }};

    // The lanes, so Routing can offer "Connect lane to...".
    const oscairn_plugin_param_target_lanes_t lanes{
        [](const clap_plugin_t*) -> uint32_t { return laneCount; },
        [](const clap_plugin_t*, uint32_t index, oscairn_param_target_lane_info_t* info) -> bool
        {
            if(index >= laneCount) return false;
            info->lane = index;
            std::snprintf(info->name, sizeof info->name, "%s", laneNames[index]);
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
            s.canSend = registry && registry->query && registry->query(s.host, OSCAIRN_EVENT_SPACE_PARAM_TARGET, &s.space);
            return true;
        };
        p.destroy = [](const clap_plugin_t* pl) { delete &self(pl); };
        p.activate = [](const clap_plugin_t* pl, double rate, uint32_t, uint32_t)
        {
            if(!std::isfinite(rate) || rate < 8000) return false;
            auto& s = self(pl);
            s.rate = rate;
            s.phase[0] = s.phase[1] = 0;
            return true;
        };
        p.deactivate = [](const clap_plugin_t*) {};
        p.start_processing = [](const clap_plugin_t*) { return true; };
        p.stop_processing = [](const clap_plugin_t*) {};
        p.reset = [](const clap_plugin_t* pl) { auto& s = self(pl); s.phase[0] = s.phase[1] = 0; };
        p.on_main_thread = [](const clap_plugin_t*) {};
        p.process = [](const clap_plugin_t* pl, const clap_process_t* block) -> clap_process_status
        {
            auto& s = self(pl);
            params.flush(pl, block->in_events, block->out_events);
            const double hz = s.rateHz, depth = s.depth;
            for(uint32_t i = 0; i < block->frames_count; ++i)
            {
                // Lane 0 a sine at Rate, scaled by Depth (MODULATE: an amount around where the target
                // is); lane 1 a sine four times as fast as a position (SET: 0..1 over the range).
                const double slow = std::sin(2 * M_PI * s.phase[0]);
                const double fast = .5 + .5 * std::sin(2 * M_PI * s.phase[1]);
                if(s.canSend && block->out_events)
                {
                    oscairn_event_param_target_t e{};
                    e.header = {sizeof(e), i, s.space, OSCAIRN_EVENT_PARAM_TARGET, 0};
                    e.note_id = e.port_index = e.channel = e.key = -1;
                    for(uint32_t lane = 0; lane < laneCount; ++lane)
                    {
                        e.lane = lane;
                        e.value = lane == 0 ? depth * slow : fast;
                        e.mode = lane == 0 ? OSCAIRN_PARAM_TARGET_MODULATE : OSCAIRN_PARAM_TARGET_SET;
                        block->out_events->try_push(block->out_events, &e.header);
                    }
                }
                s.phase[0] = std::fmod(s.phase[0] + hz / s.rate, 1);
                s.phase[1] = std::fmod(s.phase[1] + 4 * hz / s.rate, 1);
            }
            return CLAP_PROCESS_CONTINUE;
        };
        p.get_extension = [](const clap_plugin_t*, const char* id) -> const void*
        {
            if(!std::strcmp(id, CLAP_EXT_PARAMS)) return &params;
            if(!std::strcmp(id, CLAP_EXT_NOTE_PORTS)) return &notes;
            if(!std::strcmp(id, OSCAIRN_EXT_PARAM_TARGET_LANES)) return &lanes;
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
