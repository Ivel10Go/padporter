add_executable(padporter-cli main.cpp)
target_link_libraries(padporter-cli PRIVATE padporter_core)

if(MSVC)
    target_compile_options(padporter-cli PRIVATE /W4)
endif()
