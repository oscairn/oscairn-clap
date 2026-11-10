// SPDX-License-Identifier: MIT
//
// Parts sharing one instance (oscairn.sound-snapshots draft 4 + oscairn.sound-contexts + an
// output-bus parameter, RFC 0021): several of Oscairn's parts play one instance of this plugin,
// each with its own sound, its own MIDI channel and its own output port. The host puts a part's
// sound into an instance with room, sends its notes and parameter edits on its channel (a
// parameter event with a channel edits the sound that channel selects) and takes its audio from
// the port its output-bus parameter names. The sound is one sine, so "a sound of its own" is a
// level, a pitch and a bus: three parameters. Everything beyond that is the SDK's engine.
// See docs/fallback-matrix.md "Parts sharing one instance" and plugins/basic-synth.
#include <clap/clap.h>
#include <oscairn-clap/sound-contexts-plugin.hpp>
#include <oscairn-clap/ui.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <istream>
#include <ostream>

namespace
{
    constexpr uint32_t outputPorts = 4; // a main stereo port plus three more, one per possible bus

    // id = index + 1. "Output bus" is the parameter the host shares parts with: stepped, writable,
    // 0 to the last port index, its oscairn.ui role OSCAIRN_ROLE_OUTPUT_BUS.
    struct Parameter
    {
        const char* name;
        double maximum, initial;
        const char* unit;
        bool stepped;
        uint32_t widget, role;
    };
    constexpr std::array<Parameter, 3> parameters{{
        {"Level", 1, .5, "%", false, OSCAIRN_WIDGET_KNOB, OSCAIRN_ROLE_LEVEL},
        {"Pitch", 24, 0, "st", false, OSCAIRN_WIDGET_KNOB, OSCAIRN_ROLE_NONE},
        {"Output bus", double(outputPorts - 1), 0, "", true, OSCAIRN_WIDGET_LIST, OSCAIRN_ROLE_OUTPUT_BUS},
    }};
    constexpr size_t Count = parameters.size();
    constexpr uint32_t busIndex = 2; // the Output bus parameter's slot in a patch
    constexpr std::array<const char*, outputPorts> busNames{"Main", "Bus 1", "Bus 2", "Bus 3"};
    constexpr auto defaults = []
    {
        std::array<double, Count> values{};
        for(size_t i = 0; i < values.size(); ++i) values[i] = parameters[i].initial;
        return values;
    }();

    const char* const features[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
    const clap_plugin_descriptor_t descriptor{
        CLAP_VERSION_INIT, "org.oscairn.example.instance-sharing", "Oscairn Example Instance Sharing", "Oscairn", "", "", "", "0.1.0",
        "Several parts playing one instance, each its sound, channel and output port", features};

    struct Patch
    {
        std::array<double, Count> values = defaults;
    };
    struct Voice : oscairn::contexts::VoiceBase
    {
        double phase = 0, level = 0, increment = 0;
        uint32_t bus = 0; // the port it started on; a later bus edit does not move a held note
    };

    struct Synth : oscairn::contexts::Engine<Synth, Voice, Patch, 4, 16>
    {
        double rate = 48000;
        std::array<std::atomic<double>, Count> values{}; // the main thread reads these while audio runs
        static constexpr const char* stateVersion = "oscairn-example-sharing-1";
        static constexpr size_t maxStateBytes = 4096;
        Synth() { for(size_t i = 0; i < Count; ++i) values[i] = defaults[i]; }

        bool accepts(const Patch&, const clap_event_note_t&) { return true; }
        void start(Voice& v, Context& c)
        {
            v.phase = v.level = 0;
            v.increment = 440.0 * std::pow(2.0, (v.key - 60 + c.patch.values[1]) / 12.0) / rate;
            v.bus = std::min<uint32_t>(uint32_t(c.patch.values[busIndex] + .5), outputPorts - 1);
        }
        void release(Voice& v) { v.held = false; }
        void render(const clap_process_t*, uint32_t from, uint32_t to);
        void applyParam(const clap_event_header_t*);
        void selectedGlobally(Context& next, const clap_output_events_t* out);
        static uint32_t prepare(const clap_plugin_t*, const char* name, const oscairn_sound_parameter_t*, uint32_t,
                               const char* patch, const oscairn_asset_ref_t*, char*, uint32_t);
        bool writePatch(std::ostream& out, const Patch& patch)
        {
            for(double value : patch.values) out << ' ' << value;
            return true;
        }
        bool readPatch(std::istream& in, Patch& patch)
        {
            std::array<double, Count> read{};
            for(size_t i = 0; i < Count; ++i)
            {
                double value = 0;
                const auto stepped = parameters[i].stepped;
                if(!(in >> value) || !std::isfinite(value) || value < 0 || value > parameters[i].maximum ||
                   (stepped && value != std::round(value)))
                    return false;
                read[i] = value;
            }
            patch.values = read;
            return true;
        }
        // draft 4: the sound of a part saved on its own plays on the main port, wherever it shared from.
        void alone(Patch& patch) { patch.values[busIndex] = 0; }
        uint32_t patchValues(const Patch& patch, oscairn_sound_parameter_t* out, uint32_t capacity)
        {
            for(uint32_t i = 0; i < Count && i < capacity; ++i) out[i] = {i + 1, patch.values[i]};
            return uint32_t(Count);
        }
        // The instance's own values follow the sound every channel now selects.
        void restored() { for(size_t i = 0; i < Count; ++i) values[i] = contexts[selected[0]].patch.values[i]; }
    };
    Synth& self(const clap_plugin_t* p) { return *static_cast<Synth*>(p->plugin_data); }

    void Synth::render(const clap_process_t* block, uint32_t from, uint32_t to)
    {
        auto& s = *this;
        std::array<double, 4> level{}; // per context slot
        std::array<bool, 4> copied{};
        std::array<float* const*, 4> ring{}; // per context: its ring-out channels, or none
        for(const auto& v : voices)
            if(v.active && !copied[v.context])
            {
                copied[v.context] = true;
                level[v.context] = contexts[v.context].patch.values[0];
                ring[v.context] = s.retiring(v.context);
            }
        for(uint32_t i = from; i < to; ++i)
        {
            s.eventTime = i;
            std::array<double, outputPorts> sum{}; // one per output port: each part's sound on its own bus
            for(auto& v : voices)
            {
                if(!v.active) continue;
                // A 5 ms attack and release; a voice never outlives its own fade.
                const double step = 1.0 / (0.005 * rate);
                if(v.held) v.level = std::min(1.0, v.level + step);
                else v.level -= step;
                if(v.level <= 0) { finishVoice(v); continue; }
                const double sample = std::sin(v.phase) * v.level * v.velocity * level[v.context] * amplitude(v, i - from) * .3;
                // A retiring sound's main output goes to its two ring-out channels instead of port 0.
                if(v.bus == 0 && ring[v.context]) ring[v.context][0][i] += float(sample), ring[v.context][1][i] += float(sample);
                else sum[v.bus] += sample;
                v.phase = std::fmod(v.phase + v.increment * 2 * M_PI, 2 * M_PI);
            }
            for(uint32_t port = 0; port < outputPorts && port < block->audio_outputs_count; ++port)
                if(auto& out = block->audio_outputs[port]; out.channel_count >= 2 && out.data32 && out.data32[0] && out.data32[1])
                    out.data32[0][i] = out.data32[1][i] = float(sum[port]);
        }
    }

    // A parameter edit on a channel lands on the sound that channel selects — the host gives each
    // sharing part its own channel, so this is what routes a part to its bus and its sound.
    void Synth::applyParam(const clap_event_header_t* header)
    {
        auto& s = *this;
        if(header->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
        if(header->type != CLAP_EVENT_PARAM_VALUE) return;
        const auto& e = *reinterpret_cast<const clap_event_param_value_t*>(header);
        const auto id = e.param_id;
        if(id < 1 || id > Count || !std::isfinite(e.value) || e.channel < -1 || e.channel > 15) return;
        auto value = std::clamp(e.value, 0.0, parameters[id - 1].maximum);
        if(parameters[id - 1].stepped) value = std::round(value);
        s.values[id - 1] = value;
        for(int ch = 0; ch < 16; ++ch)
            if(e.channel < 0 || ch == e.channel) contexts[s.selected[ch]].patch.values[id - 1] = value;
    }

    void Synth::selectedGlobally(Context& next, const clap_output_events_t* output)
    {
        for(size_t i = 0; i < Count; ++i)
        {
            values[i] = next.patch.values[i];
            clap_event_param_value_t e{};
            e.header = {sizeof(e), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
            e.param_id = clap_id(i + 1);
            e.note_id = e.port_index = e.channel = e.key = -1;
            e.value = next.patch.values[i];
            if(output) output->try_push(output, &e.header);
        }
    }

    uint32_t Synth::prepare(const clap_plugin_t* p, const char* name, const oscairn_sound_parameter_t* supplied, uint32_t count,
                           const char* patch, const oscairn_asset_ref_t* base, char* message, uint32_t capacity)
    {
        if(patch || base) { if(message && capacity) std::snprintf(message, capacity, "this plugin takes no patch text"); return CLAP_INVALID_ID; }
        if(!name || std::strlen(name) > 63) return CLAP_INVALID_ID;
        Patch values;
        for(uint32_t i = 0; i < count; ++i)
        {
            const auto& v = supplied[i];
            const auto& definition = v.id >= 1 && v.id <= Count ? parameters[v.id - 1] : Parameter{"", -1, 0, "", false, 0, 0};
            if(v.id < 1 || v.id > Count || !std::isfinite(v.value) || v.value < 0 || v.value > definition.maximum ||
               (definition.stepped && v.value != std::round(v.value)))
            {
                if(message && capacity) std::snprintf(message, capacity, "value %u is not a parameter of this plugin", v.id);
                return CLAP_INVALID_ID;
            }
            values.values[v.id - 1] = v.value;
        }
        auto& s = self(p);
        auto* slot = s.freeSlot();
        if(!slot) { if(message && capacity) std::snprintf(message, capacity, "no free sound context"); return CLAP_INVALID_ID; }
        slot->patch = values;
        return s.publish(*slot, name);
    }

    const clap_plugin_params_t params{
        [](const clap_plugin_t*) -> uint32_t { return uint32_t(Count); },
        [](const clap_plugin_t*, uint32_t i, clap_param_info_t* out) -> bool
        {
            if(i >= Count) return false;
            const auto& p = parameters[i];
            *out = {};
            out->id = i + 1;
            out->flags = CLAP_PARAM_IS_AUTOMATABLE | (p.stepped ? CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM : CLAP_PARAM_IS_MODULATABLE);
            out->min_value = 0;
            out->max_value = p.maximum;
            out->default_value = p.initial;
            std::snprintf(out->name, sizeof out->name, "%s", p.name);
            return true;
        },
        [](const clap_plugin_t* p, clap_id id, double* v) -> bool
        {
            if(id < 1 || id > Count) return false;
            *v = self(p).values[id - 1];
            return true;
        },
        [](const clap_plugin_t*, clap_id id, double value, char* text, uint32_t size) -> bool
        {
            if(id < 1 || id > Count || !std::isfinite(value)) return false;
            value = std::clamp(value, 0.0, parameters[id - 1].maximum);
            if(id == busIndex + 1) std::snprintf(text, size, "%s", busNames[size_t(value + .5)]);
            else if(id == 1) std::snprintf(text, size, "%.0f%%", value * 100);
            else std::snprintf(text, size, "%.0f st", value);
            return true;
        },
        [](const clap_plugin_t*, clap_id id, const char* text, double* value) -> bool
        {
            if(id < 1 || id > Count) return false;
            if(id == busIndex + 1)
                for(size_t i = 0; i < busNames.size(); ++i)
                    if(!std::strcmp(text, busNames[i])) { *value = double(i); return true; }
            char* end = nullptr;
            double parsed = std::strtod(text, &end);
            if(end == text || !std::isfinite(parsed)) return false;
            while(*end == ' ') ++end;
            if(*end)
            {
                if(std::strcmp(end, parameters[id - 1].unit) != 0) return false;
                if(id == 1) parsed /= 100;
            }
            if(parsed < 0 || parsed > parameters[id - 1].maximum || (parameters[id - 1].stepped && parsed != std::round(parsed))) return false;
            *value = parsed;
            return true;
        },
        [](const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t* out)
        {
            self(p).applySelections(out);
            for(uint32_t i = 0; in && i < in->size(in); ++i) self(p).applyParam(in->get(in, i));
        }};

    // Requirement 3 and 5: more than one audio output port, and no audio input.
    const clap_plugin_audio_ports_t audio{
        [](const clap_plugin_t*, bool input) -> uint32_t { return input ? 0u : outputPorts; },
        [](const clap_plugin_t*, uint32_t i, bool input, clap_audio_port_info_t* out) -> bool
        {
            if(i >= outputPorts || input) return false;
            *out = {};
            out->id = i;
            out->flags = i == 0 ? CLAP_AUDIO_PORT_IS_MAIN : 0;
            out->channel_count = 2;
            out->port_type = CLAP_PORT_STEREO;
            out->in_place_pair = CLAP_INVALID_ID;
            std::snprintf(out->name, sizeof out->name, "%s", busNames[i]);
            return true;
        }};
    // Requirement 6: at least one note input, no note output.
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

    // Requirement 4: the Output bus parameter's hint, so the host finds it by role. Declaring any
    // role turns Oscairn's name guessing off for the rest, so Level names its role too.
    const oscairn_plugin_ui_t ui{
        [](const clap_plugin_t*, clap_id id, oscairn_param_hint_t* hint) -> bool
        {
            if(id < 1 || id > Count) return false;
            const auto& p = parameters[id - 1];
            *hint = {p.widget, id == 1 ? 1u : 0u, CLAP_INVALID_ID, 0, p.role, 0, OSCAIRN_FIELD_NONE, 0};
            return true;
        },
        nullptr, nullptr};

    const clap_plugin_state_t snapshot{Synth::saveSnapshot, Synth::loadSnapshot};
    // Requirement 1: native sound contexts, with their state.
    const oscairn_plugin_sound_contexts_t soundContexts{Synth::capacity, Synth::get, Synth::prepare, Synth::select, Synth::discard, &snapshot, nullptr};
    // Requirement 2: draft 4's save_sound and sound_values, which the SDK builds from alone/patchValues.
    const oscairn_plugin_sound_snapshots_t soundSnapshots{Synth::prepareSnapshot, Synth::setRingOut, Synth::chokeRetiring, Synth::saveSound, Synth::soundValues};

    const clap_plugin_t* create()
    {
        auto* s = new Synth;
        s->plugin.desc = &descriptor;
        s->plugin.plugin_data = s;
        s->plugin.init = [](const clap_plugin_t*) { return true; };
        s->plugin.destroy = [](const clap_plugin_t* p) { delete &self(p); };
        s->plugin.activate = [](const clap_plugin_t* p, double rate, uint32_t, uint32_t)
        {
            if(!std::isfinite(rate) || rate < 1) return false;
            auto& s = self(p);
            s.rate = rate;
            s.setSampleRate(rate);
            s.resetVoices();
            return true;
        };
        s->plugin.deactivate = [](const clap_plugin_t*) {};
        s->plugin.start_processing = [](const clap_plugin_t*) { return true; };
        s->plugin.stop_processing = [](const clap_plugin_t*) {};
        s->plugin.reset = [](const clap_plugin_t* p) { self(p).resetVoices(); };
        s->plugin.process = Synth::process;
        s->plugin.on_main_thread = [](const clap_plugin_t*) {};
        s->plugin.get_extension = [](const clap_plugin_t*, const char* id) -> const void*
        {
            if(!std::strcmp(id, OSCAIRN_EXT_SOUND_CONTEXTS)) return &soundContexts;
            if(!std::strcmp(id, OSCAIRN_EXT_SOUND_SNAPSHOTS)) return &soundSnapshots;
            if(!std::strcmp(id, OSCAIRN_EXT_UI)) return &ui;
            if(!std::strcmp(id, CLAP_EXT_STATE)) return &snapshot;
            if(!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &audio;
            if(!std::strcmp(id, CLAP_EXT_NOTE_PORTS)) return &notes;
            if(!std::strcmp(id, CLAP_EXT_PARAMS)) return &params;
            return nullptr;
        };
        return &s->plugin;
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
