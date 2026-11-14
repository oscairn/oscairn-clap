# oscairn.assets and oscairn.asset-dependencies: sound assets

| | |
| --- | --- |
| IDs | Host extension `OSCAIRN_EXT_ASSETS` = `"oscairn.assets/draft-4"` (also answered: `draft-3`, `draft-2`, `draft-1`); plugin extension `OSCAIRN_EXT_ASSET_DEPENDENCIES` = `"oscairn.asset-dependencies/draft-2"` (also read: `draft-1`) |
| Header | [`assets.h`](../include/oscairn-clap/assets.h) |
| Design | Oscairn [RFC 0009](https://github.com/oscairn/oscairn/blob/main/docs/rfcs/0009-sound-assets-and-dependencies.md), asset packages in [the library archive format](https://github.com/oscairn/oscairn/blob/main/docs/library-archive.md#asset-packages) |
| Example | [`examples/assets/`](../examples/assets/) |

For samplers and anything else whose sound depends on files. The plugin's state stores **asset
references**, never local paths: a library, a path relative to it, and the file's SHA-256. The
host resolves a reference to a verified local file, and the plugin lists the references its state
uses, so a Performance records its samples, can be checked before it opens, and moves between
machines with them.

```c
typedef struct oscairn_asset_ref {
    char library[64];  /* a library ID: "user", or an installed library's */
    char path[1024];   /* relative to the library */
    char sha256[65];   /* 64 lowercase hex digits, or empty when not yet known */
} oscairn_asset_ref_t;
```

## Who implements what

| Side | Implements |
| --- | --- |
| Host | `oscairn_host_assets_t`: `resolve`, `library_root` (draft 2), `create` (draft 3), `resolve_sample` (draft 4), from `clap_host.get_extension(OSCAIRN_EXT_ASSETS)`, and from a preset indexer's `get_extension` |
| Plugin | `oscairn_plugin_asset_dependencies_t`: `count` and `get` (required), `optional` (draft 2, may be null) |

## Host functions (the plugin calls them)

All are **main thread**. Called from another thread, `resolve`, `create` and `resolve_sample` return false with a
reason and `library_root` returns false.

### `bool resolve(host, oscairn_asset_ref_t* ref, char* path, path_capacity, char* message, message_capacity)`

Writes a local, verified path for `ref` into `path`. When `ref->sha256` is empty it is filled in
with the file's hash (keep it in your state); when given, the file must have that content
(identical bytes in another library also satisfy it). The path stays inside the library. On failure
it returns false with a NUL-terminated reason in `message`.

### `bool library_root(owner, library_id, min_version, path, path_capacity)` (draft 2)

The folder holding an installed asset library's files at their paths, read-only, when a version at
least `min_version` (null or empty: any) is installed: in Oscairn,
`AssetLibraries/<id>/<version>/` in the data folder. Writes the NUL-terminated path and returns
true; false when the library is not installed, is older, or the path does not fit. `owner` is what
the table came from: the `clap_host_t`, or the `clap_preset_discovery_indexer_t`. A preset
provider's `init`, which has an indexer and no host, can so declare a library's folder as a preset
location. In Oscairn it works from an indexer, from a plugin in the engine's process and from a
plugin in a worker process.

### `bool create(host, const char* file, oscairn_asset_ref_t* ref, char* message, message_capacity)` (draft 3)

Stores a file the plugin made (a recording, an edited wavetable, a converted waveform), given as a
finished file at the absolute path `file`, and fills `ref` with its library, path and SHA-256. A file
already in a registered or installed library is referenced where it is, and must stay there; any
other is copied into the user library (`user`, `<first 16 hex digits of the SHA-256>/<name>`, a
plain file name of at most 255 bytes), after which the plugin may delete its file. The plugin then
keeps `ref` in its state and lists it in `oscairn.asset-dependencies`. False with a reason on
failure.

### `bool resolve_sample(host, const oscairn_asset_ref_t* ref, char* path, path_capacity, oscairn_sample_location_t* location, char* message, message_capacity)` (draft 4)

Draft 4 adds items kept by content and named by their id rather than files. A reference whose
`path` is `sample:<id>` or `waveform:<id>` (64 lowercase hex digits; `library` empty; `sha256` empty
or that id, which is its digest) names a sample in an Oscairn Sample Pack or a waveform
([github.com/oscairn/samplepack](https://github.com/oscairn/samplepack)). `resolve` answers a
waveform reference with the path of its `.owf` file, once every sample it names is installed, and a
sample reference with its pack's path. `resolve_sample` answers a sample reference with where its
record is: the pack's path in `path`, read-only, and in `location` (`oscairn_sample_location_t`:
`offset`, `length`, `preload`) the record's offset, length and preload span (bytes from the
record's start to the end of its preload window: read these first), which with the id make the
`sp_index_entry` the samplepack library's `sp_record_*` functions take. Both stay valid while the
plugin holds the sound prepared: packs are appended to, never rewritten. An in-memory lookup, cheap
enough for every sample of a sound. False with a reason on failure.

From an indexer, `resolve`, `create` and `resolve_sample` are null. Oscairn answers
drafts 1 to 4 with the same table (each draft's struct is a prefix of the next); an indexer answers
drafts 2 to 4, and only when the scan has an asset store.

## Plugin functions (the host calls them)

| Function | Does |
| --- | --- |
| `uint32_t count(plugin)` | How many assets the current state uses (Oscairn reads at most 65,536) |
| `bool get(plugin, index, oscairn_asset_ref_t* ref)` | The index-th; false is an error ("Plugin did not describe an asset it uses") |
| `bool optional(plugin, index)` | Draft 2, may be null: true for an asset the plugin can do without (it keeps a fallback, as an arpeggiator embeds its phrases). Null, or a draft 1 table: every asset required |

**Main thread.** Oscairn asks them when it saves a Performance, after saving the plugin's state,
and records the list in the Performance. A table without `count` or `get` is refused at load
("Incomplete asset-dependency interface").

When a Performance opens, a missing **required** asset stops it loading, naming the asset; a
missing **optional** one gives a notice, and the plugin's `resolve` of it fails, so it uses its
fallback. An exported Performance or Sound carries each recorded asset once, whatever library it
came from, an installed asset library (packed with `oscairn-sign asset-package`) included, with
that library's license and attribution files.

## Patches with asset paths

[`oscairn.sound-contexts`](sound-contexts.md)' `prepare` takes patch text whose asset paths are
relative to `base`, a library folder: the plugin resolves `base->library` with `base->path` joined
to each relative path through `resolve`.

## Licenses of factory content

Factory content packed with `oscairn-sign asset-package` under the publisher's own license
(`LicenseRef-...`) must carry its text (`--license-file`). Users' exports carry its files like any
other library's, with that text: there is no option to keep them out.

## Without it

A plugin that does not use `oscairn.assets` keeps its own paths in its state: the Performance
records no assets for it, they do not travel with exports, and a missing file shows only as the
plugin's own failure. Without `oscairn.asset-dependencies`, nothing is checked before a Performance
opens. LV2 plugins need neither: the files their state maps through `state:mapPath` become asset
references the same way.

## Minimal example

```cpp
#include <oscairn-clap/assets.h>

oscairn_asset_ref_t sample{};  // in the state
std::string samplePath;        // resolved, never saved

bool loadSample(const clap_host_t* host) // main thread, after the state loads
{
    auto* assets = static_cast<const oscairn_host_assets_t*>(host->get_extension(host, OSCAIRN_EXT_ASSETS));
    char path[4096], why[256];
    if (!assets || !assets->resolve(host, &sample, path, sizeof path, why, sizeof why)) return false; // play the fallback
    samplePath = path;
    return true;
}

static const oscairn_plugin_asset_dependencies_t dependencies{
    [](const clap_plugin_t* p) -> uint32_t { return self(p).sample.path[0] ? 1 : 0; },
    [](const clap_plugin_t* p, uint32_t i, oscairn_asset_ref_t* ref) { if (i) return false; *ref = self(p).sample; return true; },
    [](const clap_plugin_t*, uint32_t) { return true; }}; // it has a built-in fallback
```

## Testing

- With a mock host, answer `resolve` from a folder and check the plugin stores the reference (with
  the hash filled in), never the path, and lists it through `get`.
- In Oscairn, `assets.import` puts a file in the user library and returns its reference;
  `assets.libraries` lists the libraries. Save a Performance using the asset and export it:
  `oscairn-plugin-check` installs the export on a fresh device, where the asset must resolve again.
  Remove an optional asset and check the Performance opens with a notice.
- Oscairn's tests: `assets` (the store), `prepared-instances` (`create` from a test plugin),
  `control-archives` (`library_root` from a preset indexer).
