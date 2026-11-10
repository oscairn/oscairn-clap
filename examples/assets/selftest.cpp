// SPDX-License-Identifier: MIT
// oscairn.assets + oscairn.asset-dependencies: keep an asset reference in the state, list it as
// a dependency, resolve it through the host at load, and keep working when the resolve fails.
#include "../mini-host.hpp"

#include <cstdio>
#include <unistd.h> // mkdtemp

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
    {
        const std::string curve = std::string(dir) + "/curves";
        std::system(("mkdir -p '" + curve + "'").c_str());
        const double table[8]{1.5, 1.4, 1.3, 1.2, 1.1, 1.0, .9, .8};
        FILE* file = std::fopen((curve + "/bright.bin").c_str(), "wb");
        check(file && std::fwrite(table, sizeof table, 1, file) == 1, "wrote the curve file the reference names");
        std::fclose(file);
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
    check(!deps->get(plugin.plugin, 1, &ref), "there is no second dependency");

    // Round-trip through the state, and the mini host's resolve fills the hash in.
    OutStream saved;
    check(state->save(plugin.plugin, &saved.stream), "the state saves");
    check(saved.data.find("curves/bright.bin") != std::string::npos, "the saved state carries the reference");
    check(plugin.activate(), "activates");
    float in[2][256], out[2][256]{};
    for(uint32_t f = 0; f < 256; ++f) in[0][f] = in[1][f] = .3f;
    const float* input[]{in[0], in[1]};
    float* output[]{out[0], out[1]};
    EventList events;
    plugin.process(256, events, output, input);
    double changed = 0;
    for(uint32_t f = 0; f < 256; ++f) changed = std::max(changed, double(std::abs(out[0][f])));
    check(changed > 1e-4, "it still shapes sound with the fallback curve when the file was unreadable");

    // And it can produce an asset of its own through create(): what the host returns becomes its
    // dependency. The mini host stores it under the user library.
    oscairn_asset_ref_t made{};
    char message[128]{};
    const auto* assets = static_cast<const oscairn_host_assets_t*>(plugin.host.host.get_extension(&plugin.host.host, OSCAIRN_EXT_ASSETS));
    check(assets && assets->create && assets->create(&plugin.host.host, "/tmp/curve.bin", &made, message, sizeof message) &&
              !std::strcmp(made.library, "user"),
          "create stores a produced file and returns its reference");
    std::system(("rm -rf '" + plugin.host.assetRoot + "'").c_str());
    return finish("oscairn-example-assets");
}
