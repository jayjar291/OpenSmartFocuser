cmake_minimum_required(VERSION 3.10)

# OpenSmartFocuser INDI driver scaffold
# Placeholder-only project skeleton based on INDI project setup docs.
project(indi-opensmartfocuser-focuser C CXX)

include(GNUInstallDirs)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")

find_package(INDI REQUIRED)
find_package(Nova REQUIRED)
find_package(ZLIB REQUIRED)
find_package(GSL REQUIRED)

set(CDRIVER_VERSION_MAJOR 0)
set(CDRIVER_VERSION_MINOR 1)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/config.h
)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/indi_opensmartfocuser_focuser.xml.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/indi_opensmartfocuser_focuser.xml
)

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${INDI_INCLUDE_DIR})
include_directories(${NOVA_INCLUDE_DIR})
include_directories(${EV_INCLUDE_DIR})

include(CMakeCommon)

add_executable(
  indi_opensmartfocuser_focuser
  indi_opensmartfocuser_focuser.cpp
)

target_link_libraries(
  indi_opensmartfocuser_focuser
  ${INDI_LIBRARIES}
  ${NOVA_LIBRARIES}
  ${GSL_LIBRARIES}
)

install(TARGETS indi_opensmartfocuser_focuser RUNTIME DESTINATION bin)

install(
  FILES
  ${CMAKE_CURRENT_BINARY_DIR}/indi_opensmartfocuser_focuser.xml
  DESTINATION ${INDI_DATA_DIR}
)
