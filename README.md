# oscairn-clap

The CLAP extensions of [Oscairn](https://github.com/oscairn/oscairn), an open synthesizer
platform, and the helpers for implementing them: C headers a plugin includes beside the
[CLAP SDK](https://github.com/free-audio/clap), and the reference for each extension. A plugin
that follows the CLAP specification already loads and plays in Oscairn; these extensions let it
fit the instrument better: editor hints for small screens and physical knobs, declared memory,
multitimbral sound contexts, instant Performance changes, parts sharing one instance, voice
budgets, shared effect engines, parameter locks for other plugins, a free-running beat, the
part's harmony, and sound assets.

Every extension is optional. A host that does not know one never asks for it, and a plugin that
does not offer one gets the fallback listed in [the fallback matrix](docs/fallback-matrix.md).

## What is here

- `include/oscairn-clap/`: the extension headers (C, `extern "C"`) and the SDK helpers (C++17,
  header-only).
- `docs/`: one reference page per extension: its ID and current draft, which side implements
  what, every function and event with its thread and call order, what the host does without it,
  a minimal example, and how to test it.
- `examples/`: a small example plugin per extension, building with CMake against the CLAP SDK.
- `LICENSE`: MIT.

| Extension | Header | Reference | Example |
| --- | --- | --- | --- |
| Editor hints, roles and rich views | `ui.h` | [ui](docs/ui.md) | [examples/ui](examples/ui/) |
| Declared memory | `memory.h` | [memory](docs/memory.md) | [examples/memory](examples/memory/) |
| Native sound contexts | `sound-contexts.h` | [sound-contexts](docs/sound-contexts.md) | [examples/sound-contexts](examples/sound-contexts/) |
| Snapshots prepared beside, ring-out, one sound alone | `sound-contexts.h` | [sound-snapshots](docs/sound-snapshots.md) | [examples/sound-snapshots](examples/sound-snapshots/) |
| Voice budget | `voice-budget.h` | [voice-budget](docs/voice-budget.md) | [examples/voice-budget](examples/voice-budget/) |
| Native effect contexts | `effect-contexts.h` | [effect-contexts](docs/effect-contexts.md) | [examples/effect-contexts](examples/effect-contexts/) |
| Parameter-target events and lanes | `param-target.h` | [param-target](docs/param-target.md) | [examples/param-target](examples/param-target/) |
| Free-running beat | `beat.h` | [beat](docs/beat.md) | [examples/beat](examples/beat/) |
| Harmony | `harmony.h` | [harmony](docs/harmony.md) | [examples/harmony](examples/harmony/) |
| Sound assets and asset dependencies | `assets.h` | [assets](docs/assets.md) | [examples/assets](examples/assets/) |
| Accelerators (draft) | `hardware.h` | [hardware](docs/hardware.md) | none yet |

| SDK helper | Reference |
| --- | --- |
| `sound-contexts-plugin.hpp`: slots, selection, voice ownership, snapshots, ring-out, voice budget | [sound-contexts-plugin](docs/sound-contexts-plugin.md) |
| `voice-budget-plugin.hpp`: the host's limits, the order to give voices up in, the note report | [voice-budget-plugin](docs/voice-budget-plugin.md) |
| `preset-discovery-plugin.hpp`: CLAP preset discovery from two functions | [preset-discovery-plugin](docs/preset-discovery-plugin.md) |

How Oscairn checks, hosts (in a worker process), packages, signs and tests a plugin is in Oscairn's
[plugin porting guide](https://github.com/oscairn/oscairn/blob/main/docs/plugin-porting.md). The
reference pages here say what each extension means and what Oscairn's host does with it; the
porting guide is the tutorial around them.

## License

MIT (see `LICENSE`), so plugins of any license, open or closed, can include the headers and the
helpers. `sound-contexts-plugin.hpp` was relicensed from AGPL-3.0-or-later to MIT on 2026-10-31.

## Using it

The headers need the CLAP SDK's headers on the include path (CLAP 1.2; Oscairn pins
`free-audio/clap` at `a47f6badb49d948fd009998f28309cdab78979c9`), and `include/` from this
repository:

```cpp
#include <clap/clap.h>
#include <oscairn-clap/ui.h>
```

Vendor it at a pinned commit, either way:

- **A git submodule:** `git submodule add https://github.com/oscairn/oscairn-clap third_party/oscairn-clap`,
  check out the commit you want in it, and commit the submodule. Add
  `third_party/oscairn-clap/include` to the include path.
- **A pinned copy:** copy `include/` and `LICENSE` into your tree and record the repository and
  the commit beside them. Oscairn does this at `core/clap-ext`, with an `upstream.json` holding
  `repository` and `commit`, and a check that fails when the copy differs from that commit.

With CMake's FetchContent (`SOURCE_SUBDIR` names a folder without a `CMakeLists.txt`, so the
repository is only downloaded):

```cmake
include(FetchContent)
FetchContent_Declare(oscairn_clap GIT_REPOSITORY https://github.com/oscairn/oscairn-clap.git
                     GIT_TAG <commit> SOURCE_SUBDIR include)
FetchContent_MakeAvailable(oscairn_clap)
target_include_directories(my_plugin PRIVATE ${oscairn_clap_SOURCE_DIR}/include)
```

Pin a commit, not a branch, and move the pin on purpose.

## Drafts change

Every extension is a **draft**, and its ID says so (`oscairn.ui/draft-5`). Until Oscairn's first
public release, drafts change without notice: a new draft may add, reorder or remove members, and
the host may stop reading an old one. Where a new draft only adds members at the end, Oscairn
keeps answering the older IDs, and each reference page says which drafts the host still reads.
When you move your pin, rebuild against the new headers and read each page's draft history.

The headers here and Oscairn's copy are kept identical: a change lands here first, and Oscairn
pins the commit that has it.
