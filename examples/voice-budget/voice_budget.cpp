// SPDX-License-Identifier: MIT
//
// oscairn.voice-budget (draft 1): the host shares one processing budget among every plugin; for
// an instrument with this extension it reads what the plugin plays and tells it how many voices
// it may keep. The plugin gives voices up itself — never a held note of a mono or reserved
// context, released before held, the new voice's own context first, lower priority, quietest,
// oldest — and fades what it gives up. The voice pool here is four sines; the SDK helper
// (voice-budget-plugin.hpp) decides the order, the plugin owns the voices and the fades.
// See docs/rfcs/0018-voice-budget-and-stealing.md and plugins/basic-synth.
#include <clap/clap.h>
#include <oscairn-clap/voice-budget-plugin.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    constexpr size_t pool = 4;
    constexpr double chokeSeconds = .003; // 3 ms: enough, the header says 2 to 5

    const char* const features[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
    const clap_plugin_descriptor_t descriptor{
        CLAP_VERSION_INIT, "org.oscairn.example.voice-budget", "Oscairn Example Voice Budget", "Oscairn", "", "", "", "0.1.0",
        "A four-voice sine that gives voices up inside the host's budget (oscairn.voice-budget)", features};

    struct Voice
    {
        bool active = false, held = false;
        int32_t noteId = -1;
        int16_t channel = 0, key = 0;
        double velocity = 0, level = 0, phase = 0;
        double volume = 1; // CLAP_NOTE_EXPRESSION_VOLUME, linear
        uint32_t chokeLeft = 0, chokeLength = 0; // the fade of a voice given up
        uint64_t started = 0;
    };
    struct Plugin
    {
        clap_plugin_t api{};
        double rate = 48000;
        std::array<Voice, pool> voices{};
        std::atomic<double> level{.5};
        oscairn::voices::Limits limits;    // set_limits' settings, copied
        uint64_t started = 0;
        uint32_t chokeFrames = 144;
    };
    Plugin& self(const clap_plugin_t* p) { return *static_cast<Plugin*>(p->plugin_data); }

    // A voice's gain now: envelope, velocity, volume expression and the fade of a voice given up
    // (voice-budget.h's loudness).
    double loudness(const Voice& v)
    {
        const double fade = v.chokeLength ? v.chokeLeft / double(v.chokeLength) : 1.0;
        return v.level * v.velocity * v.volume * fade;
    }

    // Whether an event's note address (-1: any) names this voice.
    bool addressed(const Voice& v, int32_t noteId, int16_t channel, int16_t key)
    {
        return v.active && (noteId < 0 || v.noteId == noteId) && (channel < 0 || v.channel == channel) && (key < 0 || v.key == key);
    }

    // The voice to give up for a new one, by the header's order. A plugin without sound contexts
    // reads context 0's settings, so every candidate says context 0, never selected.
    int victim(Plugin& s)
    {
        std::array<oscairn::voices::Candidate, pool> described{};
        for(size_t i = 0; i < pool; ++i)
            if(s.voices[i].active)
                described[i] = {0, false, s.voices[i].held, s.voices[i].chokeLength != 0, false,
                                loudness(s.voices[i]), s.voices[i].started};
        return oscairn::voices::victim(pool, [&](size_t i) { return s.voices[i].active ? &described[i] : nullptr; }, s.limits,
                                       oscairn::voices::none);
    }

    // Past the limit set_limits gave, fade voices out by the header's order until it holds.
    void keepWithinLimit(Plugin& s)
    {
        uint32_t sounding = 0;
        for(const auto& v : s.voices) sounding += v.active && !v.chokeLength;
        for(; s.limits.limit && sounding > s.limits.limit; --sounding)
        {
            const int i = victim(s);
            if(i < 0) break;
            auto& v = s.voices[size_t(i)];
            v.held = false;
            v.chokeLength = v.chokeLeft = s.chokeFrames;
        }
    }

    void startVoice(Plugin& s, const clap_event_note_t& note)
    {
        Voice* slot = nullptr;
        for(auto& v : s.voices)
            if(!v.active) { slot = &v; break; }
        if(!slot)
        {
            // The pool is full, so there is no slot to fade in: cut a voice already fading (a choke or
            // a steal), the furthest gone first; when none is, cut the victim the order names.
            for(auto& v : s.voices)
                if(v.chokeLength && (!slot || v.chokeLeft < slot->chokeLeft)) slot = &v;
            if(!slot)
            {
                const int i = victim(s);
                if(i < 0) return;
                slot = &s.voices[size_t(i)];
            }
        }
        *slot = Voice{};
        slot->active = slot->held = true;
        slot->noteId = note.note_id;
        slot->channel = note.channel;
        slot->key = note.key;
        slot->velocity = note.velocity;
        slot->started = ++s.started;
    }

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

    // Every entry is required. honors_volume: every voice follows CLAP_NOTE_EXPRESSION_VOLUME.
    const oscairn_plugin_voice_budget_t budget{
        [](const clap_plugin_t*) -> bool { return true; },
        [](const clap_plugin_t* p, uint32_t limit, const oscairn_voice_budget_context_t* contexts, uint32_t count)
        {
            self(p).limits.set(limit, contexts, count); // audio thread, never during process: copy it
        },
        [](const clap_plugin_t* p, double* voiceCost) -> uint32_t
        {
            auto& s = self(p);
            if(voiceCost) *voiceCost = 12.0; // a guess: twelve microseconds a voice a block
            uint32_t n = 0;
            for(const auto& v : s.voices) n += v.active;
            return n;
        },
        [](const clap_plugin_t* p, oscairn_voice_budget_note_t* out, uint32_t capacity) -> uint32_t
        {
            auto& s = self(p);
            oscairn::voices::Report<pool> report;
            for(const auto& v : s.voices)
                if(v.active) report.add(v.noteId, 0, v.channel, v.key, loudness(v));
            return report.write(out, capacity);
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
            auto& s = self(pl);
            s.rate = rate;
            s.chokeFrames = std::max<uint32_t>(1, uint32_t(rate * chokeSeconds + .5));
            return true;
        };
        p.deactivate = [](const clap_plugin_t*) {};
        p.start_processing = [](const clap_plugin_t*) { return true; };
        p.stop_processing = [](const clap_plugin_t*) {};
        p.reset = [](const clap_plugin_t* pl)
        {
            auto& s = self(pl);
            for(auto& v : s.voices) v = Voice{};
        };
        p.on_main_thread = [](const clap_plugin_t*) {};
        p.process = [](const clap_plugin_t* pl, const clap_process_t* block) -> clap_process_status
        {
            auto& s = self(pl);
            if(block->audio_outputs_count < 1 || block->audio_outputs[0].channel_count < 2) return CLAP_PROCESS_ERROR;
            params.flush(pl, block->in_events, block->out_events);
            keepWithinLimit(s); // set_limits takes effect at the block's start
            uint32_t next = 0;
            const uint32_t count = block->in_events->size(block->in_events);
            float* out[2]{block->audio_outputs[0].data32[0], block->audio_outputs[0].data32[1]};
            for(uint32_t i = 0; i < block->frames_count; ++i)
            {
                for(; next < count; ++next)
                {
                    const auto* h = block->in_events->get(block->in_events, next);
                    if(h->time > i) break;
                    if(h->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
                    if(h->type == CLAP_EVENT_NOTE_ON) startVoice(s, *reinterpret_cast<const clap_event_note_t*>(h));
                    else if(h->type == CLAP_EVENT_NOTE_OFF || h->type == CLAP_EVENT_NOTE_CHOKE)
                    {
                        const auto& n = *reinterpret_cast<const clap_event_note_t*>(h);
                        for(auto& v : s.voices)
                            if(addressed(v, n.note_id, n.channel, n.key))
                            {
                                v.held = false;
                                if(h->type == CLAP_EVENT_NOTE_CHOKE && !v.chokeLength) v.chokeLength = v.chokeLeft = s.chokeFrames;
                            }
                    }
                    else if(h->type == CLAP_EVENT_NOTE_EXPRESSION)
                    {
                        const auto& e = *reinterpret_cast<const clap_event_note_expression_t*>(h);
                        if(e.expression_id != CLAP_NOTE_EXPRESSION_VOLUME || !std::isfinite(e.value)) continue;
                        for(auto& v : s.voices)
                            if(addressed(v, e.note_id, e.channel, e.key)) v.volume = std::clamp(e.value, 0.0, 4.0);
                    }
                }
                double sum = 0;
                for(auto& v : s.voices)
                {
                    if(!v.active) continue;
                    const double step = 1.0 / (0.005 * s.rate); // a 5 ms attack and release
                    if(v.chokeLength) { if(--v.chokeLeft == 0) { v = Voice{}; continue; } } // its fade runs down, linear
                    else if(v.held) v.level = std::min(1.0, v.level + step);
                    else v.level -= step;
                    if(v.level <= 0) { v = Voice{}; continue; }
                    sum += std::sin(v.phase) * loudness(v);
                    v.phase = std::fmod(v.phase + 440.0 * 2 * M_PI * (1 + v.key / 128.0) / s.rate, 2 * M_PI);
                }
                const auto sample = float(sum * .25 * s.level.load());
                out[0][i] = out[1][i] = sample;
            }
            const bool sounding = std::any_of(s.voices.begin(), s.voices.end(), [](const Voice& v) { return v.active; });
            return sounding ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_SLEEP;
        };
        p.get_extension = [](const clap_plugin_t*, const char* id) -> const void*
        {
            if(!std::strcmp(id, OSCAIRN_EXT_VOICE_BUDGET)) return &budget;
            if(!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &audio;
            if(!std::strcmp(id, CLAP_EXT_NOTE_PORTS)) return &notes;
            if(!std::strcmp(id, CLAP_EXT_PARAMS)) return &params;
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
