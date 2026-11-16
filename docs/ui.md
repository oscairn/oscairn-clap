# oscairn.ui: editor hints, roles and rich views

| | |
| --- | --- |
| ID | `OSCAIRN_EXT_UI` = `"oscairn.ui/draft-6"` (plugin extension) |
| Header | [`ui.h`](../include/oscairn-clap/ui.h) |
| Also read | `OSCAIRN_EXT_UI_DRAFT_5`, `OSCAIRN_EXT_UI_DRAFT_4`, `OSCAIRN_EXT_UI_DRAFT_3` |
| Design | Oscairn [RFC 0019](https://github.com/oscairn/oscairn/blob/main/docs/rfcs/0019-adaptive-ui.md), role banks in [RFC 0011](https://github.com/oscairn/oscairn/blob/main/docs/rfcs/0011-performance-ui-and-editors.md#step-6-status) |
| Example | [`examples/ui/`](../examples/ui/) |

Oscairn draws every plugin's editor from its parameters, on screens of any size, and puts
parameters on physical knobs; the plugin's own window opens on the desktop only. `oscairn.ui`
tells the host how each parameter is best shown and played: the control it wants, a priority, a
group, a role (what it does in terms every instrument shares), and its place in a rich view (an
envelope, a key or velocity map, an FM operator view) that the host draws and lets the player drag.
It also lets a plugin name and order its own pages and tabs, and give a stepped parameter's values
icons from a set the host draws.

Hints only shape the editor: every parameter stays reachable whatever its hint, and views never
replace tiles or physical controls.

## Who implements what

| Side | Implements |
| --- | --- |
| Plugin | `oscairn_plugin_ui_t`: `param_hint` (required), `algorithm` (draft 4, may be null), `seconds` (draft 5, may be null), `tab_count`, `tab`, `param_tab` and `value_icon` (draft 6, each may be null), returned from `get_extension(OSCAIRN_EXT_UI)` |
| Host | Reads the hints with the parameter info; nothing for the plugin to call |

## Functions

All of them are **main thread**. The host asks them when it reads parameter info: at every
activation, and after the plugin calls `clap_host_params.rescan(CLAP_PARAM_RESCAN_INFO)`. Hints may
change only with that info, and `view` numbers must stay stable across rescans.

### `bool param_hint(plugin, param_id, oscairn_param_hint_t* hint)`

Fill `hint` for `param_id`; return false when it has none (the host then decides from the
parameter's metadata). The host starts `hint` at "no hint" (`AUTO`, priority 0, `CLAP_INVALID_ID`,
group 0, no role, no view) before the call. The fields:

- **`widget`** (`enum oscairn_widget`): knob, slider, buttons, switch, list, or a range with
  `range_high` naming its high parameter. The host keeps it only where it fits: buttons and lists
  on a `CLAP_PARAM_IS_STEPPED` parameter, a switch on a stepped parameter of exactly two values, a
  range whose `range_high` is another existing parameter; otherwise `AUTO`, and the host decides.
  Unknown values mean `AUTO`.
- **`priority`:** 1 first; 0 is none, after every numbered one. It orders the parameter's tiles, a
  small screen's list (four at a time on a pedal-sized device with a few encoders), and the
  physical knobs: a device's screen knobs play an editor page's first parameters in this order; a
  part with no controls of its own whose roles fill no bank gets two Assign banks from its
  instrument's first parameters in this order; an effect without a `mix` role has its first
  continuous parameter (not stepped, such as an EQ's Mode) as its knob in the Effects bank, or its
  first parameter where all are stepped.
- **`group`:** parameters of one module with the same nonzero group are drawn joined, edge to edge
  in priority order, and kept together on narrow screens.
- **`role`** (`enum oscairn_role`): puts the parameter on the same knob in every instrument's role
  banks (Filter, Amp EG, EQ, Levels): cutoff, resonance, filter envelope amount, key tracking and
  the filter envelope's stages; attack, decay, sustain, release, hold, velocity, amp key tracking
  and level; the EQ's gains, frequencies, mid Q and output; levels 1 to 8 (elements, operators,
  oscillators); `MIX`, an effect's main control (its knob in the Effects bank); `PAN`; and
  `OUTPUT_BUS`, the stepped parameter from 0 choosing the output port a sound plays on (see
  [sound-snapshots](sound-snapshots.md#parts-sharing-one-instance)), which fills no bank. A role the
  plugin lacks is a blank knob in its bank. Audio effects (audio in and out) fill no role bank;
  instruments and other nodes do. The banks serve every
  set of physical knobs: a controller profile's knob rows and a Mackie Control surface's V-pots.
  Values past `OSCAIRN_ROLE_OUTPUT_BUS` mean none.
- **`view`, `field`, `index`** (draft 4): see [Rich views](#rich-views).

### `bool algorithm(plugin, param_id, value, oscairn_fm_algorithm_t* algorithm)` (draft 4, may be null)

For a parameter whose field is `OSCAIRN_FIELD_ALGORITHM`: the routing of algorithm `value`.
`inputs[n]` is the mask of operators modulating operator n + 1 (bit k is operator k + 1),
`carriers` the mask heard, `feedback_from` and `feedback_to` the feedback loop (0: none; the same
operator twice is self feedback). The host asks for every value of the parameter (from its minimum,
one per step) when it reads the hints and drops all routings if one returns false. A feedback
operator past 8 is read as none.

### `bool seconds(plugin, param_id, double value, double* seconds)` (draft 5, may be null)

For a `DELAY`, `ATTACK`, `HOLD`, `DECAY`, `RELEASE`, `TIME_n` or `RATE_n` parameter: how long its
stage takes at `value`, any value in its range; for a rate, the time a segment across the
envelope's whole range takes. Approximate is enough. The host asks at each value of a stepped
parameter of 2 to 128 values, else at 33 evenly spaced values from minimum to maximum, and drops
them all if one returns false or a negative or non-finite time.

Without `seconds` (draft 4, or draft 5 with it null), the host reads the parameter's value texts at
those values instead, when every one is a number followed by `ms`, `s` or `sec` ("31.2 ms",
"1.20 s"). When any of an envelope's stages has neither, the envelope is drawn by its parameters'
ranges, a rate reversed.

### Pages and tabs: `tab_count`, `tab`, `param_tab` (draft 6, each may be null)

Without them the host makes pages and tabs from the parameters' module paths (see [Without
it](#without-it)): a page per first level, in order of first appearance, and tabs from the second
level on a page with many parameters. With them a plugin names and orders its own, whatever its
module paths and the order of its parameters. The host draws pages and tabs in one style of its
own, themed with the rest of its interface (two levels: a page, and a row of tabs under it); a plugin
says what they are, never how they look.

```c
typedef struct oscairn_ui_tab {
    uint32_t id;     /* unique among all the entries */
    uint32_t parent; /* 0: a page; else the id of a page, and this is a tab under it */
    char name[64];   /* UTF-8, NUL-terminated: at most 63 bytes */
} oscairn_ui_tab_t;
```

`tab_count` gives the number of entries, pages and tabs together (0: none declared), and
`tab(plugin, index, &entry)` fills entry `index`. The list is the order: the pages are the entries
with `parent` 0, in the order they come, and a page's tabs are the entries naming it as `parent`, in
the order they come, wherever they stand among the others. `param_tab(plugin, param_id, &tab_id)`
sets the id of the page or tab the parameter is on; false leaves the parameter to its module path.
The host asks all three with the hints, for every parameter. Rules:

- **Ids** are the plugin's, nonzero, unique, and stable across rescans, so the host keeps the page
  and tab the player has open. The host leaves out an entry with an id of 0 or one used before, an
  empty name, or a `parent` that is not a page's id (there are two levels, no deeper), and reads at
  most 16 pages and 16 tabs under each page, skipping any past them (these limits are the host's
  and may be raised without a new draft). Names are cut at 63 bytes.
- **A parameter on a tab** is drawn on that tab only. **On a page itself** (its id is the page's) it is
  drawn on that page above its tabs, whichever tab is open; the host sets no limit on how many a
  plugin places there, they take room from the tabs' area on a small screen (a host may scroll the
  page), and keeping them few is the plugin's lookout. A parameter whose id is not an entry's
  is treated as left out.
- **A page with fewer than two tabs** is drawn without tabs: its one tab's parameters are the page's.
  With two or more, the host draws them whatever the page's size (its own threshold for tabs from
  module paths does not apply: the plugin has said what the tabs are). A page or tab with no
  parameters is not drawn.
- **Parameters left out, and a plugin that declares none,** get today's rule (a plugin need not
  place every parameter): their page is the
  first level of the module path and, on a page with many parameters, their tab the second. A
  declared page takes them when its name is that first level exactly (and a declared tab of it when
  its name is the second level exactly, else they are on the page itself). Where no declared page
  matches, they are on a page of their own after the declared ones, in order of first appearance.
  Nothing declared: pages and tabs are as before, unchanged.
- **Views** keep working. A view's parameters keep their own tiles, each on its own tab; the view is
  drawn once, beside the tiles of the first of its parameters' tabs in the order of `tab`
  (a page's own parameters come before its tabs). A tab holding only the parameters of a view is
  fine, and its tiles are there too.
- **When they change,** with the parameters' info: the host asks again after
  `clap_host_params.rescan(CLAP_PARAM_RESCAN_INFO)`, as for hints, and at every activation. A plugin
  whose structure follows its preset (an engine with more or fewer operators) calls it after the
  preset loads; a plugin whose tabs never change need not.

### `bool value_icon(plugin, param_id, double value, uint32_t* icon)` (draft 6, may be null)

For a stepped parameter: set `icon` to the `enum oscairn_icon` of the parameter at `value`, its
plain value (the same number `value_to_text` takes: an integer from the parameter's own minimum,
which may be negative); false when it has none. The host asks only for stepped parameters of at
most 128 values, at each of their step values, when it reads the hints. The icons are a closed set the host draws in its
own style, never a picture from the plugin:

| Icon | Shape |
| --- | --- |
| `SINE`, `TRIANGLE` | the waves |
| `SAW` | a ramp up, then a drop |
| `SAW_DOWN` | a ramp down, then a jump up |
| `SQUARE` | half high, half low |
| `PULSE` | a narrow pulse (a quarter of the cycle or less) |
| `NOISE` | noise |
| `SAMPLE_AND_HOLD` | random steps |

The set is waveforms only for now. `NONE` and any value a host does not know mean no icon. The host draws the icon beside the value's
text, or instead of it where a button is too small for the text. It replaces the host's own guess:
without the call, the host picks an icon when a value's text is a waveform's name ("Sine", "Saw",
"Square"); for a parameter where any value has an icon from the plugin, it guesses for none of
the others.

## Rich views

Every parameter with the same nonzero `view` is part of one view, wherever its module is (an FM
view's operators may be on pages of their own). `field` (`enum oscairn_field`) is its place in the
view, and `index` whose field it is: 1 to 8 (an operator's, an element's, a zone's), or 0 for the
view's own (the algorithm). What a view is follows from its fields:

| View | Fields | Drawn |
| --- | --- | --- |
| Envelope, named stages | `DELAY`, `ATTACK`, `HOLD`, `DECAY`, `SUSTAIN`, `RELEASE` (attack and release needed) | From zero: delay, attack to full, hold, decay to the sustain level, held, release to zero. No decay: the attack reaches the sustain level |
| Envelope, points | `LEVEL_0`…`LEVEL_8`, `TIME_1`…`TIME_8` or `RATE_1`…`RATE_8` (a rate is faster when higher) | Segment k reaches `LEVEL_k`; without one it holds, and the last falls to zero. From `LEVEL_0`, else the last segment's level (DX7's level 4), else zero. Holds at the end of segment n - 1 until the note ends; segment n is the release |
| Key or velocity map | Per index (a zone): `KEY_LOW`, `KEY_HIGH`, `VELOCITY_LOW`, `VELOCITY_HIGH`; and `BREAKPOINT`, `LEFT_DEPTH`, `RIGHT_DEPTH`, `LEFT_CURVE`, `RIGHT_CURVE`, `VELOCITY_DEPTH`, `VELOCITY_CURVE` | Every index's zone over a keyboard, the page's own in the accent; level scaling around the breakpoint, a curve's value text saying its sign and shape (`-LIN`, `+EXP`); velocity's effect. Key parameters are laid across their ranges as their value texts name the keys |
| FM operator view | Any view with `ALGORITHM` (stepped, at most 128 values, index 0) and `FEEDBACK`; per operator `OPERATOR_LEVEL`, `RATIO` (or its coarse part when `FINE` is its own), `FINE`, `DETUNE`, `FIXED`, and its envelope and scaling fields with its index | Where the algorithm is: the routing and a column per operator (level, ratio, envelope); an operator's page draws its envelope and scaling |

An envelope is drawn per view and index, a map per view and index (with all the view's zones),
each where its parameters' tiles are. Dragging a view sets its parameters, undone as any parameter
edit. Rules:

- `param_hint` must return true for **every** parameter in a view, also those with no role or
  widget (`OSCAIRN_WIDGET_AUTO`, priority 0, `CLAP_INVALID_ID`, group 0, their role or none).
- The host keeps a field only where it knows it, with an index up to 8, and an algorithm only on a
  stepped parameter of at most 128 values; anything else leaves its view and is a plain tile.
- An envelope's stages are drawn by time: each segment has the same room, filled on a log scale
  (about even below 50 ms) up to the longest any of its stages can take, past a least width that
  keeps an instant stage's point apart to grab. Dragging a point maps back through the same scale.
- Levels and other positions follow each parameter's range (0 at the left or bottom); value texts
  label them. A pitch envelope's levels are drawn from the bottom, not about a center, for now.
- A view is data: it says which parameters make it and how to read them, never how to draw it.
  A module grid and a waveform with markers are planned, not in a draft yet.

Worked examples for real engines (a DX7-style FM synth with three views and `seconds` from MSFA's
rate table, Surge's two ADSRs, an AWM2-style element map) are in Oscairn's
[porting guide](https://github.com/oscairn/oscairn/blob/main/docs/plugin-porting.md#rich-views-for-oscairns-engines).

## Without it

Every parameter still gets a tile; the host decides the control from the metadata (below). Roles
are guessed from parameter names, exact names only: "Cutoff", "Resonance", "Attack" (a filter
module's "Attack" is the filter envelope's), "Level", "Wet" or "Mix", and so on. That misses most
spellings, so declare roles: **declaring any role turns the guessing off for all the plugin's
other parameters**, so declare every role it has. Knobs go by parameter order where no priority
is given. Envelopes, LFOs and filters are still recognized by their parameters' names within one
module and drawn: an envelope under `Attack`, `Decay`, `Sustain`, `Release` (optionally `Delay`,
`Hold`), `Rate 1`–`4` with `Level 1`–`4` (DX7), or `Attack Time`, `Attack Level`, `Decay 1 Time`
and so on with `Release Time` (FM-X); an LFO's waveform from a `Wave`, `Type` or `Shape` with a
`Speed`, `Rate` or `Frequency`; a filter's response from `Cutoff` and `Resonance` with a `Type`
whose value text names it (`LP 24 dB`, `HPF12`). The host ignores the extension when `param_hint`
is null.

The parameter metadata matters with or without hints: the first level of a parameter's CLAP module
path names its page (and the second, on a page with many parameters, its tab, unless the plugin
declares them), a stepped parameter of 2 to 4 values is a row of buttons (up to 64, a list),
and value texts are shown as written. Oscairn's porting guide lists the rest under
[Editing without the plugin's window](https://github.com/oscairn/oscairn/blob/main/docs/plugin-porting.md#editing-without-the-plugins-window).

## Drafts

Draft 3 added `role`; draft 4 `view`, `field`, `index` and `algorithm`; draft 5 `seconds`; draft 6
`tab_count`, `tab`, `param_tab` and `value_icon`. The structs grew each time, so the host asks for
draft 6, then 5, 4 and 3, and reads only the members that draft has: a draft 5 plugin gets its
pages and tabs from the module paths and the icons the host guesses, a draft 4 plugin also its
times from value texts, a draft 3 plugin its hints and roles without views. A plugin built against
draft 2 or earlier is not read; rebuild it.

Draft 6 grew only the table: the hint did not change. A draft 6 table is a draft 5 table with
members after its last, so a plugin can return the same table for the draft 6, 5, 4 and 3 IDs (a
draft 5 host reads the first three members and ignores the rest), and a draft 6 plugin on a draft 5
host behaves as a draft 5 plugin. A plugin that wants no host older than draft 6 answers only the
draft 6 ID.

Draft 7 is reserved for the ninth level role (`OSCAIRN_ROLE_LEVEL_9`) and a `reversed` field at the
end of the hint; draft 6 is the tabs and icons.

## Minimal example

An instrument whose cutoff and level are on the role knobs and whose ADSR is a view drawn by time
(parameter IDs 1 to 6):

```cpp
#include <oscairn-clap/ui.h>

static bool paramHint(const clap_plugin_t*, clap_id id, oscairn_param_hint_t* hint)
{
    static const uint32_t roles[] = {0, OSCAIRN_ROLE_CUTOFF, OSCAIRN_ROLE_LEVEL,
        OSCAIRN_ROLE_ATTACK, OSCAIRN_ROLE_DECAY, OSCAIRN_ROLE_SUSTAIN, OSCAIRN_ROLE_RELEASE};
    static const uint32_t fields[] = {0, 0, 0, OSCAIRN_FIELD_ATTACK, OSCAIRN_FIELD_DECAY,
        OSCAIRN_FIELD_SUSTAIN, OSCAIRN_FIELD_RELEASE};
    if (id < 1 || id > 6) return false;
    *hint = {OSCAIRN_WIDGET_KNOB, id <= 2 ? id : 0, CLAP_INVALID_ID, id >= 3 ? 1u : 0u,
             roles[id], fields[id] ? 1u : 0u, fields[id], 0};
    return true;
}

// The stage times are seconds already (0 to 2 s); the sustain is a level.
static bool seconds(const clap_plugin_t*, clap_id id, double value, double* out)
{
    if (id < 3 || id > 6 || id == 5) return false;
    *out = value;
    return true;
}

static const oscairn_plugin_ui_t ui{paramHint, nullptr, seconds};
// in get_extension:
if (!strcmp(id, OSCAIRN_EXT_UI)) return &ui;
```

## Testing

- In Oscairn's desktop app, open the plugin's editor (Part page): its tiles follow the priorities
  and groups, and the view is drawn above its parameters' tiles. Drag it and check the parameter
  values move. `oscairn-plugin-check` does not look at hints.
- Over Oscairn's JSON-RPC control interface, `params.list` returns each parameter's kept hint
  (widget, priority, group, role, view, field, index, and a view's `seconds` and `algorithms`
  when the host kept them), so a test can check what the host kept
  rather than what the plugin returned: a widget that does not fit, or a field on an index past 8,
  is dropped.
- Check `param_hint` returns true for every parameter in a view, and that `seconds` (or the value
  texts) answers at every value the host asks: one miss drops all the times.
- Oscairn's own tests: `desktop-gaps` edits the views of `hosts/standalone/tests/views_plugin.cpp`,
  which declares all three kinds.
