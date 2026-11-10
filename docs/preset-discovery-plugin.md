# preset-discovery-plugin.hpp: CLAP preset discovery from two functions

| | |
| --- | --- |
| Header | [`preset-discovery-plugin.hpp`](../include/oscairn-clap/preset-discovery-plugin.hpp) (C++17, header-only, MIT) |
| Implements | CLAP's preset discovery factory (`CLAP_PRESET_DISCOVERY_FACTORY_ID`) and one provider |
| Loads with | [`oscairn.sound-contexts`](sound-contexts.md)' `prepare_preset` (on the SDK, `Engine::preparePreset`), or CLAP's `preset-load` |
| Design | Oscairn [RFC 0005](https://github.com/oscairn/oscairn/blob/main/docs/rfcs/0005-sound-contexts.md#presets) |
| Example | [`examples/sound-contexts/`](../examples/sound-contexts/) |

Oscairn lists a plugin's CLAP preset-discovery presets where sounds are picked and prepares any
of them as an instant-switch sound. This helper writes the factory and the provider; the plugin
says only where its presets are and what each is called.

## What the plugin provides

A type (say `Presets`) with, all static:

| Member | Is |
| --- | --- |
| `const clap_preset_discovery_provider_descriptor_t descriptor` | The provider's descriptor (its own ID, not the plugin's) |
| `const char* const pluginId` | The plugin the presets are for |
| `void declare(oscairn::presets::Declarations& d)` | Where they are: `d.inside("Factory")` for presets inside the plugin, `d.files("Patches", folder, "fxp")` for a file or a folder of files ending `.fxp` (null or empty extension: every file). Optional last argument: location flags (default `CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT`) |
| `void list(uint32_t kind, const char* location, oscairn::presets::Listing& l)` | The presets in one location: `l.add(name, loadKey)` for each, stopping when it returns false; `l.error(message)` reports a problem |

and `clap_entry.get_factory` returns `&oscairn::presets::Factory<Presets>::factory` for
`CLAP_PRESET_DISCOVERY_FACTORY_ID` (and, for older hosts, `CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT`,
which Oscairn also asks for).

### The load-key scheme

A preset is its location kind, location and load key, exactly what `prepare_preset` and
`clap_plugin_preset_load.from_location` take. Inside the plugin (`kind`
`CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN`, `location` null) every preset needs a name and a load
key. A file holding several presets names each and gives each a load key. A file that is one preset
may give neither (`l.add(name)` with no key; Oscairn names it after the file).

## What the helper does

- `init` calls `declare` and declares each location (and file type) with the indexer; if any
  declaration is refused, `init` returns false and the host skips the whole provider.
- `get_metadata(kind, location, receiver)` calls `list` for that location; each `add` calls
  `begin_preset(name, loadKey)` and `add_plugin_id` with `pluginId`.
- The provider has no extensions of its own. One factory, one provider.

**File types are per provider in Oscairn's indexer:** every extension any `files` call declares is
applied to every folder the provider declares. Give a plugin's folders one file type each only
when they all hold the same type, or filter in `list`.

## What Oscairn does

- **Listing.** A scan indexes presets in a separate helper process (`oscairn-plugin-scan`, beside
  the engine or the desktop app, or named by `OSCAIRN_PLUGIN_SCAN`) with a 15-second timeout, and a
  listing for a loaded plugin reads them through the same helper. The listing, or its failure, is
  kept per plugin binary until the binary changes, so a provider that hangs costs one timeout, not
  one per listing; a helper that could not start is tried again at the next listing. Until its
  listing is in, `presets.list` answers "Presets are being listed; ask again". A plugin in a worker
  lists presets only through the helper (without one it lists none, and `presets.list` says why).
  Only a plugin in the engine's own process with no helper configured has its provider run there,
  on the control thread. `get_metadata` must be fast and never interactive, as CLAP says.
- **Limits:** 4096 presets per plugin library, 64 providers, 64 locations and 64 file types per
  provider, 10,000 files per folder and 16 folder levels, names up to 255 bytes, locations and load
  keys up to 4096 bytes, a 10-second reading budget per scan; a preset past a limit is skipped.
- **Nothing watches preset folders**, and the listing is kept, so files added later show once the
  plugin's binary changes or the engine restarts.
- **Loading.** A native instrument's preset becomes a new sound context through `prepare_preset`
  (from the plugin's defaults, while the playing sound plays on); a native instrument without it
  has its presets listed but refused. Any other plugin's preset is loaded with CLAP `preset-load`'s
  `from_location` into a spare instance, never during `process`.
- **What a Sound saves** is the plugin's own state (for native contexts, the snapshot of every
  prepared sound), never a reference to the preset, so it opens when the preset is gone.
- A provider's `init` may ask the indexer's `get_extension(OSCAIRN_EXT_ASSETS)` for
  `library_root`, to declare an installed asset library's folder as a location ([assets](assets.md)).

## Without it

Presets that a plugin offers only in its own window are not listed anywhere in Oscairn; a user
reaches them only by opening the window on the desktop and saving the result as a Sound.

## Minimal example

```cpp
#include <oscairn-clap/preset-discovery-plugin.hpp>

struct Presets
{
    static constexpr clap_preset_discovery_provider_descriptor_t descriptor{
        CLAP_VERSION_INIT, "com.example.synth.presets", "Example presets", "Example"};
    static constexpr const char* pluginId = "com.example.synth";
    static void declare(oscairn::presets::Declarations& d) { d.inside("Factory"); }
    static void list(uint32_t kind, const char*, oscairn::presets::Listing& l)
    {
        if (kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN)
            l.add("Warm", "warm") && l.add("Bright", "bright");
    }
};

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry{CLAP_VERSION_INIT,
    [](const char*) { return true; }, [] {},
    [](const char* id) -> const void* {
        if (!strcmp(id, CLAP_PRESET_DISCOVERY_FACTORY_ID) || !strcmp(id, CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT))
            return &oscairn::presets::Factory<Presets>::factory;
        return !strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? &factory : nullptr;
    }};
```

The plugin then reads a preset by its key: on the SDK, `readPreset(kind, location, loadKey, patch,
error)` (see [sound-contexts-plugin](sound-contexts-plugin.md#presets)).

## Testing

- With a mock indexer, create the provider, call `init` and check the declared locations, then
  `get_metadata` for each and check every preset has a name, a load key where needed and the plugin
  ID.
- In Oscairn, `presets.list` over JSON-RPC lists a loaded plugin's presets (ask again while it says
  they are being listed).
- Oscairn's tests: `control-sound-presets` (Basic Synth, which uses this helper: five factory
  presets inside the plugin, load keys their names in lower case and hyphenated), `clap-presets`.
