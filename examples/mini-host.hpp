// SPDX-License-Identifier: MIT
//
// A miniature CLAP host for the example self-tests: dlopen a .clap, create one instance, and
// drive an extension directly. Answers event-registry, harmony and assets host extensions;
// collects output events; streams state to and from memory.
#pragma once
#include <clap/clap.h>
#include <oscairn-clap/assets.h>
#include <oscairn-clap/beat.h>
#include <oscairn-clap/effect-contexts.h>
#include <oscairn-clap/harmony.h>
#include <oscairn-clap/memory.h>
#include <oscairn-clap/param-target.h>
#include <oscairn-clap/sound-contexts.h>
#include <oscairn-clap/ui.h>
#include <oscairn-clap/voice-budget.h>

#include <dlfcn.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

inline int failures = 0;
inline void check(bool ok, const std::string& what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what.c_str());
    std::fflush(stdout); // so a crash still shows what passed
    if(!ok) ++failures;
}

// One host-side event list: events are raw byte blobs kept in order.
struct EventList
{
    std::vector<std::vector<uint8_t>> events;
    clap_input_events_t input{};
    clap_output_events_t output{};
    EventList()
    {
        input = {this,
                 [](const clap_input_events_t* l) -> uint32_t { return uint32_t(static_cast<EventList*>(l->ctx)->events.size()); },
                 [](const clap_input_events_t* l, uint32_t i) -> const clap_event_header_t*
                 { return reinterpret_cast<const clap_event_header_t*>(static_cast<EventList*>(l->ctx)->events[i].data()); }};
        output = {this,
                  [](const clap_output_events_t* l, const clap_event_header_t* e) -> bool
                  {
                      auto* self = static_cast<EventList*>(l->ctx);
                      self->events.emplace_back(reinterpret_cast<const uint8_t*>(e), reinterpret_cast<const uint8_t*>(e) + e->size);
                      return true;
                  }};
    }
    template<class E>
    void add(const E& e)
    {
        const uint8_t* begin = reinterpret_cast<const uint8_t*>(&e);
        events.emplace_back(begin, begin + e.header.size);
    }
    // Every event in a space, oldest first.
    std::vector<const clap_event_header_t*> in(uint16_t space) const
    {
        std::vector<const clap_event_header_t*> out;
        for(const auto& e : events)
            if(const auto* h = reinterpret_cast<const clap_event_header_t*>(e.data()); h->space_id == space) out.push_back(h);
        return out;
    }
};

struct MiniHost
{
    clap_host_t host{};
    uint16_t nextSpace = 1; // the first space a plugin queries gets id 1
    oscairn_harmony_t harmony{};
    std::string assetRoot = "/usr/share/oscairn/library";

    MiniHost()
    {
        host = {};
        host.clap_version = CLAP_VERSION_INIT;
        host.host_data = this;
        host.name = "Example mini host";
        host.vendor = "Oscairn";
        host.url = "";
        host.version = "0";
        host.get_extension = [](const clap_host_t* h, const char* id) -> const void* { return self(h).extension(id); };
    }
    static MiniHost& self(const clap_host_t* h) { return *static_cast<MiniHost*>(h->host_data); }

    const void* extension(const char* id)
    {
        if(!std::strcmp(id, CLAP_EXT_EVENT_REGISTRY))
        {
            static const clap_host_event_registry_t registry{
                [](const clap_host_t* h, const char* space, uint16_t* id) -> bool
                {
                    if(!space || !id) return false;
                    *id = self(h).nextSpace++;
                    return true;
                }};
            return &registry;
        }
        if(!std::strcmp(id, OSCAIRN_EXT_HARMONY))
        {
            static const oscairn_host_harmony_t harmonyExt{
                [](const clap_host_t* h, oscairn_harmony_t* out) -> bool
                {
                    *out = self(h).harmony;
                    return true;
                }};
            return &harmonyExt;
        }
        if(!std::strcmp(id, OSCAIRN_EXT_ASSETS))
        {
            static const oscairn_host_assets_t assets{
                resolve, libraryRoot, create};
            return &assets;
        }
        return nullptr;
    }
    static bool resolve(const clap_host_t* h, oscairn_asset_ref_t* ref, char* path, uint32_t capacity, char* message, uint32_t messageCapacity)
    {
        if(!ref || !ref->library[0] || !ref->path[0])
        {
            if(message && messageCapacity) std::snprintf(message, messageCapacity, "incomplete reference");
            return false;
        }
        const int n = std::snprintf(path, capacity, "%s/%s", self(h).assetRoot.c_str(), ref->path);
        if(!ref->sha256[0]) std::strcpy(ref->sha256, "0000000000000000000000000000000000000000000000000000000000000000");
        return n > 0 && uint32_t(n) < capacity;
    }
    // `owner` is the table's source: here always the clap_host_t (assets.h).
    static bool libraryRoot(const void* owner, const char* library, const char*, char* path, uint32_t capacity)
    {
        if(!owner || !library) return false;
        const int n = std::snprintf(path, capacity, "%s/%s", self(static_cast<const clap_host_t*>(owner)).assetRoot.c_str(), library);
        return n > 0 && uint32_t(n) < capacity;
    }
    static bool create(const clap_host_t*, const char* file, oscairn_asset_ref_t* ref, char* message, uint32_t messageCapacity)
    {
        if(!file || !file[0])
        {
            if(message && messageCapacity) std::snprintf(message, messageCapacity, "no file");
            return false;
        }
        std::snprintf(ref->library, sizeof ref->library, "user");
        const char* slash = std::strrchr(file, '/');
        std::snprintf(ref->path, sizeof ref->path, "examples/%s", slash ? slash + 1 : file);
        std::strcpy(ref->sha256, "0000000000000000000000000000000000000000000000000000000000000000");
        return true;
    }
};

// One process() call with `frames` frames and `events` pending, on any number of ports.
inline clap_process_status processBlock(const clap_plugin_t* plugin, uint32_t frames, EventList& events, const clap_audio_buffer_t* outputs,
                                        uint32_t outputCount, const clap_audio_buffer_t* inputs = nullptr, uint32_t inputCount = 0)
{
    clap_process_t p{};
    p.frames_count = frames;
    p.audio_inputs = inputs;
    p.audio_outputs = const_cast<clap_audio_buffer_t*>(outputs);
    p.audio_inputs_count = inputCount;
    p.audio_outputs_count = outputCount;
    p.in_events = &events.input;
    p.out_events = &events.output;
    return plugin->process(plugin, &p);
}

// The same with one stereo output port and, optionally, one stereo input port.
inline clap_process_status processStereo(const clap_plugin_t* plugin, uint32_t frames, EventList& events, float* const* outChannels = nullptr,
                                         const float* const* inChannels = nullptr)
{
    clap_audio_buffer_t out{}, in{};
    out.channel_count = 2;
    out.data32 = const_cast<float**>(outChannels);
    in.channel_count = 2;
    in.data32 = const_cast<float**>(inChannels);
    return processBlock(plugin, frames, events, &out, outChannels ? 1 : 0, &in, inChannels ? 1 : 0);
}

// One loaded plugin: the entry, its factory and one created instance.
struct Plugin
{
    void* handle = nullptr;
    const clap_plugin_entry_t* entry = nullptr;
    const clap_plugin_factory_t* factory = nullptr;
    const clap_plugin_t* plugin = nullptr;
    MiniHost host;
    bool active = false;

    Plugin(const char* path, const char* id)
    {
        handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        check(handle != nullptr, std::string("open ") + path + (handle ? "" : ": " + std::string(dlerror())));
        if(!handle) std::exit(1);
        entry = static_cast<const clap_plugin_entry_t*>(dlsym(handle, "clap_entry"));
        check(entry && clap_version_is_compatible(entry->clap_version), "the entry is a compatible CLAP entry");
        if(!entry) std::exit(1);
        check(entry->init(path), "the entry initializes");
        factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        check(factory != nullptr, "the factory is offered");
        if(!factory) std::exit(1);
        plugin = factory->create_plugin(factory, &host.host, id);
        check(plugin != nullptr, "the plugin is created");
        if(!plugin) std::exit(1);
        check(plugin->init(plugin), "the plugin initializes");
    }
    ~Plugin()
    {
        deactivate();
        plugin->destroy(plugin);
        entry->deinit();
        dlclose(handle);
    }
    template<class T>
    const T* extension(const char* id) const
    {
        return static_cast<const T*>(plugin->get_extension(plugin, id));
    }
    bool activate(double rate = 48000, uint32_t block = 256)
    {
        active = plugin->activate(plugin, rate, block, block);
        return active && plugin->start_processing(plugin);
    }
    // CLAP's order: stop processing, then deactivate; the destructor does it when still active.
    void deactivate()
    {
        if(!active) return;
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        active = false;
    }
    clap_process_status process(uint32_t frames, EventList& events, float* const* outChannels = nullptr, const float* const* inChannels = nullptr)
    {
        return processStereo(plugin, frames, events, outChannels, inChannels);
    }
};

// The largest magnitude among `frames` samples of one channel.
inline double peak(const float* channel, uint32_t frames)
{
    double p = 0;
    for(uint32_t f = 0; f < frames; ++f) p = std::max(p, double(std::abs(channel[f])));
    return p;
}

inline void noteEvent(clap_event_note_t& n, uint32_t time, uint16_t type, int32_t id, int16_t channel, int16_t key, double velocity = .8)
{
    n = {};
    n.header = {sizeof(n), time, CLAP_CORE_EVENT_SPACE_ID, type, 0};
    n.note_id = id;
    n.port_index = 0;
    n.channel = channel;
    n.key = key;
    n.velocity = velocity;
}
inline void paramEvent(clap_event_param_value_t& e, uint32_t time, clap_id id, double value)
{
    e = {};
    e.header = {sizeof(e), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
    e.param_id = id;
    e.note_id = e.port_index = e.channel = e.key = -1;
    e.value = value;
}

// State to/from memory through clap_istream_t / clap_ostream_t (their user pointer is `ctx`).
struct OutStream
{
    std::string data;
    const clap_ostream_t stream{
        this,
        [](const clap_ostream_t* s, const void* buffer, uint64_t size) -> int64_t
        {
            auto* self = static_cast<OutStream*>(s->ctx);
            self->data.append(static_cast<const char*>(buffer), size_t(size));
            return int64_t(size);
        }};
};
struct InStream
{
    std::string data;
    size_t at = 0;
    const clap_istream_t stream{
        this,
        [](const clap_istream_t* s, void* buffer, uint64_t size) -> int64_t
        {
            auto* self = static_cast<InStream*>(s->ctx);
            const size_t n = std::min(size_t(size), self->data.size() - self->at);
            std::memcpy(buffer, self->data.data() + self->at, n);
            self->at += n;
            return int64_t(n);
        }};
};

inline int finish(const char* what)
{
    std::printf("%s: %s\n", what, failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
