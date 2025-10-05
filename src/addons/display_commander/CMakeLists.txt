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
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/reshade/deps/imgui # provides imgui.h
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/Streamline/include # Streamline headers if referenced
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/nvapi              # official NVIDIA NVAPI headers (nvapi.h, NvApiDriverSettings.h)
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/minhook/include    # MinHook headers
)

# System libraries removed (xinput, dinput8, and winmm) to avoid DLL dependency issues - they are loaded dynamically

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
)
