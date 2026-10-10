cmake_minimum_required(VERSION 3.10)
# This project builds a single C++ INDI executable with a small CMake support layer.
project(indi-opensmartfocuser C CXX)

# Use platform-standard install locations such as bin and share.
include(GNUInstallDirs)

# Prefer the INDI finder shipped with this driver over CMake's built-in modules.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")

# Require INDI headers/libraries because the executable subclasses INDI devices.
find_package(INDI REQUIRED)

# Keep the executable's reported version and generated driver manifest consistent.
set(CDRIVER_VERSION_MAJOR 0)
set(CDRIVER_VERSION_MINOR 1)

# Generate build-local headers and indiserver metadata from configured values.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/indi_opensmartfocuser.xml.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/indi_opensmartfocuser.xml
)

include(CMakeCommon)

# Resolve generated, project, and INDI headers during compilation.
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${INDI_INCLUDE_DIR})

# The live driver is split between INDI lifecycle/commands and serial framing.
# Empty add-on and star-map placeholders are intentionally not linked here.
add_executable(indi_opensmartfocuser
  src/OSFocuser_device.cpp
  src/protocol.cpp
)

# Link the INDI driver API used by INDI::Focuser and the executable entry point.
target_link_libraries(indi_opensmartfocuser ${INDI_LIBRARIES})

# Install the runnable driver into the platform's binary directory.
install(TARGETS indi_opensmartfocuser RUNTIME DESTINATION bin)

# Install generated driver discovery metadata where indiserver searches for it.
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/indi_opensmartfocuser.xml DESTINATION ${INDI_DATA_DIR})

# Install the USB identity rule that creates the stable /dev/OSF device path.
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/99-opensmartfocuser.rules DESTINATION /etc/udev/rules.d)
