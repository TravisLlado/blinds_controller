add_library(tmc2209_pico
    src/TMC2209/TMC2209.cpp)

target_link_libraries(tmc2209_pico
        pico_stdlib
)

target_include_directories(tmc2209_pico PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})