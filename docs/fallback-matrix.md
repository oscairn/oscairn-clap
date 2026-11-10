# Fallback matrix

What a plain CLAP plugin gets in Oscairn instead of each extension. Every extension is optional:
a plugin without it loads and plays, and gets the column on the right. Details are on each
extension's page.

| Extension | With it | A plain CLAP plugin gets |
| --- | --- | --- |
| [`oscairn.ui`](ui.md) | Its controls, priorities, groups, roles on the role-bank knobs, envelope, map and FM views drawn to drag | Generated editors from its parameters (pages from module paths, controls from flags and ranges); roles guessed from exact parameter names ("Cutoff", "Attack", "Wet"…); envelopes, LFOs and filters drawn only when recognized by name; knobs in parameter order |
| [`oscairn.memory`](memory.md) | Refused before it allocates when it would not fit; a worker memory limit sized to it; modes measured apart | Measured after loading; counted at 32 MiB until measured; the flat worker limit (4 GiB by default); never refused before it allocates |
| [`oscairn.sound-contexts`](sound-contexts.md) | Up to 256 prepared sounds in one instance, chosen per MIDI channel, held notes keeping theirs; presets prepared while playing | Instant switching by extra instances: up to 8 per part (the Default one of them), each a whole instance's memory and CPU, only for instruments (note input, audio output, no audio input, no note output); presets loaded with `preset-load` into a spare instance |
| [`oscairn.sound-snapshots`](sound-snapshots.md) | The next Performance's sounds prepared beside the playing ones and selected at the cue; tails ringing out through the outgoing chain; parts sharing one instance | At a Performance change the snapshot is loaded into a free, warm or new instance (a sampler loads its samples again), the old instance ringing out alone; each part plays its own instance |
| [`oscairn.voice-budget`](voice-budget.md) | It gives up its own least audible voices within a limit the host sets | The host fades whole notes (volume expression to silence over 5 ms, then a choke), released and quiet ones first; CLAP notes only, never a MIDI-only plugin's |
| [`oscairn.effect-contexts`](effect-contexts.md) | Many effect slots on one engine; the next Performance's effects as new contexts while the old ones ring out | Each effect slot its own instance, loaded or taken over at each change; saved contexts of a shared engine open as stand-ins |
| [`oscairn.param-target`](param-target.md) and lanes | Sends sample-accurate changes to other plugins' parameters and controls; lanes listed in Routing | Changes only its own parameters; other plugins move only by the user's controls and scenes |
| [`oscairn.beat`](beat.md) | A free-running beat shared by every plugin, also while the transport is stopped | CLAP's transport, which stops moving when the transport stops |
| [`oscairn.harmony`](harmony.md) | The part's key, scale and held chord, as events and through the host extension | Only the notes that reach it; no key or scale |
| [`oscairn.assets`](assets.md) and asset dependencies | Samples as references the host resolves, recorded in Performances, checked before opening, carried by exports; optional assets with a notice | Its own file paths in its state: nothing recorded, checked or carried |
| [Preset discovery](preset-discovery-plugin.md) (CLAP) | Presets listed in Oscairn's sound lists and prepared as instant-switch sounds | Presets reachable only in its own window, on the desktop |

## Parts sharing one instance

Sharing is not one extension but a combination. Parts whose instruments are the same plugin share
one instance only when the plugin has **all** of:

1. native sound contexts (`oscairn.sound-contexts`) with their `state`;
2. `oscairn.sound-snapshots` with `save_sound` and `sound_values` (draft 4);
3. **more than one audio output port**;
4. an **output-bus parameter**: stepped, writable, from 0 to at least 1, its value the index of
   the output port a sound plays on, tagged `OSCAIRN_ROLE_OUTPUT_BUS` in `oscairn.ui`;
5. **no audio input port**;
6. **no note output port** (and at least one note input).

At run time Oscairn also needs one sound selected on every channel when the first part joins, a
plugin that has not failed, and, at a Performance load, no effect contexts made from it and audio
taken only from its main output. Without all of it, each part keeps an instance of its own. See
[sound-snapshots](sound-snapshots.md#parts-sharing-one-instance).

## Not an extension, but depended on

- **`clap.state`** (or sound contexts' `state`): without it a Performance keeps only parameter
  values, for plugins with up to 2,048 writable parameters, and a worker cannot restore the plugin's
  sound after a crash.
- **`CLAP_PARAM_IS_MODULATABLE`**: without it, a relative control target moves the parameter's
  value (saved, undone) instead of modulating it.
- **`CLAP_EVENT_NOTE_END`, `NOTE_CHOKE` as a fade, and the volume expression**: without them the
  host's note stealing clicks and charges the plugin for voices it no longer plays.
- **`CLAP_PROCESS_SLEEP`**: without it a plugin left behind by a scene or a Performance change runs
  until three seconds of silence instead of stopping when idle.
