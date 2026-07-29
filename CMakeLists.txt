cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME 26NUEDC_stm32_template)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Refactored production modules and the application composition root.
    User/app/app.c
    User/bsp/bsp_board.c
    User/bsp/bsp_board_stm32_callbacks.c
    User/domain/line_pattern.c
    User/domain/motion_planner.c
    User/domain/speed_controller.c
    User/drivers/driver_encoder.c
    User/drivers/driver_camera_protocol.c
    User/drivers/driver_gpio_output.c
    User/drivers/driver_gyro_protocol.c
    User/drivers/driver_keys.c
    User/drivers/driver_motor.c
    User/drivers/driver_oled.c
    User/drivers/driver_sensor_mcu.c
    User/drivers/driver_servo.c
    User/drivers/driver_stepper.c
    User/drivers/driver_ultrasonic.c
    User/drivers/driver_uart_stream.c
    User/libraries/oled_data.c
    User/libraries/cascaded_pid.c
    User/libraries/pid.c
    User/services/command_service.c
    User/services/bluetooth_service.c
    User/services/display.c
    User/services/error_service.c
    User/services/menu_service.c
    User/services/parameter_service.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    User/app
    User/bsp
    User/drivers
    User/libraries
    User/domain
    User/services
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
