cmake_minimum_required(VERSION 3.16)
project(driver_restart)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools)

# Create the executable
add_executable(driver_restart
    main.cpp
    process_manager.cpp
    window_manager.cpp
    driver_manager.cpp
    utils.cpp
)

# Set target properties
set_target_properties(driver_restart PROPERTIES
    WIN32_EXECUTABLE TRUE
    OUTPUT_NAME "driver_restart"
)

# Link required libraries
target_link_libraries(driver_restart
    setupapi
    psapi
    shell32
    advapi32
)

# Set subsystem to console for command line interface
set_target_properties(driver_restart PROPERTIES
    LINK_FLAGS "/SUBSYSTEM:CONSOLE"
)

# Copy to build directory for easy access
add_custom_command(TARGET driver_restart POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:driver_restart> ${CMAKE_BINARY_DIR}/tools/
    COMMENT "Copying driver_restart to tools directory"
)
