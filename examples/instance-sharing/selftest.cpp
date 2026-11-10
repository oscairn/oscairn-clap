// SPDX-License-Identifier: MIT
// Parts sharing one instance: check every requirement Oscairn lists in
// docs/fallback-matrix.md ("Parts sharing one instance") against the built plugin — the
// extensions it offers, save_sound/sound_values, the port layout, the output-bus parameter's
// flags/range/role, and that a sound on bus 2 renders on port 2 and not on the main port.
#include "../mini-host.hpp"

namespace
{
    constexpr uint32_t ports = 4;

    // The plugin has four stereo output ports; mini-host's process() drives one, so the bus
    // checks build their own clap_process_t with all of them.
    struct Ports
    {
        std::array<std::array<float, 256>, ports * 2> frames{};
        std::array<float*, ports * 2> channels{};
        Ports()
        {
            for(size_t i = 0; i < channels.size(); ++i) channels[i] = frames[i].data();
        }
        void clear() { for(auto& c : frames) c.fill(0.f); }
        // Total absolute signal on one port's two channels.
        double energy(uint32_t port) const
        {
            double sum = 0;
            for(uint32_t c = 0; c < 2; ++c)
                for(const float f : frames[port * 2 + c]) sum += std::abs(f);
            return sum;
        }
    };

    void render(const Plugin& plugin, uint32_t frames, EventList& events, Ports& out)
    {
        out.clear();
        clap_audio_buffer_t buffers[ports];
        for(uint32_t p = 0; p < ports; ++p)
        {
            buffers[p] = {};
            buffers[p].channel_count = 2;
            buffers[p].data32 = &out.channels[p * 2];
        }
        clap_process_t process{};
        process.frames_count = frames;
        process.audio_outputs = buffers;
        process.audio_outputs_count = ports;
        process.in_events = &events.input;
        process.out_events = &events.output;
        plugin.plugin->process(plugin.plugin, &process);
    }

    void holdNote(EventList& events, uint32_t time, int16_t channel, int16_t key)
    {
        clap_event_note_t n;
        noteEvent(n, time, CLAP_EVENT_NOTE_ON, channel * 100 + key, channel, key);
        events.add(n);
    }
    void releaseNote(EventList& events, uint32_t time, int16_t channel, int16_t key)
    {
        clap_event_note_t n;
        noteEvent(n, time, CLAP_EVENT_NOTE_OFF, channel * 100 + key, channel, key);
        events.add(n);
    }
    void setBus(EventList& events, uint32_t time, clap_id id, double value, int16_t channel)
    {
        clap_event_param_value_t e;
        paramEvent(e, time, id, value);
        e.channel = channel; // a parameter event on a channel: the host's per-part edit
        events.add(e);
    }
}

int main(int argc, char** argv)
{
    if(argc < 2) { std::fprintf(stderr, "usage: %s PLUGIN.clap\n", argv[0]); return 2; }
    constexpr const char* id = "org.oscairn.example.instance-sharing";
    Plugin plugin(argv[1], id);
    const auto* sounds = plugin.extension<oscairn_plugin_sound_contexts_t>(OSCAIRN_EXT_SOUND_CONTEXTS);
    const auto* snaps = plugin.extension<oscairn_plugin_sound_snapshots_t>(OSCAIRN_EXT_SOUND_SNAPSHOTS);
    const auto* ui = plugin.extension<oscairn_plugin_ui_t>(OSCAIRN_EXT_UI);
    const auto* params = plugin.extension<clap_plugin_params_t>(CLAP_EXT_PARAMS);
    const auto* audio = plugin.extension<clap_plugin_audio_ports_t>(CLAP_EXT_AUDIO_PORTS);
    const auto* notes = plugin.extension<clap_plugin_note_ports_t>(CLAP_EXT_NOTE_PORTS);

    // Requirement 1: native sound contexts, with their state.
    check(sounds && sounds->capacity && sounds->get && sounds->prepare && sounds->select && sounds->discard,
          "sound-contexts/draft-5 is offered with every required function");
    check(sounds && sounds->state && sounds->state->save && sounds->state->load, "sound contexts carry their state");
    // Requirement 2: sound-snapshots draft 4 with save_sound and sound_values — sharing's pair.
    check(snaps && snaps->prepare, "sound-snapshots/draft-4 is offered with prepare");
    check(snaps && snaps->save_sound && snaps->sound_values, "save_sound and sound_values are both there (one without the other is no sharing)");
    check(ui && ui->param_hint, "oscairn.ui is offered with param_hint (the output-bus role is read through it)");
    if(!sounds || !snaps || !ui || !params || !audio || !notes) return finish("oscairn-example-instance-sharing");

    // Requirement 3: more than one audio output port. Requirement 5: no audio input.
    check(audio->count(plugin.plugin, false) > 1, "more than one audio output port");
    check(audio->count(plugin.plugin, true) == 0, "no audio input port");
    uint32_t stereo = 0, mains = 0;
    for(uint32_t i = 0; i < audio->count(plugin.plugin, false); ++i)
    {
        clap_audio_port_info_t info{};
        if(!audio->get(plugin.plugin, i, false, &info)) continue;
        stereo += info.channel_count == 2 && info.port_type && !std::strcmp(info.port_type, CLAP_PORT_STEREO);
        mains += (info.flags & CLAP_AUDIO_PORT_IS_MAIN) != 0;
    }
    check(stereo == audio->count(plugin.plugin, false), "every audio output is a stereo port");
    check(mains == 1, "exactly one of them is the main output");
    // Requirement 6: at least one note input, no note output.
    check(notes->count(plugin.plugin, true) >= 1, "at least one note input port");
    check(notes->count(plugin.plugin, false) == 0, "no note output port");

    // Requirement 4: an output-bus parameter — stepped, writable, from 0 to the last port index,
    // tagged OSCAIRN_ROLE_OUTPUT_BUS.
    clap_id busId = CLAP_INVALID_ID;
    for(uint32_t i = 0; i < params->count(plugin.plugin); ++i)
    {
        clap_param_info_t info{};
        if(!params->get_info(plugin.plugin, i, &info)) continue;
        oscairn_param_hint_t hint{};
        check(ui->param_hint(plugin.plugin, info.id, &hint), std::string("a hint answers for parameter ") + std::to_string(info.id));
        if(hint.role == OSCAIRN_ROLE_OUTPUT_BUS) busId = info.id;
    }
    check(busId != CLAP_INVALID_ID, "one parameter carries the output-bus role");
    if(busId == CLAP_INVALID_ID) return finish("oscairn-example-instance-sharing");
    clap_param_info_t bus{};
    check(params->get_info(plugin.plugin, uint32_t(busId) - 1, &bus), "the output-bus parameter is listed");
    check((bus.flags & CLAP_PARAM_IS_STEPPED) != 0, "it is stepped");
    check((bus.flags & CLAP_PARAM_IS_READONLY) == 0, "it is writable");
    check(bus.min_value == 0 && bus.max_value >= 1, "its range runs from 0 to at least 1");
    check(bus.max_value == double(audio->count(plugin.plugin, false)) - 1, "its maximum is the last output port index");

    check(plugin.activate(), "activates and starts processing");
    char message[256]{};

    // Two sounds of their own, each with its level, pitch and bus: two channels, as two parts.
    const std::array<oscairn_sound_parameter_t, 3> onSecondBus{{{1, 1.0}, {2, 12.0}, {3, 2.0}}};
    const auto busSound = sounds->prepare(plugin.plugin, "Bus 2", onSecondBus.data(), uint32_t(onSecondBus.size()), nullptr, nullptr, message, sizeof message);
    check(busSound != CLAP_INVALID_ID, std::string("a sound prepared with bus 2: ") + message);
    const oscairn_sound_parameter_t onMain{1, 0.4};
    const auto mainSound = sounds->prepare(plugin.plugin, "Main port", &onMain, 1, nullptr, nullptr, message, sizeof message);
    check(mainSound != CLAP_INVALID_ID, "a second sound prepares on the main port by default");
    check(sounds->select(plugin.plugin, busSound, 1), "the first is selected on channel 1");
    check(sounds->select(plugin.plugin, mainSound, 0), "the second on channel 0");
    Ports out;
    EventList idle;
    render(plugin, 256, idle, out); // the boundary the queued selections apply at

    // sound_values: the values of a sound no channel selects (the host reads a shared part's
    // sound); a selected sound answers 0, meaning "use what was saved".
    const oscairn_sound_parameter_t raised{2, 24.0};
    const auto unplayed = sounds->prepare(plugin.plugin, "Unselected", &raised, 1, nullptr, nullptr, message, sizeof message);
    oscairn_sound_parameter_t values[8]{};
    const auto listed = snaps->sound_values(plugin.plugin, unplayed, values, 8);
    check(listed == 3, "sound_values lists a sound's parameter values");
    bool asSet = false;
    for(uint32_t i = 0; i < listed && i < 8; ++i) asSet = asSet || (values[i].id == 2 && values[i].value == 24.0);
    check(asSet, "and they are the values it was prepared with");
    check(snaps->sound_values(plugin.plugin, busSound, values, 8) == 0, "a sound a channel selects answers no values");
    check(snaps->sound_values(plugin.plugin, 9999, values, 8) == 0, "an unknown sound answers none");

    // Each part's audio goes out its own port: channel 1's sound plays on bus 2, channel 0's on
    // the main port.
    render(plugin, 256, idle, out);
    check(out.energy(0) == 0 && out.energy(2) == 0, "before any note, every port is silent");
    EventList starts;
    holdNote(starts, 0, 0, 60);
    holdNote(starts, 0, 1, 60);
    render(plugin, 256, starts, out);
    double mainPort = 0, freeBus = 0, secondBus = 0;
    for(uint32_t b = 0; b < 8; ++b)
    {
        render(plugin, 256, idle, out);
        mainPort += out.energy(0);
        freeBus += out.energy(1);
        secondBus += out.energy(2);
    }
    check(secondBus > 1.0, "the sound set to bus 2 renders on port 2");
    check(mainPort > 1.0, "the sound left at bus 0 renders on the main port meanwhile");
    check(freeBus == 0, "an unused bus stays silent");

    // A parameter event with a channel edits the sound that channel selects: moving channel 1's
    // sound to bus 1 moves its next voices to port 1 (a voice keeps the port it started on), and
    // leaves channel 0's playing on the main port.
    EventList edits;
    setBus(edits, 0, busId, 1.0, 1);
    releaseNote(edits, 10, 1, 60); // the old voice fades out on port 2
    holdNote(edits, 20, 1, 62); // its replacement starts on bus 1
    render(plugin, 256, edits, out);
    double movedBus = 0, stillMain = 0, leftBus = 0;
    for(uint32_t b = 0; b < 12; ++b)
    {
        render(plugin, 256, idle, out);
        if(b > 1) // the released voice's 5 ms fade lasts about one block
        {
            movedBus += out.energy(1);
            stillMain += out.energy(0);
            leftBus += out.energy(2);
        }
    }
    check(movedBus > 1.0, "a channel's parameter event moves its own sound to that bus");
    check(stillMain > 1.0, "and the other channel keeps playing on the main port");
    check(leftBus < 1e-6, "the bus its last voice left goes silent");

    // save_sound: a whole snapshot holding that one sound alone — its Default, selected on every
    // channel and on the main port — what a part saved as a Sound of its own carries.
    OutStream saved;
    check(snaps->save_sound(plugin.plugin, busSound, &saved.stream), "save_sound writes the sound");
    check(!snaps->save_sound(plugin.plugin, 9999, &saved.stream), "an unknown sound is refused");
    size_t named = 0;
    for(size_t at = saved.data.find("Bus 2"); at != std::string::npos; at = saved.data.find("Bus 2", at + 1)) ++named;
    check(named == 1, "the snapshot names that sound once and holds no other");

    // Loading it into a fresh instance: the instance holds exactly that sound, selected on all 16
    // channels, and it plays on the main port — its bus back to 0, wherever it shared from.
    Plugin fresh(argv[1], id);
    check(fresh.activate(), "a second instance activates");
    InStream input{saved.data, 0};
    const auto* freshSounds = fresh.extension<oscairn_plugin_sound_contexts_t>(OSCAIRN_EXT_SOUND_CONTEXTS);
    check(freshSounds->state->load(fresh.plugin, &input.stream), "the single-sound snapshot loads into it");
    oscairn_sound_context_info_t info{};
    uint32_t prepared = 0, onAllChannels = 0;
    for(uint32_t slot = 0; slot < freshSounds->capacity(fresh.plugin); ++slot)
        if(freshSounds->get(fresh.plugin, slot, &info)) { ++prepared; onAllChannels += info.selected_channels == 0xFFFF; }
    check(prepared == 1, "the fresh instance holds exactly the one sound");
    check(onAllChannels == 1, "selected on every MIDI channel");
    EventList freshStarts;
    holdNote(freshStarts, 0, 3, 60);
    render(fresh, 256, freshStarts, out);
    double freshMain = 0, freshBus2 = 0;
    for(uint32_t b = 0; b < 8; ++b)
    {
        render(fresh, 256, idle, out);
        freshMain += out.energy(0);
        freshBus2 += out.energy(2);
    }
    check(freshMain > 1.0, "loaded alone, the sound plays on the main port");
    check(freshBus2 == 0, "and not on the bus it shared from");
    return finish("oscairn-example-instance-sharing");
}
