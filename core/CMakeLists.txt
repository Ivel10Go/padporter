configure_file(include/padporter/Version.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/generated/padporter/Version.h @ONLY)

add_library(padporter_core STATIC
    src/PathUtil.cpp
    src/Gzip.cpp
    src/Model.cpp
    src/XpjReader.cpp
    src/WavInfo.cpp
    src/SampleResolver.cpp
    src/MidiWriter.cpp
    src/Exporter.cpp
    src/DeviceLibrary.cpp
    src/Parts.cpp)

target_include_directories(padporter_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated)

target_link_libraries(padporter_core PRIVATE nlohmann_json::nlohmann_json padporter_miniz)
target_compile_features(padporter_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(padporter_core PRIVATE /W4)
endif()
