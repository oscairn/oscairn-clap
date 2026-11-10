# param-target — `oscairn.param-target`

A modulator with no sound of its own: two output lanes (Slow, Fast) that send
`oscairn_event_param_target` events every block. The host routes each
(plugin, lane) pair to whatever parameter the user mapped and turns it into a
CLAP param event on the target at the same sample — the plugin never knows what it
controls. Lanes are listed through `oscairn.param-target-lanes/draft-1` so Routing
can offer "Connect lane to…". On a host without the event space, the registry query
answers false and nothing is sent.

**Build:** `cmake --build build --target oscairn-example-param-target`

**In Oscairn:** in Routing, connect one of its lanes to another plugin's
parameter; Rate and Depth shape the modulation the target receives.

**Self-test:** `ctest --test-dir build -R oscairn-example-param-target` — lane
listing, target events emitted on both lanes with valid mode/key/value, and no
events when the host does not know the event space.
