// SPDX-License-Identifier: MIT
//
// oscairn.ui (draft 5): one sine per note, three parameters, and a hint for each.
// Brightness is the filter's cutoff (its role: the Filter bank's first knob), Level the
// output. The envelope's stages are one group (drawn joined) and one rich view: an ADSR,
// whose stage times come from seconds() rather than from the value texts.
// See docs/plugin-porting.md, "Editor hints and roles".
#include <clap/clap.h>
#include <oscairn-clap/ui.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
    // id = index + 1. Brightness, Level, then the envelope's four stages.
    struct Parameter
    {
        const char* name;
        double maximum, initial;
        const char* unit;
        uint32_t widget, priority, group, role, field;
    };
    constexpr std::array<Parameter, 6> parameters{{
        {"Brightness", 1, .6, "%", OSCAIRN_WIDGET_KNOB, 1, 0, OSCAIRN_ROLE_CUTOFF, OSCAIRN_FIELD_NONE},
        {"Level", 1, .5, "%", OSCAIRN_WIDGET_KNOB, 2, 0, OSCAIRN_ROLE_LEVEL, OSCAIRN_FIELD_NONE},
        {"Attack", 2, .005, "s", OSCAIRN_WIDGET_KNOB, 0, 1, OSCAIRN_ROLE_ATTACK, OSCAIRN_FIELD_ATTACK},
        {"Decay", 2, .2, "s", OSCAIRN_WIDGET_KNOB, 0, 1, OSCAIRN_ROLE_DECAY, OSCAIRN_FIELD_DECAY},
        {"Sustain", 1, .4, "%", OSCAIRN_WIDGET_KNOB, 0, 1, OSCAIRN_ROLE_SUSTAIN, OSCAIRN_FIELD_SUSTAIN},
        {"Release", 2, .15, "s", OSCAIRN_WIDGET_KNOB, 0, 1, OSCAIRN_ROLE_RELEASE, OSCAIRN_FIELD_RELEASE},
    }};
    constexpr size_t Count = parameters.size();
    constexpr uint32_t envelopeView = 1; // every stage is in it

    const char* const features[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
    const clap_plugin_descriptor_t descriptor{
        CLAP_VERSION_INIT, "org.oscairn.example.ui", "Oscairn Example UI", "Oscairn", "", "", "", "0.1.0",
        "A sine that declares oscairn.ui hints, roles and an ADSR view", features};

    struct Plugin
    {
        clap_plugin_t api{};
        double rate = 48000;
        std::array<std::atomic<double>, Count> values{}; // the main thread reads these while audio runs
        std::atomic<int> held{0};
        double phase = 0, level = 0, low = 0;
        int stage = 0; // 0 idle, 1 attack, 2 decay/sustain, 3 release
        Plugin() { for(size_t i = 0; i < Count; ++i) values[i] = parameters[i].initial; }
        double value(size_t i) const { return values[i].load(std::memory_order_relaxed); }
    };
    Plugin& self(const clap_plugin_t* p) { return *static_cast<Plugin*>(p->plugin_data); }

    const clap_plugin_params_t params{
        [](const clap_plugin_t*) -> uint32_t { return uint32_t(Count); },
        [](const clap_plugin_t*, uint32_t i, clap_param_info_t* out) -> bool
        {
            if(i >= Count) return false;
            const auto& p = parameters[i];
            *out = {};
            out->id = i + 1;
            out->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
            out->max_value = p.maximum;
            out->default_value = p.initial;
            std::snprintf(out->name, sizeof out->name, "%s", p.name);
            return true;
        },
        [](const clap_plugin_t* p, clap_id id, double* v) -> bool
        {
            if(id < 1 || id > Count) return false;
            *v = self(p).value(id - 1);
            return true;
        },
        [](const clap_plugin_t*, clap_id id, double v, char* text, uint32_t size) -> bool
        {
            if(id < 1 || id > Count || !std::isfinite(v)) return false;
            if(id <= 2 || id == 5) std::snprintf(text, size, "%.0f%%", v * 100);
            else std::snprintf(text, size, "%.3f s", v);
            return true;
        },
        [](const clap_plugin_t*, clap_id id, const char* text, double* v) -> bool
        {
            if(id < 1 || id > Count) return false;
            char* end = nullptr;
            double parsed = std::strtod(text, &end);
            if(end == text || !std::isfinite(parsed) || parsed < 0 || parsed > parameters[id - 1].maximum) return false;
            *v = parsed;
            return true;
        },
        [](const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*)
        {
            for(uint32_t i = 0; in && i < in->size(in); ++i)
                if(const auto* h = in->get(in, i); h->space_id == CLAP_CORE_EVENT_SPACE_ID && h->type == CLAP_EVENT_PARAM_VALUE)
                {
                    const auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
                    if(e->param_id >= 1 && e->param_id <= Count)
                        self(p).values[e->param_id - 1] = std::clamp(e->value, 0.0, parameters[e->param_id - 1].maximum);
                }
        }};

    // Every parameter gets a hint, not only the ones with a role.
    const oscairn_plugin_ui_t ui{
        [](const clap_plugin_t*, clap_id id, oscairn_param_hint_t* hint) -> bool
        {
            if(id < 1 || id > Count) return false;
            const auto& p = parameters[id - 1];
            *hint = {p.widget, p.priority, CLAP_INVALID_ID, p.group, p.role, p.field ? envelopeView : 0u, p.field, 0};
            return true;
        },
        nullptr, // no FM algorithm view
        // Draft 5: how long a stage takes at a value. The sustain is a level, so it has none.
        [](const clap_plugin_t*, clap_id id, double value, double* seconds) -> bool
        {
            if(id < 3 || id > Count || id == 5) return false;
            *seconds = value;
            return true;
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
            if(!std::isfinite(rate) || rate < 1) return false;
            self(pl).rate = rate;
            return true;
        };
        p.deactivate = [](const clap_plugin_t*) {};
        p.start_processing = [](const clap_plugin_t*) { return true; };
        p.stop_processing = [](const clap_plugin_t*) {};
        p.reset = [](const clap_plugin_t* pl) { auto& s = self(pl); s.held = 0; s.level = 0; s.low = 0; s.stage = 0; };
        p.on_main_thread = [](const clap_plugin_t*) {};
        p.process = [](const clap_plugin_t* pl, const clap_process_t* block) -> clap_process_status
        {
            auto& s = self(pl);
            params.flush(pl, block->in_events, block->out_events);
            for(uint32_t i = 0; i < block->in_events->size(block->in_events); ++i)
            {
                const auto* h = block->in_events->get(block->in_events, i);
                if(h->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
                if(h->type == CLAP_EVENT_NOTE_ON)
                {
                    ++s.held;
                    s.stage = 1;
                }
                else if(h->type == CLAP_EVENT_NOTE_OFF || h->type == CLAP_EVENT_NOTE_CHOKE)
                {
                    s.held = std::max(0, s.held - 1);
                    if(!s.held && s.stage) s.stage = 3;
                }
            }
            const auto attack = s.value(2), decay = s.value(3), sustain = s.value(4), release = s.value(5);
            for(uint32_t f = 0; f < block->frames_count; ++f)
            {
                // A linear attack, an exponential decay to the sustain, an exponential release.
                switch(s.stage)
                {
                case 1: // attack to full
                    s.level = std::min(1.0, s.level + (attack > 0 ? 1.0 / (attack * s.rate) : 1.0));
                    if(s.level >= 1.0) s.stage = 2;
                    break;
                case 2: // decay to the sustain level, then hold
                    s.level = sustain + (s.level - sustain) * (decay > 0 ? std::exp(-1.0 / (decay * s.rate)) : 0.0);
                    break;
                case 3: // release to zero
                    s.level *= release > 0 ? std::exp(-1.0 / (release * s.rate)) : 0.0;
                    if(s.level < 1e-4) { s.level = 0; s.stage = 0; }
                    break;
                default: s.level = 0;
                }
                // Brightness opens a one-pole low-pass over the sine.
                const double cutoff = 80.0 + s.value(0) * 6000.0;
                const double g = 1.0 - std::exp(-2.0 * M_PI * cutoff / s.rate);
                const double oscillator = std::sin(s.phase);
                s.phase = std::fmod(s.phase + 440.0 * 2 * M_PI / s.rate, 2 * M_PI);
                s.low += g * (oscillator - s.low);
                const auto out = float(s.low * s.level * s.value(1) * .5);
                for(uint32_t c = 0; c < block->audio_outputs[0].channel_count; ++c) block->audio_outputs[0].data32[c][f] = out;
            }
            return s.held || s.level > 0 ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_SLEEP;
        };
        p.get_extension = [](const clap_plugin_t*, const char* id) -> const void*
        {
            if(!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &audio;
            if(!std::strcmp(id, CLAP_EXT_NOTE_PORTS)) return &notes;
            if(!std::strcmp(id, CLAP_EXT_PARAMS)) return &params;
            if(!std::strcmp(id, OSCAIRN_EXT_UI)) return &ui;
            return nullptr;
        };
        return &s->api;
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
