# Enhanced ReShade Injector CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

project(enhanced_injector)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Create the enhanced injector executable
add_executable(enhanced_injector
    enhanced_injector.cpp
)

# Set target properties
set_target_properties(enhanced_injector PROPERTIES
    OUTPUT_NAME "enhanced_injector"
)

# Link required libraries
target_link_libraries(enhanced_injector
    advapi32
    kernel32
    user32
    shell32
)

# Copy configuration file to build directory
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/injector_config.ini
    ${CMAKE_CURRENT_BINARY_DIR}/injector_config.ini
    COPYONLY
)

# Install the executable and config file
install(TARGETS enhanced_injector
    RUNTIME DESTINATION bin
)

install(FILES injector_config.ini
    DESTINATION bin
)
