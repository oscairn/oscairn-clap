# SPDX-License-Identifier: MIT
# One minimal example plugin per Oscairn CLAP extension, its self-test, and (when
# OSCAIRN_PLUGIN_CHECK points at a built binary) a plugin-check test.
find_package(Threads REQUIRED)

# What a host loads for TARGET: on macOS the .clap bundle directory, not the binary inside it.
function(oscairn_loadable_path out target)
    if(APPLE)
        set(${out} $<TARGET_BUNDLE_DIR:${target}> PARENT_SCOPE)
    else()
        set(${out} $<TARGET_FILE:${target}> PARENT_SCOPE)
    endif()
endfunction()

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
    target_compile_options(${target}-selftest PRIVATE -Wall -Wextra)
    add_test(NAME ${target}-selftest COMMAND ${target}-selftest $<TARGET_FILE:${target}>)
    set_tests_properties(${target}-selftest PROPERTIES TIMEOUT 30 LABELS self-test)

    if(OSCAIRN_PLUGIN_CHECK)
        oscairn_loadable_path(checkPath ${target})
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
oscairn_add_example(oscairn-example-instance-sharing instance-sharing org.oscairn.example.instance-sharing instance-sharing/instance_sharing.cpp synth 1=0.1)

# Parts sharing one instance, against a running Oscairn: two parts of this plugin land on one
# instance, each with its own sound, channel and bus (graph.get's per-node `shared`). Only
# registered when OSCAIRN_JSON_RPC_TEST points at a built oscairn-json-rpc-test; labeled so CI
# can exclude it (it needs a built Oscairn, like plugin-check).
if(OSCAIRN_JSON_RPC_TEST)
    find_package(Python3 COMPONENTS Interpreter)
endif()
if(OSCAIRN_JSON_RPC_TEST AND Python3_Interpreter_FOUND)
    oscairn_loadable_path(sharePath oscairn-example-instance-sharing)
    add_test(NAME oscairn-example-instance-sharing-oscairn
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/instance-sharing/oscairn-sharing.py"
                "${OSCAIRN_JSON_RPC_TEST}" "${sharePath}")
    set_tests_properties(oscairn-example-instance-sharing-oscairn PROPERTIES TIMEOUT 300 LABELS oscairn-sharing)
endif()
