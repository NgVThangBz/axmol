

if(BUILD_SHARED_LIBS)
  cmake_policy(SET CMP0079 NEW)

  # Enable -fPIC flag when compiling as shared libs...
  if(UNIX AND NOT ANDROID)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  endif()
endif()

function(setup_ax_extension_config target_name)
  set(options DNTLINK)
  set(oneValueArgs LINK_SCOPE)
  cmake_parse_arguments(opt "${options}" "${oneValueArgs}" "" ${ARGN})

  target_link_libraries(${target_name} ${opt_LINK_SCOPE} ${_AX_CORE_LIB})

  if(BUILD_SHARED_LIBS)
    target_compile_definitions(${target_name} PRIVATE AX_EX_DLLEXPORT INTERFACE AX_EX_DLLIMPORT)
  endif()

  set_target_properties(${target_name}
    PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    FOLDER "Extensions"
  )

  if(WINRT)
    set_property(TARGET ${target_name} PROPERTY VS_PROJECT_IMPORT ${CMAKE_BINARY_DIR}/packages/Microsoft.Windows.CppWinRT/build/native/Microsoft.Windows.CppWinRT.props)
  endif()

  if(NOT opt_DNTLINK)
    set(_AX_EXTENSION_LIBS "${target_name};${_AX_EXTENSION_LIBS}" CACHE INTERNAL "extensions for auto link to target application")
  endif()

  if(XCODE OR VS)
    ax_mark_code_files("${target_name}")
  endif()
endfunction()


if(AX_ENABLE_EXT_ASSETMANAGER)
  add_subdirectory(assets-manager)
endif()

if(AX_ENABLE_EXT_JSONDEFAULT)
  add_subdirectory(JSONDefault)
endif()

# Disable -fPIC flag for other libs...
if(BUILD_SHARED_LIBS AND UNIX AND NOT ANDROID)
  set(CMAKE_POSITION_INDEPENDENT_CODE OFF)
endif()

message(STATUS "Enabled ${_AX_CORE_LIB} extensions:${_AX_EXTENSION_LIBS}")
