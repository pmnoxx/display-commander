# D3D Debug Layer Addon CMakeLists.txt

# Sources
file(GLOB_RECURSE DXGI_DEBUG_LAYER_SOURCES
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/*.cpp
)

if(NOT DXGI_DEBUG_LAYER_SOURCES STREQUAL "")
  add_library(zzz_dxgi_debug_layer MODULE ${DXGI_DEBUG_LAYER_SOURCES})

  target_include_directories(zzz_dxgi_debug_layer
    PRIVATE
      ${CMAKE_CURRENT_LIST_DIR}/../../../src
      ${CMAKE_CURRENT_LIST_DIR}/../../../include
    SYSTEM PUBLIC
      ${CMAKE_CURRENT_LIST_DIR}/../../../external/reshade/include
      ${CMAKE_CURRENT_LIST_DIR}/../../../external/reshade/deps/minhook/include
  )

  # No static linking to dxgidebug - we load it dynamically at runtime

  # Compile MinHook directly into this module for OutputDebugString hooks
  set(MINHOOK_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../external/reshade/deps/minhook/src")
  if(EXISTS "${MINHOOK_SRC_DIR}/hook.c")
    set(MINHOOK_SRC_FILES
      ${MINHOOK_SRC_DIR}/buffer.c
      ${MINHOOK_SRC_DIR}/hook.c
      ${MINHOOK_SRC_DIR}/trampoline.c
      ${MINHOOK_SRC_DIR}/hde/hde64.c
      ${MINHOOK_SRC_DIR}/hde/hde32.c
    )
    target_sources(zzz_dxgi_debug_layer PRIVATE ${MINHOOK_SRC_FILES})
    # Avoid __declspec(dllimport) from header when statically compiling sources
    target_compile_definitions(zzz_dxgi_debug_layer PRIVATE MH_STATIC=1)
    # When using clang-cl, undefine _MSC_VER for MinHook C sources to force memcpy path instead of __movsb
    if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
      set_source_files_properties(${MINHOOK_SRC_FILES} PROPERTIES COMPILE_OPTIONS "-U_MSC_VER")
    endif()
  else()
    message(WARNING "MinHook sources not found under ${MINHOOK_SRC_DIR}; OutputDebugString capture may be disabled.")
  endif()

  # Output naming
  if(CMAKE_GENERATOR_PLATFORM STREQUAL "Win32" OR CMAKE_EXE_LINKER_FLAGS MATCHES "/machine:X86" OR CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(TARGET_SUFFIX ".addon32")
  else()
    set(TARGET_SUFFIX ".addon64")
  endif()

  set_target_properties(zzz_dxgi_debug_layer PROPERTIES
    SUFFIX "${TARGET_SUFFIX}"
  )

  # Link system GUID library for DXGI_DEBUG_* GUIDs
  target_link_libraries(zzz_dxgi_debug_layer PRIVATE dxguid)
endif()
