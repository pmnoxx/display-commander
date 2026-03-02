# Enhanced ReShade Injector CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

project(tools)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enhanced Injector
add_executable(enhanced_injector
    enhanced_injector.cpp
)

set_target_properties(enhanced_injector PROPERTIES
    OUTPUT_NAME "enhanced_injector"
)

target_link_libraries(enhanced_injector
    advapi32
    kernel32
    user32
    shell32
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/injector_config.ini
    ${CMAKE_CURRENT_BINARY_DIR}/injector_config.ini
    COPYONLY
)

install(TARGETS enhanced_injector
    RUNTIME DESTINATION bin
)

install(FILES injector_config.ini
    DESTINATION bin
)

# Game Commander
add_subdirectory(game_commander)

# Unused API check (requires cppcheck and compile_commands.json from a configured build)
# Configure with: cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_EXECUTABLE)
  set(ADDON_BUILD_DIR "${CMAKE_BINARY_DIR}/..")
  add_custom_target(find_unused_api
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/find_unused_api.py" "${ADDON_BUILD_DIR}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Find unused functions in addon (cppcheck)"
    USES_TERMINAL
  )
endif()
