// SPDX-License-Identifier: MIT
// oscairn.ui: read every hint through the extension, check the roles, the ADSR view and
// seconds(), then play a note and check the audio follows the parameters.
#include "../mini-host.hpp"

int main(int argc, char** argv)
{
    if(argc < 2) { std::fprintf(stderr, "usage: %s PLUGIN.clap\n", argv[0]); return 2; }
    Plugin plugin(argv[1], "org.oscairn.example.ui");
    const auto* ui = plugin.extension<oscairn_plugin_ui_t>(OSCAIRN_EXT_UI);
    check(ui && ui->param_hint && ui->seconds, "oscairn.ui/draft-5 is offered with param_hint and seconds");
    if(!ui) return finish("oscairn-example-ui");

    oscairn_param_hint_t hints[7]{};
    for(clap_id id = 1; id <= 6; ++id)
        check(ui->param_hint(plugin.plugin, id, &hints[id]), "hint for parameter");
    check(!ui->param_hint(plugin.plugin, 7, &hints[0]), "no hint past the last parameter");
    check(hints[1].role == OSCAIRN_ROLE_CUTOFF && hints[1].widget == OSCAIRN_WIDGET_KNOB && hints[1].priority == 1,
          "Brightness is the cutoff knob, first priority");
    check(hints[2].role == OSCAIRN_ROLE_LEVEL, "Level has the level role");
    const uint32_t fields[] = {OSCAIRN_FIELD_ATTACK, OSCAIRN_FIELD_DECAY, OSCAIRN_FIELD_SUSTAIN, OSCAIRN_FIELD_RELEASE};
    const uint32_t stageRoles[] = {OSCAIRN_ROLE_ATTACK, OSCAIRN_ROLE_DECAY, OSCAIRN_ROLE_SUSTAIN, OSCAIRN_ROLE_RELEASE};
    bool stages = true;
    for(clap_id id = 3; id <= 6; ++id)
        stages &= hints[id].field == fields[id - 3] && hints[id].view == 1 && hints[id].group == 1 && hints[id].index == 0 &&
                  hints[id].role == stageRoles[id - 3];
    check(stages, "the four stages name their envelope view, group, field and role");
    check(hints[1].range_high == CLAP_INVALID_ID, "a plain knob names no range high end");

    double seconds = -1;
    check(ui->seconds(plugin.plugin, 3, .25, &seconds) && seconds == .25, "an attack of .25 takes .25 s");
    check(!ui->seconds(plugin.plugin, 5, .5, &seconds), "the sustain level names no stage time");

    // Play it: a held note sounds, and Level 0 silences it.
    const auto* params = plugin.extension<clap_plugin_params_t>(CLAP_EXT_PARAMS);
    check(params != nullptr, "the parameter interface is offered");
    check(plugin.activate(), "activates and starts processing");
    float out[2][256]{};
    float* channels[2]{out[0], out[1]};
    auto render = [&](uint32_t blocks) { double p = 0; for(uint32_t b = 0; b < blocks; ++b) { EventList in; plugin.process(256, in, channels); p = std::max(p, peak(out[0], 256)); } return p; };
    EventList starts;
    clap_event_note_t n;
    noteEvent(n, 0, CLAP_EVENT_NOTE_ON, 1, 0, 60);
    starts.add(n);
    plugin.process(256, starts, channels);
    check(render(199) > .05, "a held note sounds");
    EventList edits;
    clap_event_param_value_t e;
    paramEvent(e, 0, 2, 0.0); // Level to 0
    edits.add(e);
    params->flush(plugin.plugin, &edits.input, &edits.output);
    check(render(200) < 1e-3, "Level 0 silences it");
    return finish("oscairn-example-ui");
}
