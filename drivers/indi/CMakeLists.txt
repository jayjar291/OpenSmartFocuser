cmake_minimum_required(VERSION 3.10)
project(indi-opensmartfocuser C CXX)

include(GNUInstallDirs)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")

find_package(INDI REQUIRED)

set(CDRIVER_VERSION_MAJOR 0)
set(CDRIVER_VERSION_MINOR 1)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/indi_opensmartfocuser.xml.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/indi_opensmartfocuser.xml
)

include(CMakeCommon)

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${INDI_INCLUDE_DIR})

add_executable(indi_opensmartfocuser src/OSFocuser_device.cpp)

target_link_libraries(indi_opensmartfocuser ${INDI_LIBRARIES})

install(TARGETS indi_opensmartfocuser RUNTIME DESTINATION bin)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/indi_opensmartfocuser.xml DESTINATION ${INDI_DATA_DIR})

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/99-opensmartfocuser.rules DESTINATION /etc/udev/rules.d)
