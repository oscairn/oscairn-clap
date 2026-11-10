// SPDX-License-Identifier: MIT
// oscairn.param-target: check the lane list, then render and read the events back: they must
// carry the space the event registry returned, one lane each, the right mode and value range.
#include "../mini-host.hpp"

int main(int argc, char** argv)
{
    if(argc < 2) { std::fprintf(stderr, "usage: %s PLUGIN.clap\n", argv[0]); return 2; }
    Plugin plugin(argv[1], "org.oscairn.example.param-target");
    const auto* lanes = plugin.extension<oscairn_plugin_param_target_lanes_t>(OSCAIRN_EXT_PARAM_TARGET_LANES);
    check(lanes && lanes->count && lanes->get, "oscairn.param-target-lanes/draft is offered");
    if(!lanes) return finish("oscairn-example-param-target");
    check(lanes->count(plugin.plugin) == 2, "two lanes");
    oscairn_param_target_lane_info_t info{};
    check(lanes->get(plugin.plugin, 0, &info) && info.lane == 0 && !std::strcmp(info.name, "Slow"), "lane 0 is Slow");
    check(lanes->get(plugin.plugin, 1, &info) && info.lane == 1 && !std::strcmp(info.name, "Fast"), "lane 1 is Fast");
    check(!lanes->get(plugin.plugin, 2, &info), "there is no third lane");

    check(plugin.activate(), "activates and starts processing");
    EventList events;
    check(plugin.process(256, events) == CLAP_PROCESS_CONTINUE, "processes");
    // The mini host hands out space ids from 1; the plugin asked, so the events ride space 1.
    const auto sent = events.in(1);
    check(!sent.empty(), "lane events are pushed on the queried space");
    size_t slow = 0, fast = 0, bad = 0;
    uint32_t lastTime = 0;
    bool ordered = true;
    for(const auto* h : sent)
    {
        if(h->type != OSCAIRN_EVENT_PARAM_TARGET || h->size != sizeof(oscairn_event_param_target_t)) { ++bad; continue; }
        const auto* e = reinterpret_cast<const oscairn_event_param_target_t*>(h);
        if(e->lane == 0)
        {
            ++slow;
            bad += e->mode != OSCAIRN_PARAM_TARGET_MODULATE || std::abs(e->value) > 1;
        }
        else if(e->lane == 1)
        {
            ++fast;
            bad += e->mode != OSCAIRN_PARAM_TARGET_SET || e->value < 0 || e->value > 1;
        }
        else ++bad;
        bad += e->note_id != -1 || e->port_index != -1 || e->channel != -1 || e->key != -1;
        ordered &= e->header.time >= lastTime;
        lastTime = e->header.time;
    }
    check(bad == 0, "every event is a well-formed target event: wildcard note address, in-range value, right mode");
    check(slow == 256 && fast == 256, "a point on every lane at every sample");
    check(ordered, "events arrive in sample order");
    return finish("oscairn-example-param-target");
}
