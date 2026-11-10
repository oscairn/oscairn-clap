# param-target — `oscairn.param-target`

A modulator with no sound of its own: two output lanes (Slow, Fast) that send an
`oscairn_event_param_target` on every sample. The host routes each (plugin, lane) pair to
the parameter the user mapped and turns it into a CLAP parameter event on the target at
the same sample; the plugin never knows what it controls. Lanes are listed through
`oscairn.param-target-lanes/draft` so Routing can offer them. On a host without the event
space, the registry query answers false and nothing is sent.

**Build:** `cmake --build build --target oscairn-example-param-target`

**In Oscairn:** in Routing, connect one of its lanes to another plugin's parameter; Rate
and Depth shape the modulation the target receives.

**Self-test:** `ctest --test-dir build -R oscairn-example-param-target` — the lane list,
then one event per lane per sample on the queried space, in order, with the wildcard note
address and each lane's mode and value range.
