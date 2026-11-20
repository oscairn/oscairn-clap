// SPDX-License-Identifier: MIT
// oscairn.voice-budget: hold more notes than voices, set a limit with set_limits, and check the
// plugin gives voices up itself — fading, reported through active_voices and notes, the count
// shrinking to the limit — instead of the host cutting whole notes.
#include "../mini-host.hpp"
#include <oscairn-clap/voice-budget-plugin.hpp>

// The SDK helper alone (voice-budget-plugin.hpp), on voices described by hand.
namespace helper
{
    using oscairn::voices::Candidate;
    struct Pool
    {
        std::vector<Candidate> v; std::vector<bool> live;
        const Candidate* operator()(size_t i) const { return live[i] ? &v[i] : nullptr; }
    };
    Candidate voice(uint32_t context, bool held, double gain, uint64_t serial, double ageMs)
    {
        Candidate c; c.context = context; c.held = held; c.loudness = gain; c.age = serial; c.ageMs = ageMs;
        return c;
    }
    Pool pool(std::vector<Candidate> v) { Pool p{v, {}}; p.live.assign(p.v.size(), true); return p; }

    void checks()
    {
        using namespace oscairn::voices;
        {
            // Past the instance limit, by victim()'s order: a released voice of a priority-5 part
            // goes before a held key of a priority-0 part (tier before priority).
            oscairn_voice_budget_context_t parts[2]{};
            parts[0].context = 1; parts[0].priority = 0; parts[1].context = 2; parts[1].priority = 5;
            Limits limits; limits.set(0, parts, 2);
            Pool p = pool({voice(1, true, .5, 1, 3000), voice(2, false, .5, 2, 3000)});
            Importance<> report; report.list(2, p, limits);
            check(victim(2, p, limits, none) == 1 && report.next(2, p, limits) == 1,
                  "the instance limit: next() as victim(), a released voice of a higher-priority part before a held key");
        }
    }
}

int main(int argc, char** argv)
{
    if(argc < 2) { std::fprintf(stderr, "usage: %s PLUGIN.clap\n", argv[0]); return 2; }
    helper::checks();
    Plugin plugin(argv[1], "org.oscairn.example.voice-budget");
    const auto* budget = plugin.extension<oscairn_plugin_voice_budget_t>(OSCAIRN_EXT_VOICE_BUDGET_DRAFT_2);
    check(budget && budget->honors_volume && budget->set_limits && budget->active_voices && budget->notes && budget->importance,
          "oscairn.voice-budget/draft-2 is offered complete");
    check(plugin.extension<oscairn_plugin_voice_budget_t>(OSCAIRN_EXT_VOICE_BUDGET) == budget,
          "and the same table for draft 1 (OSCAIRN_EXT_VOICE_BUDGET), for an older host");
    if(!budget) return finish("oscairn-example-voice-budget");
    check(budget->honors_volume(plugin.plugin), "it says it honors the volume expression (checked below)");
    check(plugin.activate(), "activates and starts processing");

    float out[2][256]{};
    float* channels[2]{out[0], out[1]};
    auto holdNotes = [&](uint32_t blocks)
    {
        EventList starts;
        for(int i = 0; i < 6; ++i) // six notes, four voices
        {
            clap_event_note_t n;
            noteEvent(n, 0, CLAP_EVENT_NOTE_ON, 10 + i, 0, int16_t(60 + i));
            starts.add(n);
        }
        plugin.process(256, starts, channels);
        for(uint32_t b = 1; b < blocks; ++b)
        {
            EventList empty;
            plugin.process(256, empty, channels);
        }
    };
    holdNotes(30);
    double cost = 0;
    const uint32_t playing = budget->active_voices(plugin.plugin, &cost);
    check(playing > 0 && playing <= 4, "it reports the voices it sounds, never more than its pool");
    check(cost > 0, "it estimates a voice's cost");

    oscairn_voice_budget_note_t notes[16]{};
    const uint32_t reported = budget->notes(plugin.plugin, notes, 16);
    check(reported > 0 && reported <= playing, "every sounding note is reported, with its address");
    bool loudness = true;
    for(uint32_t i = 0; i < reported; ++i) loudness &= notes[i].voices >= 1 && notes[i].loudness > 0 && notes[i].loudness <= 1;
    check(loudness, "each note names its voices and a loudness in range");

    // The host's budget: two voices. The plugin must get there itself, fading what it gives up:
    // a voice given up fades for 3 ms (144 frames), counted until it has gone.
    budget->set_limits(plugin.plugin, 2, nullptr, 0); // between blocks, never during process
    EventList empty;
    plugin.process(64, empty, channels); // the limit lands at this block's start
    check(budget->active_voices(plugin.plugin, nullptr) > 2, "64 frames on, the voices it gave up are still fading");
    plugin.process(256, empty, channels);
    check(budget->active_voices(plugin.plugin, nullptr) <= 2, "a block later it keeps at most two voices");
    holdNotes(10);
    check(budget->active_voices(plugin.plugin, nullptr) <= 2, "and stays within it while playing on");

    // Note the limit does not change the notes it reports: a fading voice still sounds its note.
    const uint32_t during = budget->notes(plugin.plugin, notes, 16);
    check(during > 0, "notes still sounds its notes, fading ones reported");

    // Release everything: the voices go, and the plugin stops.
    budget->set_limits(plugin.plugin, 0, nullptr, 0);
    EventList releases;
    for(int i = 0; i < 6; ++i)
    {
        clap_event_note_t n;
        noteEvent(n, 0, CLAP_EVENT_NOTE_OFF, 10 + i, 0, int16_t(60 + i));
        releases.add(n);
    }
    plugin.process(256, releases, channels);
    for(uint32_t b = 0; b < 40; ++b) plugin.process(256, empty, channels);
    check(budget->active_voices(plugin.plugin, nullptr) == 0, "released voices end and stop being counted");
    check(plugin.process(256, empty, channels) == CLAP_PROCESS_SLEEP, "an idle instance reports sleep");

    // Draft 2: four keys held at known velocities list quietest first; lowered by two, the plugin
    // gives up the two it listed first (its listed slots, Importance::next()), and the keys left
    // are the two loudest.
    {
        EventList starts;
        const double velocities[4]{.9, .2, .6, .4}; // keys 80 to 83
        for(int i = 0; i < 4; ++i)
        {
            clap_event_note_t k;
            noteEvent(k, 0, CLAP_EVENT_NOTE_ON, 20 + i, 0, int16_t(80 + i), velocities[i]);
            starts.add(k);
        }
        plugin.process(256, starts, channels);
        for(int b = 0; b < 4; ++b) plugin.process(256, empty, channels); // past the 5 ms attack
        oscairn_voice_budget_importance_t lists[2]{};
        const uint32_t listed = budget->importance(plugin.plugin, lists, 2);
        check(listed == 1 && lists[0].voices == 4 && lists[0].count == 4, "one list of the four voices");
        bool ascending = listed == 1 && lists[0].count == 4;
        for(uint32_t i = 1; ascending && i < 4; ++i) ascending = lists[0].least[i - 1].level_db < lists[0].least[i].level_db;
        check(ascending && lists[0].least[0].tier == 2, "held keys, listed quietest first");
        const float first = lists[0].least[0].level_db, second = lists[0].least[1].level_db;
        check(std::abs(first - 20 * std::log10(.2)) < 1 && std::abs(second - 20 * std::log10(.4)) < 1, "the quietest two are the keys at .2 and .4");
        check(budget->importance(plugin.plugin, nullptr, 0) == 0, "no room, nothing written");
        budget->importance(plugin.plugin, lists, 2); // the report the drop follows
        budget->set_limits(plugin.plugin, 2, nullptr, 0);
        for(int b = 0; b < 2; ++b) plugin.process(256, empty, channels);
        const uint32_t left = budget->notes(plugin.plugin, notes, 16);
        bool loudest = left == 2;
        for(uint32_t i = 0; loudest && i < left; ++i) loudest = notes[i].key == 80 || notes[i].key == 82;
        check(loudest, "lowered by two, it gave up the two it listed first: the keys at .9 and .6 are left");
        budget->set_limits(plugin.plugin, 0, nullptr, 0);
        EventList ends;
        for(int i = 0; i < 4; ++i)
        {
            clap_event_note_t k;
            noteEvent(k, 0, CLAP_EVENT_NOTE_OFF, 20 + i, 0, int16_t(80 + i));
            ends.add(k);
        }
        plugin.process(256, ends, channels);
        for(uint32_t b = 0; b < 40; ++b) plugin.process(256, empty, channels);
    }

    // honors_volume: a note's volume expression at 0 silences it, and its loudness says so.
    EventList start;
    clap_event_note_t n;
    noteEvent(n, 0, CLAP_EVENT_NOTE_ON, 50, 0, 70);
    start.add(n);
    plugin.process(256, start, channels);
    EventList mute;
    clap_event_note_expression_t e{};
    e.header = {sizeof(e), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_EXPRESSION, 0};
    e.expression_id = CLAP_NOTE_EXPRESSION_VOLUME;
    e.note_id = 50;
    e.port_index = e.channel = e.key = -1;
    e.value = 0;
    mute.add(e);
    plugin.process(256, mute, channels);
    check(peak(out[0], 256) == 0, "volume 0 silences the note");
    check(budget->notes(plugin.plugin, notes, 16) == 1 && notes[0].loudness == 0, "and its reported loudness is 0");
    return finish("oscairn-example-voice-budget");
}
