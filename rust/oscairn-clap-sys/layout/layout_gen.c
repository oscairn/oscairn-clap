/* SPDX-License-Identifier: MIT */
/* Prints the C layout facts (one per line) that build.rs bakes into the layout tests:
 *   S <type> <size> <align>
 *   F <type> <member> <offset>
 * Compiled and run by build.rs against the same headers bindgen sees. */
#include <oscairn-clap/assets.h>
#include <oscairn-clap/beat.h>
#include <oscairn-clap/effect-contexts.h>
#include <oscairn-clap/harmony.h>
#include <oscairn-clap/memory.h>
#include <oscairn-clap/param-target.h>
#include <oscairn-clap/sound-contexts.h>
#include <oscairn-clap/ui.h>
#include <oscairn-clap/voice-budget.h>
#include <stddef.h>
#include <stdio.h>

#define S(t) printf("S %s %zu %zu\n", #t, sizeof(t), _Alignof(t))
#define F(t, m) printf("F %s %s %zu\n", #t, #m, offsetof(t, m))

int main(void)
{
    S(oscairn_asset_ref_t);
    F(oscairn_asset_ref_t, library);
    F(oscairn_asset_ref_t, path);
    F(oscairn_asset_ref_t, sha256);
    S(oscairn_host_assets_t);
    F(oscairn_host_assets_t, resolve);
    F(oscairn_host_assets_t, library_root);
    F(oscairn_host_assets_t, create);
    S(oscairn_plugin_asset_dependencies_t);
    F(oscairn_plugin_asset_dependencies_t, count);
    F(oscairn_plugin_asset_dependencies_t, get);
    F(oscairn_plugin_asset_dependencies_t, optional);

    S(oscairn_event_beat_t);
    F(oscairn_event_beat_t, header);
    F(oscairn_event_beat_t, beats);
    F(oscairn_event_beat_t, tempo);

    S(oscairn_effect_resources_t);
    F(oscairn_effect_resources_t, shared_bytes);
    F(oscairn_effect_resources_t, context_bytes);
    F(oscairn_effect_resources_t, capacity);
    F(oscairn_effect_resources_t, prepared);
    F(oscairn_effect_resources_t, tail_active);
    S(oscairn_plugin_effect_contexts_t);
    F(oscairn_plugin_effect_contexts_t, create);
    F(oscairn_plugin_effect_contexts_t, resources);

    S(oscairn_harmony_t);
    F(oscairn_harmony_t, key_root);
    F(oscairn_harmony_t, chord_root);
    F(oscairn_harmony_t, chord_type);
    F(oscairn_harmony_t, chord_bass);
    F(oscairn_harmony_t, scale);
    F(oscairn_harmony_t, reserved);
    F(oscairn_harmony_t, held);
    S(oscairn_event_harmony_t);
    F(oscairn_event_harmony_t, header);
    F(oscairn_event_harmony_t, harmony);
    S(oscairn_host_harmony_t);
    F(oscairn_host_harmony_t, get);

    S(oscairn_memory_t);
    F(oscairn_memory_t, shared_bytes);
    F(oscairn_memory_t, context_bytes);
    F(oscairn_memory_t, asset_bytes);
    S(oscairn_plugin_memory_t);
    F(oscairn_plugin_memory_t, estimate);
    F(oscairn_plugin_memory_t, current);

    S(oscairn_param_target_lane_info_t);
    F(oscairn_param_target_lane_info_t, lane);
    F(oscairn_param_target_lane_info_t, name);
    S(oscairn_plugin_param_target_lanes_t);
    F(oscairn_plugin_param_target_lanes_t, count);
    F(oscairn_plugin_param_target_lanes_t, get);
    S(oscairn_event_param_target_t);
    F(oscairn_event_param_target_t, header);
    F(oscairn_event_param_target_t, lane);
    F(oscairn_event_param_target_t, mode);
    F(oscairn_event_param_target_t, note_id);
    F(oscairn_event_param_target_t, port_index);
    F(oscairn_event_param_target_t, channel);
    F(oscairn_event_param_target_t, key);
    F(oscairn_event_param_target_t, value);

    S(oscairn_sound_parameter_t);
    F(oscairn_sound_parameter_t, id);
    F(oscairn_sound_parameter_t, value);
    S(oscairn_sound_context_info_t);
    F(oscairn_sound_context_info_t, id);
    F(oscairn_sound_context_info_t, name);
    F(oscairn_sound_context_info_t, selected_channels);
    F(oscairn_sound_context_info_t, active_voices);
    F(oscairn_sound_context_info_t, idle);
    S(oscairn_plugin_sound_contexts_t);
    F(oscairn_plugin_sound_contexts_t, capacity);
    F(oscairn_plugin_sound_contexts_t, get);
    F(oscairn_plugin_sound_contexts_t, prepare);
    F(oscairn_plugin_sound_contexts_t, select);
    F(oscairn_plugin_sound_contexts_t, discard);
    F(oscairn_plugin_sound_contexts_t, state);
    F(oscairn_plugin_sound_contexts_t, prepare_preset);
    S(oscairn_plugin_sound_snapshots_t);
    F(oscairn_plugin_sound_snapshots_t, prepare);
    F(oscairn_plugin_sound_snapshots_t, ring_out);
    F(oscairn_plugin_sound_snapshots_t, choke_retiring);
    F(oscairn_plugin_sound_snapshots_t, save_sound);
    F(oscairn_plugin_sound_snapshots_t, sound_values);

    S(oscairn_param_hint_t);
    F(oscairn_param_hint_t, widget);
    F(oscairn_param_hint_t, priority);
    F(oscairn_param_hint_t, range_high);
    F(oscairn_param_hint_t, group);
    F(oscairn_param_hint_t, role);
    F(oscairn_param_hint_t, view);
    F(oscairn_param_hint_t, field);
    F(oscairn_param_hint_t, index);
    S(oscairn_fm_algorithm_t);
    F(oscairn_fm_algorithm_t, inputs);
    F(oscairn_fm_algorithm_t, carriers);
    F(oscairn_fm_algorithm_t, feedback_from);
    F(oscairn_fm_algorithm_t, feedback_to);
    S(oscairn_plugin_ui_t);
    F(oscairn_plugin_ui_t, param_hint);
    F(oscairn_plugin_ui_t, algorithm);
    F(oscairn_plugin_ui_t, seconds);

    S(oscairn_voice_budget_context_t);
    F(oscairn_voice_budget_context_t, context);
    F(oscairn_voice_budget_context_t, limit);
    F(oscairn_voice_budget_context_t, reserve);
    F(oscairn_voice_budget_context_t, priority);
    F(oscairn_voice_budget_context_t, flags);
    S(oscairn_voice_budget_note_t);
    F(oscairn_voice_budget_note_t, note_id);
    F(oscairn_voice_budget_note_t, port_index);
    F(oscairn_voice_budget_note_t, channel);
    F(oscairn_voice_budget_note_t, key);
    F(oscairn_voice_budget_note_t, voices);
    F(oscairn_voice_budget_note_t, loudness);
    S(oscairn_plugin_voice_budget_t);
    F(oscairn_plugin_voice_budget_t, honors_volume);
    F(oscairn_plugin_voice_budget_t, set_limits);
    F(oscairn_plugin_voice_budget_t, active_voices);
    F(oscairn_plugin_voice_budget_t, notes);

    printf("C %u\n", (unsigned)CLAP_INVALID_ID); /* the sentinel both sides must share */
    return 0;
}
