cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
  C_STANDARD 11
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

if(APPLE)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    "-framework ApplicationServices"
    "-framework CoreFoundation"
    "-framework CoreGraphics"
  )
endif()

if(UNIX AND NOT APPLE)
  find_package(X11 REQUIRED COMPONENTS Xrandr Xi)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE X11::X11 X11::Xrandr X11::Xi)
endif()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
  src/caret-locator.cpp
  src/caret-locator.hpp
  src/focus-blur-filter.cpp
  src/focus-blur-filter.hpp
  src/dim-mask-filter.cpp
  src/dim-mask-filter.hpp
  src/tilt-filter.cpp
  src/tilt-filter.hpp
  src/plugin-main.cpp
  src/zoominator-controller.cpp
  src/zoominator-frame.cpp
  src/frame-source.cpp
  src/cam-ring-filter.cpp
  src/zoominator-camring.cpp
  src/zoominator-hotkeys.cpp
  src/zoominator-input.cpp
  src/zoominator-marker.cpp
  src/zoominator-internal.hpp
  src/zoominator-overlays.cpp
  src/zoominator-recovery.cpp
  src/zoominator-sceneitems.cpp
  src/zoominator-screen.cpp
  src/zoominator-settings.cpp
  src/zoominator-sound.cpp
  src/zoominator-triggers.cpp
  src/zoominator-controller.hpp
  src/zoominator-dialog.cpp
  src/zoominator-dialog.hpp
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
