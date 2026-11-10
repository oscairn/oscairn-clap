# SPDX-License-Identifier: MIT
# One minimal example plugin per Oscairn CLAP extension, its self-test, and (when
# OSCAIRN_PLUGIN_CHECK points at a built binary) a plugin-check test.
find_package(Threads REQUIRED)

# oscairn_add_example TARGET FOLDER ID SOURCE KIND(synth|effect) PARAM
# Builds the plugin as a loadable .clap, adds its self-test (always registered), and a
# plugin-check test (only when OSCAIRN_PLUGIN_CHECK is set; labeled so CI can exclude it).
function(oscairn_add_example target folder id source kind param)
    add_library(${target} MODULE ${source})
    target_link_libraries(${target} PRIVATE oscairn_clap_headers clap)
    target_compile_options(${target} PRIVATE -Wall -Wextra)
    set_target_properties(${target} PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME ${target}
        CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    if(APPLE)
        # A .clap on macOS is a bundle: Oscairn's loader looks inside Contents/MacOS (Internal.h).
        set_target_properties(${target} PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION "clap")
    endif()

    add_executable(${target}-selftest ${folder}/selftest.cpp)
    target_link_libraries(${target}-selftest PRIVATE oscairn_clap_headers clap Threads::Threads ${CMAKE_DL_LIBS})
    target_compile_options(${target}-selftest PRIVATE -Wall -Wextra -Wno-unused-parameter)
    add_test(NAME ${target}-selftest COMMAND ${target}-selftest $<TARGET_FILE:${target}>)
    set_tests_properties(${target}-selftest PROPERTIES TIMEOUT 30 LABELS self-test)

    if(OSCAIRN_PLUGIN_CHECK)
        # plugin-check wants the loadable itself: on macOS the .clap bundle directory, not the binary inside it.
        if(APPLE)
            set(checkPath $<TARGET_BUNDLE_DIR:${target}>)
        else()
            set(checkPath $<TARGET_FILE:${target}>)
        endif()
        if(param)
            add_test(NAME ${target}-plugin-check COMMAND ${OSCAIRN_PLUGIN_CHECK} ${checkPath} ${id} ${kind} --param ${param})
        else()
            add_test(NAME ${target}-plugin-check COMMAND ${OSCAIRN_PLUGIN_CHECK} ${checkPath} ${id} ${kind})
        endif()
        set_tests_properties(${target}-plugin-check PROPERTIES TIMEOUT 600 LABELS plugin-check)
    endif()
endfunction()

oscairn_add_example(oscairn-example-ui ui org.oscairn.example.ui ui/ui.cpp synth 2=0.1)
oscairn_add_example(oscairn-example-memory memory org.oscairn.example.memory memory/memory.cpp effect 1=0.25)
oscairn_add_example(oscairn-example-sound-contexts sound-contexts org.oscairn.example.sound-contexts sound-contexts/sound_contexts.cpp synth 1=0.1)
oscairn_add_example(oscairn-example-sound-snapshots sound-snapshots org.oscairn.example.sound-snapshots sound-snapshots/sound_snapshots.cpp synth 1=0.1)
oscairn_add_example(oscairn-example-voice-budget voice-budget org.oscairn.example.voice-budget voice-budget/voice_budget.cpp synth 1=0.1)
oscairn_add_example(oscairn-example-effect-contexts effect-contexts org.oscairn.example.effect-contexts effect-contexts/effect_contexts.cpp effect 1=0.25)
# No --param: its lanes move other plugins' parameters, not its own audible output (as the in-repo LFO).
oscairn_add_example(oscairn-example-param-target param-target org.oscairn.example.param-target param-target/param_target.cpp synth "")
oscairn_add_example(oscairn-example-beat beat org.oscairn.example.beat beat/beat.cpp synth 1=0.1)
oscairn_add_example(oscairn-example-harmony harmony org.oscairn.example.harmony harmony/harmony.cpp synth 1=0.1)
oscairn_add_example(oscairn-example-assets assets org.oscairn.example.assets assets/assets.cpp effect 1=0.25)
