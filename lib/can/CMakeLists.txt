add_library(can STATIC
    mcp2515.c
)

target_include_directories(can PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(can
    hardware_spi
    hardware_gpio
    pico_stdlib
)
