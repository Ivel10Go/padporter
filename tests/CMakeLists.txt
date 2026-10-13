add_executable(padporter_tests
    TestMain.cpp
    Fixtures.cpp
    XpjReaderTests.cpp
    WavAndResolverTests.cpp
    MidiWriterTests.cpp
    ExporterTests.cpp
    DeviceLibraryTests.cpp
    PartsTests.cpp
    RealProjectTests.cpp)

target_include_directories(padporter_tests PRIVATE ${doctest_SOURCE_DIR})
target_link_libraries(padporter_tests PRIVATE padporter_core nlohmann_json::nlohmann_json)
target_compile_definitions(padporter_tests PRIVATE
    PADPORTER_TESTPROJECTS_DIR="${PROJECT_SOURCE_DIR}/testprojekte")

add_test(NAME padporter_tests COMMAND padporter_tests)
