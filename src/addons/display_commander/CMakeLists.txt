# Display Commander Addon CMakeLists.txt

# Sources
file(GLOB_RECURSE DISPLAY_COMMANDER_SOURCES
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/*.cpp
  ${CMAKE_CURRENT_LIST_DIR}/utils/*.cpp
)

if(DISPLAY_COMMANDER_SOURCES STREQUAL "")
  message(FATAL_ERROR "No sources found under src/addons/display_commander")
endif()

add_library(zzz_display_commander MODULE ${DISPLAY_COMMANDER_SOURCES})

# Compile definitions
if(EXPERIMENTAL_TAB)
  target_compile_definitions(zzz_display_commander PRIVATE EXPERIMENTAL_TAB=1)
endif()

if(EXPERIMENTAL_FEATURES)
  target_compile_definitions(zzz_display_commander PRIVATE EXPERIMENTAL_FEATURES=1)
endif()

# Add git commit count as build number
target_compile_definitions(zzz_display_commander PRIVATE GIT_COMMIT_COUNT=${GIT_COMMIT_COUNT})

# Add build date and time as compile definitions
target_compile_definitions(zzz_display_commander PRIVATE ${BUILD_DATE_DEFINE})
target_compile_definitions(zzz_display_commander PRIVATE ${BUILD_TIME_DEFINE})

# MSVC-specific flags
if(MSVC)
  target_compile_options(zzz_display_commander PRIVATE /bigobj /arch:SSE2)
  # Enable SSE4.1 and SSE4.2 for better performance
  target_compile_options(zzz_display_commander PRIVATE /arch:AVX)
  # Enable MWAITX for AMD Zen architecture support
  target_compile_options(zzz_display_commander PRIVATE /arch:AVX2)
  # Enable MWAITX specifically
  target_compile_options(zzz_display_commander PRIVATE /D_ENABLE_EXTENDED_ALIGNED_STORAGE)
  # Suppress Microsoft extension warnings for function pointer casting (common with MinHook)
  target_compile_options(zzz_display_commander PRIVATE /wd4191)
  # Use UTF-8 encoding for source files to support Unicode characters (fixes C4566 warning)
  target_compile_options(zzz_display_commander PRIVATE /utf-8)

  # Use static runtime linking to avoid dependency on debug runtime DLLs
  set_property(TARGET zzz_display_commander PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

  # Explicitly enable debug symbols for all build types (especially RelWithDebInfo)
  # /Zi generates full debug information in a program database (PDB)
  # /DEBUG generates debug information for the linker
  target_compile_options(zzz_display_commander PRIVATE
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:/Zi>
    $<$<CONFIG:Release>:/Zi>  # Also generate symbols in Release for crash analysis
  )
  target_link_options(zzz_display_commander PRIVATE
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:/DEBUG>
    $<$<CONFIG:Release>:/DEBUG>  # Also generate debug info in Release for crash analysis
  )

  # Ensure PDB file is placed alongside the DLL for easier debugging
  set_target_properties(zzz_display_commander PROPERTIES
    PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    PDB_NAME "zzz_display_commander"
  )
endif()

# GCC/Clang-specific flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(zzz_display_commander PRIVATE -msse4.1 -msse4.2)
  # Enable AVX for even better performance if available
  target_compile_options(zzz_display_commander PRIVATE -mavx)
  # Enable MWAITX for AMD Zen architecture
  target_compile_options(zzz_display_commander PRIVATE -mavx2 -mmwaitx)
  # Suppress Microsoft extension warnings for function pointer casting (common with MinHook)
  target_compile_options(zzz_display_commander PRIVATE -Wno-microsoft-cast)
endif()

# Include directories
target_include_directories(zzz_display_commander
  PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../../../src
  ${CMAKE_CURRENT_LIST_DIR}/../../../src/addons/display_commander
  ${CMAKE_CURRENT_LIST_DIR}/../../../include
  SYSTEM PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/reshade/include  # provides ReShade/reshade.hpp
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/reshade          # provides res/fonts/forkawesome.h
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/reshade/deps/imgui # provides imgui.h
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/Streamline/include # Streamline headers if referenced
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/nvapi              # official NVIDIA NVAPI headers (nvapi.h, NvApiDriverSettings.h)
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/nvidia-dlss/include # official NVIDIA DLSS/NGX headers
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/minhook/include    # MinHook headers
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/vulkan-headers/include # official Khronos Vulkan headers
)

# System libraries removed (xinput, dinput8, winmm, dbghelp) to avoid DLL dependency issues - they are loaded dynamically
# Version library is needed for GetFileVersionInfo functions
# HID libraries are needed for HID device enumeration and management
# DXGI library is needed for DXGI interface IIDs
# ETW libraries (tdh/advapi32) are needed for PresentMon-style tracing
# WinInet library is needed for HTTP downloads (shader packages list)
# Note: Some toolchains (e.g. clang-cl without full Windows SDK lib path) may not have evntrace.lib on the link path.
target_link_libraries(zzz_display_commander PRIVATE version setupapi hid dxgi tdh advapi32 wininet)

# NVAPI static linking (official NVIDIA NVAPI layout: x86/ and amd64/)
# NVIDIA recommends static linking - these are static libraries that act as shims
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../../external/nvapi")
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../../external/nvapi/x86/nvapi.lib")
      target_link_libraries(zzz_display_commander PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../../external/nvapi/x86/nvapi.lib")
      message(STATUS "NVAPI x86 static library linked successfully")
    else()
      message(WARNING "NVAPI x86 static library not found at ${CMAKE_CURRENT_LIST_DIR}/../../../external/nvapi/x86/nvapi.lib. NVAPI features may fail to link.")
    endif()
  else()
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../../external/nvapi/amd64/nvapi64.lib")
      target_link_libraries(zzz_display_commander PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../../external/nvapi/amd64/nvapi64.lib")
      message(STATUS "NVAPI x64 static library linked successfully")
    else()
      message(WARNING "NVAPI x64 static library not found at ${CMAKE_CURRENT_LIST_DIR}/../../../external/nvapi/amd64/nvapi64.lib. NVAPI features may fail to link.")
    endif()
  endif()
else()
  message(WARNING "NVAPI not found at ${CMAKE_CURRENT_LIST_DIR}/../../../external/nvapi. Clone https://github.com/NVIDIA/nvapi into external/nvapi if you need NVAPI features.")
endif()

# MinHook library
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../../external/minhook")
  # MinHook source files
  set(MINHOOK_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/minhook/src/buffer.c
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/minhook/src/hook.c
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/minhook/src/trampoline.c
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/minhook/src/hde/hde32.c
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/minhook/src/hde/hde64.c
  )

  # Create MinHook static library
  add_library(minhook STATIC ${MINHOOK_SOURCES})

  # MinHook compile definitions
  target_compile_definitions(minhook PRIVATE MH_STATIC=1)

  # MinHook include directories
  target_include_directories(minhook PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../../external/minhook/include)

  # When using clang-cl, undefine _MSC_VER for MinHook C sources to force memcpy path instead of __movsb
  if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set_source_files_properties(${MINHOOK_SOURCES} PROPERTIES COMPILE_OPTIONS "-U_MSC_VER")
  endif()

  # Use static runtime linking for MinHook as well
  if(MSVC)
    set_property(TARGET minhook PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()

  # Link MinHook to display commander
  target_link_libraries(zzz_display_commander PRIVATE minhook)

  message(STATUS "MinHook library configured successfully")
else()
  message(FATAL_ERROR "MinHook not found at ${CMAKE_CURRENT_LIST_DIR}/../../../external/minhook. Run: git submodule update --init --recursive")
endif()

# Output naming
if(CMAKE_GENERATOR_PLATFORM STREQUAL "Win32" OR CMAKE_EXE_LINKER_FLAGS MATCHES "/machine:X86" OR CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(TARGET_SUFFIX ".addon32")
else()
  set(TARGET_SUFFIX ".addon64")
endif()

set_target_properties(zzz_display_commander PROPERTIES
  SUFFIX "${TARGET_SUFFIX}"
  LINK_OPTIONS "$<$<C_COMPILER_ID:MSVC>:/DEF:${CMAKE_CURRENT_LIST_DIR}/proxy_dll/exports.def>"
)

# Link exports.def file for proxy DLL exports (dxgi.dll, d3d11.dll, d3d12.dll, version.dll)
# CMake automatically handles .def files for MSVC when added as a source, but we also explicitly link it
target_sources(zzz_display_commander PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/proxy_dll/exports.def
)
