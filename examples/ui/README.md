# ui — `oscairn.ui` draft 5

A sine synth whose six parameters carry editor hints: roles (`Cutoff`, `Level`,
`Attack`, `Decay`, `Sustain`, `Release`), a stage group (Attack/Decay/Sustain/Release
draw joined), a rich ADSR view, and `seconds()` — how long each envelope stage lasts
at a value, so the host can draw stage times instead of raw values.

**Build:** `cmake --build build --target oscairn-example-ui`

**In Oscairn:** load it on a Performance part, open its editor, and connect a
controller: Brightness lands on the Filter bank's first knob, the envelope knobs on
the Amp envelope's, because the roles match. The ADSR view shows stage durations in
seconds from `seconds()`, not the knob values.

**Self-test:** `ctest --test-dir build -R oscairn-example-ui` reads every hint back
through the extension, checks group membership, the view, and the stage times.
