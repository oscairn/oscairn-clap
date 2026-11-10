// SPDX-License-Identifier: MIT
// oscairn.assets + oscairn.asset-dependencies: keep an asset reference in the state, list it as
// a dependency, resolve it through the host at load, and fall back when the file is gone.
#include "../mini-host.hpp"

#include <cstdio>
#include <sys/stat.h> // mkdir
#include <unistd.h>   // mkdtemp, rmdir

int main(int argc, char** argv)
{
    if(argc < 2) { std::fprintf(stderr, "usage: %s PLUGIN.clap\n", argv[0]); return 2; }
    Plugin plugin(argv[1], "org.oscairn.example.assets");
    const auto* deps = plugin.extension<oscairn_plugin_asset_dependencies_t>(OSCAIRN_EXT_ASSET_DEPENDENCIES);
    const auto* state = plugin.extension<clap_plugin_state_t>(CLAP_EXT_STATE);
    check(deps && deps->count && deps->get && deps->optional, "oscairn.asset-dependencies/draft-2 is offered complete");
    if(!deps) return finish("oscairn-example-assets");

    check(deps->count(plugin.plugin) == 0, "a fresh instance with no state uses no assets");

    // The host's resolve hands out files under a root the test controls: put the curve there.
    char root[] = "/tmp/oscairn-assets-XXXXXX";
    const char* dir = mkdtemp(root);
    check(dir != nullptr, "made a scratch library folder");
    plugin.host.assetRoot = dir;
    const std::string curve = std::string(dir) + "/curves/bright.bin";
    {
        mkdir((std::string(dir) + "/curves").c_str(), 0700);
        const double table[8]{1.5, 1.4, 1.3, 1.2, 1.1, 1.0, .9, .8};
        FILE* file = std::fopen(curve.c_str(), "wb");
        check(file && std::fwrite(table, sizeof table, 1, file) == 1, "wrote the curve file the reference names");
        if(file) std::fclose(file);
    }
    // A state naming a library asset: library + path, no hash yet.
    {
        InStream input{std::string("oscairn-example-assets-1 0.75") + '\0' + "tables" + '\0' + "curves/bright.bin" + '\0' + '\0', 0};
        check(state->load(plugin.plugin, &input.stream), "the state naming an asset loads");
    }
    check(deps->count(plugin.plugin) == 1, "the asset is listed as a dependency");
    oscairn_asset_ref_t ref{};
    check(deps->get(plugin.plugin, 0, &ref) && !std::strcmp(ref.library, "tables") && !std::strcmp(ref.path, "curves/bright.bin"),
          "the reference names its library and path, never a local path");
    check(deps->optional(plugin.plugin, 0), "it is optional: the plugin keeps a fallback");
    check(ref.sha256[0] != 0, "the hash the host's resolve filled in is kept");
    check(!deps->get(plugin.plugin, 1, &ref), "there is no second dependency");

    // The reference survives the state; nothing local goes into it.
    OutStream saved;
    check(state->save(plugin.plugin, &saved.stream), "the state saves");
    check(saved.data.find("curves/bright.bin") != std::string::npos && saved.data.find(dir) == std::string::npos,
          "the saved state carries the reference, never the local path");

    // The resolved curve shapes the sound; with the file gone, the same state loads, still lists
    // the asset, and the built-in curve takes over.
    check(plugin.activate(), "activates");
    float in[2][256], out[2][256]{};
    for(uint32_t f = 0; f < 256; ++f) in[0][f] = in[1][f] = .3f;
    const float* input[]{in[0], in[1]};
    float* output[]{out[0], out[1]};
    EventList events;
    plugin.process(256, events, output, input);
    const float withFile = out[0][0];
    std::remove(curve.c_str());
    InStream again{saved.data, 0};
    check(state->load(plugin.plugin, &again.stream), "with the file gone the state still loads");
    check(deps->count(plugin.plugin) == 1, "and still names the asset, so the host can report it missing");
    plugin.process(256, events, output, input);
    check(out[0][0] != 0 && out[0][0] != withFile, "the built-in curve takes over from the missing file's");
    rmdir((std::string(dir) + "/curves").c_str());
    rmdir(dir);
    return finish("oscairn-example-assets");
}
