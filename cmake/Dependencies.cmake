# All third-party code is fetched at configure time; nothing is vendored into the repo.
include(FetchContent)

# nlohmann/json (MIT) - JSON parsing in the core library
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# miniz (MIT) - gzip/deflate. SOURCE_SUBDIR points to a non-existent folder so that
# only the amalgamated sources are downloaded and we build them ourselves below.
FetchContent_Declare(miniz
    URL https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
    SOURCE_SUBDIR _not_used
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_MakeAvailable(nlohmann_json miniz)

add_library(padporter_miniz STATIC ${miniz_SOURCE_DIR}/miniz.c)
target_include_directories(padporter_miniz PUBLIC ${miniz_SOURCE_DIR})
target_compile_definitions(padporter_miniz PUBLIC
    MINIZ_NO_ZLIB_COMPATIBLE_NAMES
    MINIZ_NO_ARCHIVE_APIS
    MINIZ_NO_TIME)

if(PADPORTER_BUILD_TESTS)
    # doctest (MIT) - header-only test framework
    FetchContent_Declare(doctest
        URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.12.tar.gz
        SOURCE_SUBDIR _not_used
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(doctest)
endif()

if(PADPORTER_BUILD_PLUGIN)
    # JUCE 8 (AGPLv3 / commercial) - app + VST3 plugin
    FetchContent_Declare(JUCE
        URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.16-security.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()
