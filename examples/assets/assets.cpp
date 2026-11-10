// SPDX-License-Identifier: MIT
//
// oscairn.assets + oscairn.asset-dependencies: a gain whose curve comes from a wavetable file
// in a sound library. The state holds an asset reference — a library id, a path inside it and
// the file's SHA-256 — never a local path. On load the host's resolve gives a real file for it;
// the reference is listed in oscairn.asset-dependencies so a Performance records it as a
// dependency and can be checked or moved between machines. This one marks it optional: with the
// library missing the resolve fails, the plugin keeps its built-in curve, and the host shows a
// notice instead of refusing.
// See docs/rfcs/0009-sound-assets-and-dependencies.md.
#include <clap/clap.h>
#include <oscairn-clap/assets.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
    const char* const features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DISTORTION, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
    const clap_plugin_descriptor_t descriptor{
        CLAP_VERSION_INIT, "org.oscairn.example.assets", "Oscairn Example Assets", "Oscairn", "", "", "", "0.1.0",
        "A wavetable curve loaded through a sound-asset reference (oscairn.assets)", features};

    struct Plugin
    {
        clap_plugin_t api{};
        const clap_host_t* host = nullptr;
        std::atomic<double> amount{.5};
        oscairn_asset_ref_t curve{}; // its wavetable, as a reference; empty: the built-in curve
        double table[8]{};
        Plugin() { for(int i = 0; i < 8; ++i) table[i] = 1.0 / (1 + i); } // the fallback shape
    };
    Plugin& self(const clap_plugin_t* p) { return *static_cast<Plugin*>(p->plugin_data); }

    // Ask the host for the file. On success read its first eight doubles as the curve; a host
    // without the extension, or a library that is not installed, leaves the fallback in place.
    void resolveCurve(Plugin& s)
    {
        if(!s.curve.library[0] || !s.curve.path[0]) return;
        const auto* assets = static_cast<const oscairn_host_assets_t*>(s.host->get_extension(s.host, OSCAIRN_EXT_ASSETS));
        if(!assets || !assets->resolve) return;
        char path[1024], message[256];
        oscairn_asset_ref_t ref = s.curve; // resolve fills sha256 when it was empty
        if(!assets->resolve(s.host, &ref, path, sizeof path, message, sizeof message)) return;
        s.curve = ref; // keep the hash the host verified, so dependencies report it
        if(FILE* file = std::fopen(path, "rb"))
        {
            double read[8]{};
            if(std::fread(read, sizeof read, 1, file) == 1)
            {
                for(int i = 0; i < 8; ++i) s.table[i] = read[i];
            }
            std::fclose(file);
        }
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
            std::strcpy(out->name, "Amount");
            return true;
        },
        [](const clap_plugin_t* p, clap_id id, double* v) -> bool
        {
            if(id != 1) return false;
            *v = self(p).amount;
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
                    if(e->param_id == 1 && std::isfinite(e->value)) self(p).amount = std::clamp(e->value, 0.0, 1.0);
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

    // The reference is the state: library, path, and the hash when it is known.
    const clap_plugin_state_t state{
        [](const clap_plugin_t* p, const clap_ostream_t* stream) -> bool
        {
            const auto& s = self(p);
            const std::string data = std::string("oscairn-example-assets-1 ") + std::to_string(s.amount.load()) + '\0' + s.curve.library + '\0' +
                                     s.curve.path + '\0' + s.curve.sha256 + '\0';
            return stream->write(stream, data.data(), uint64_t(data.size())) == int64_t(data.size());
        },
        [](const clap_plugin_t* p, const clap_istream_t* stream) -> bool
        {
            auto& s = self(p);
            std::string data;
            char chunk[256];
            for(int64_t got; (got = stream->read(stream, chunk, sizeof chunk)) > 0;)
            {
                data.append(chunk, size_t(got));
                if(data.size() > 4096) return false;
            }
            char header[32]{};
            double amount = 0;
            s.curve = {};
            int used = 0;
            if(std::sscanf(data.c_str(), "%31s %lf%n", header, &amount, &used) != 2 || std::strcmp(header, "oscairn-example-assets-1") ||
               !std::isfinite(amount) || amount < 0 || amount > 1)
                return false;
            // The rest is three NUL-separated fields: library, path, hash.
            const char* at = data.data() + used + 1; // past the NUL after the amount
            const char* end = data.data() + data.size();
            const std::pair<char*, size_t> fields[]{{s.curve.library, sizeof s.curve.library}, {s.curve.path, sizeof s.curve.path},
                                                    {s.curve.sha256, sizeof s.curve.sha256}};
            for(const auto& [field, capacity] : fields)
            {
                if(at >= end) return false;
                const size_t n = std::strlen(at);
                if(n >= capacity || n >= size_t(end - at)) return false;
                std::memcpy(field, at, n + 1);
                at += n + 1;
            }
            s.amount = amount;
            resolveCurve(s); // at load, like every host that restores a state
            return true;
        }};

    // Every asset the current state uses, so a Performance can record and check them.
    const oscairn_plugin_asset_dependencies_t dependencies{
        [](const clap_plugin_t* p) -> uint32_t { return self(p).curve.library[0] ? 1u : 0u; },
        [](const clap_plugin_t* p, uint32_t index, oscairn_asset_ref_t* ref) -> bool
        {
            if(index) return false;
            *ref = self(p).curve;
            return true;
        },
        // Draft 2: it can do without this one — the built-in curve stays.
        [](const clap_plugin_t*, uint32_t index) -> bool { return index == 0; }};

    const clap_plugin_t* create(const clap_host_t* host)
    {
        auto* s = new Plugin;
        s->host = host;
        auto& p = s->api;
        p.desc = &descriptor;
        p.plugin_data = s;
        p.init = [](const clap_plugin_t*) { return true; };
        p.destroy = [](const clap_plugin_t* pl) { delete &self(pl); };
        p.activate = [](const clap_plugin_t*, double rate, uint32_t, uint32_t)
        { return std::isfinite(rate) && rate >= 1; };
        p.deactivate = [](const clap_plugin_t*) {};
        p.start_processing = [](const clap_plugin_t*) { return true; };
        p.stop_processing = [](const clap_plugin_t*) {};
        p.reset = [](const clap_plugin_t*) {};
        p.on_main_thread = [](const clap_plugin_t*) {};
        p.process = [](const clap_plugin_t* pl, const clap_process_t* block) -> clap_process_status
        {
            auto& s = self(pl);
            if(block->audio_outputs_count < 1 || block->audio_outputs[0].channel_count < 2) return CLAP_PROCESS_ERROR;
            params.flush(pl, block->in_events, block->out_events);
            const double amount = s.amount;
            const bool input = block->audio_inputs_count > 0 && block->audio_inputs[0].channel_count >= 2;
            for(uint32_t c = 0; c < 2; ++c)
            {
                const float* in = input ? block->audio_inputs[0].data32[c] : nullptr;
                float* out = block->audio_outputs[0].data32[c];
                for(uint32_t f = 0; f < block->frames_count; ++f)
                {
                    const double dry = in ? in[f] : 0.0;
                    // Wave-shape with the curve: index it by the signal's magnitude.
                    const size_t slot = std::min(size_t(7), size_t(std::abs(dry) * 8));
                    const double shaped = dry * (1 + s.table[slot] * amount);
                    out[f] = float(dry * (1 - amount) + std::tanh(shaped) * amount);
                }
            }
            return CLAP_PROCESS_CONTINUE;
        };
        p.get_extension = [](const clap_plugin_t*, const char* id) -> const void*
        {
            if(!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &audio;
            if(!std::strcmp(id, CLAP_EXT_PARAMS)) return &params;
            if(!std::strcmp(id, CLAP_EXT_STATE)) return &state;
            if(!std::strcmp(id, OSCAIRN_EXT_ASSET_DEPENDENCIES)) return &dependencies;
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
