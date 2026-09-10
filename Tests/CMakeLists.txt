add_executable(brownsweep_tests
    TestMain.cpp
    TestDspBlocks.cpp
    TestEngine.cpp
    TestPerceptual.cpp)

target_link_libraries(brownsweep_tests PRIVATE brownsweep_dsp)
target_compile_features(brownsweep_tests PRIVATE cxx_std_17)

add_test(NAME brownsweep_tests COMMAND brownsweep_tests)
