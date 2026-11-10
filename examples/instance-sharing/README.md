# instance-sharing — parts sharing one instance

Several of Oscairn's parts play **one instance** of this plugin, each part with its own
sound, its own MIDI channel and its own output port (RFC 0021). What that gives the player:
a multitimbral Performance costs one instance's memory and one voice pool instead of one
per part, and the sounds are prepared in the same instance they play in, switched at the
cue without a reload.

It is not one extension but a combination; this example is what Oscairn checks
(`docs/fallback-matrix.md`, "Parts sharing one instance"):

| Requirement | Where the example meets it |
|---|---|
| native sound contexts with their `state` | `oscairn_plugin_sound_contexts_t soundContexts`, the SDK `Engine`'s statics, with `&snapshot` (its `state`) in the table |
| `oscairn.sound-snapshots` draft 4 with `save_sound` and `sound_values` | `soundSnapshots` lists `Synth::saveSound` and `Synth::soundValues`, built from the instrument's `alone(Patch&)` (forces the bus parameter to 0) and `patchValues` |
| more than one audio output port | four stereo ports: `Main` plus `Bus 1`–`Bus 3` (`clap_plugin_audio_ports_t audio`) |
| an output-bus parameter: stepped, writable, 0 to the last port index, role `OSCAIRN_ROLE_OUTPUT_BUS` | parameter 3 "Output bus": `CLAP_PARAM_IS_STEPPED`, range 0–3, its `oscairn.ui` hint `OSCAIRN_ROLE_OUTPUT_BUS` |
| no audio input port | `audio` answers 0 inputs |
| no note output port, at least one note input | `notes` answers 1 input, 0 outputs |

The sound is one sine, so a patch is three values — level, pitch, bus — and "each part its
own sound" is audible: `render` adds each voice into `block->audio_outputs[voice.bus]`, and
a parameter event carrying a MIDI channel edits the sound that channel selects
(`applyParam`), which is how the host gives each part its bus and its sound. `save_sound`
writes a snapshot holding exactly one sound — its Default, selected on every channel, back
on the main port — which is what a part saved as a Sound of its own carries.

**Build:** `cmake --build build --target oscairn-example-instance-sharing`

**In Oscairn:** put two parts' instruments on this plugin (`graph.add` of a second one
joins the instance the first plays on, or `instruments.share` asks for it explicitly).
Each part gets a sound of its own on a free channel and output bus; Routing shows the
part's audio on its own pair of outputs.

**Self-test:** `ctest --test-dir build -R oscairn-example-instance-sharing-selftest` —
the extensions offered, the port layout, the output-bus parameter's flags/range/role,
a sound set to bus 2 rendering on port 2 and not the main port, a per-channel edit moving
only its own sound, and `save_sound` → `state->load` into a fresh instance holding exactly
that one sound, selected everywhere, playing on the main port.

**Oscairn test:** with `OSCAIRN_JSON_RPC_TEST` pointing at a built
`oscairn-json-rpc-test`, `ctest --test-dir build -L oscairn-sharing` runs
`oscairn-sharing.py`: it starts Oscairn's JSON-RPC test server with this plugin, puts two
parts on it, commits, and checks with `graph.get` that both nodes report the same
`shared.engine` with different `channel` and `bus` — and that a parameter edit on one
part moves only its own sound.

## Without any one requirement

Without any one of them the plugin still works, but parts no longer share it:
`instruments.share` answers an error naming what is missing, and `graph.add` or opening a
Performance quietly gives each part an instance of its own. Without a second output port
or the bus parameter the host could not tell two parts' audio apart; without
`save_sound`/`sound_values` it could not save one part's sound on its own or show its
values.
