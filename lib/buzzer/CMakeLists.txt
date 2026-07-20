add_library(buzzer STATIC
    buzzer.c
)

target_include_directories(buzzer PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(buzzer
    hardware_gpio
    hardware_pwm
)
