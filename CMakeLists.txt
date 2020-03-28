cmake_minimum_required(VERSION 3.0)
project(create_website_helper)

set(MY_PROJECT_DEPENDENCIES boost-core boost-iostreams boost-format zlib)

if(NOT DEFINED ${CMAKE_TOOLCHAIN_FILE})
  if(NOT DEFINED ENV{VCPKG_ROOT})
    if(WIN32)
      set(VCPKG_ROOT $ENV{HOMEDRIVE}/vcpkg)
    else()
      set(VCPKG_ROOT $ENV{HOME}/.vcpkg)
    endif()
  else()
    set(VCPKG_ROOT $ENV{VCPKG_ROOT})
  endif()

  if(NOT EXISTS ${VCPKG_ROOT})
    message("Cloning vcpkg in ${VCPKG_ROOT}")
    execute_process(COMMAND git clone https://github.com/Microsoft/vcpkg.git ${VCPKG_ROOT})
  else()
    message("Auto-updating vcpkg in ${VCPKG_ROOT}")
    execute_process(COMMAND git pull WORKING_DIRECTORY ${VCPKG_ROOT})
  endif()

  if(NOT EXISTS ${VCPKG_ROOT}/README.md)
    message(FATAL_ERROR "***** FATAL ERROR: Could not clone vcpkg *****")
  endif()

  if(WIN32)
    set(BOOST_INCLUDEDIR ${VCPKG_ROOT}/installed/x86-windows/include)
    set(VCPKG_EXEC ${VCPKG_ROOT}/vcpkg.exe)
    set(VCPKG_BOOTSTRAP ${VCPKG_ROOT}/bootstrap-vcpkg.bat)
  else()
    set(VCPKG_EXEC ${VCPKG_ROOT}/vcpkg)
    set(VCPKG_BOOTSTRAP ${VCPKG_ROOT}/bootstrap-vcpkg.sh)
  endif()

  if(NOT EXISTS ${VCPKG_EXEC})
    message("Bootstrapping vcpkg in ${VCPKG_ROOT}")
    execute_process(COMMAND ${VCPKG_BOOTSTRAP} WORKING_DIRECTORY ${VCPKG_ROOT})
  endif()

  if(NOT EXISTS ${VCPKG_EXEC})
    message(FATAL_ERROR "***** FATAL ERROR: Could not bootstrap vcpkg *****")
  endif()

  set(CMAKE_TOOLCHAIN_FILE ${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake CACHE STRING "")

  message(STATUS "***** Checking project third party dependencies in ${VCPKG_ROOT} *****")
  execute_process(COMMAND ${VCPKG_EXEC} install ${MY_PROJECT_DEPENDENCIES} WORKING_DIRECTORY ${VCPKG_ROOT})
endif()

find_package(Boost COMPONENTS REQUIRED iostreams)

add_executable(create_website_helper create_website_helper.cpp)
target_link_libraries(create_website_helper ${Boost_LIBRARIES})
target_include_directories(create_website_helper PUBLIC ${Boost_INCLUDE_DIR} ${PROJECT_SOURCE_DIR}/include)