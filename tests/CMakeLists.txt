juce_add_console_app(viibedealer_tests
    PRODUCT_NAME "viibedealer_tests")

set(VBD_TEST_SOURCES
    TestMain.cpp
    test_params.cpp
    test_processor.cpp
    test_stft.cpp
    test_engine.cpp
    test_fractal.cpp
    test_delays.cpp
    test_gui.cpp
    test_presets.cpp)

vbd_strict_sources(${VBD_TEST_SOURCES})

target_sources(viibedealer_tests PRIVATE ${VBD_TEST_SOURCES})

target_compile_definitions(viibedealer_tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

# Links the shared-code library produced by juce_add_plugin, so the tests exercise the real
# processor with the real plugin defines rather than a recompiled copy of it.
target_link_libraries(viibedealer_tests PRIVATE
    viibedealer
    juce::juce_recommended_config_flags)

add_test(NAME viibedealer_tests COMMAND viibedealer_tests)
