cmake_minimum_required(VERSION 3.20)
project(dual_tuner_recorder LANGUAGES C VERSION 0.0.1)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR})
find_package(LibSDRplay)
find_package(pigpio REQUIRED)

if (NOT LIBSDRPLAY_FOUND)
    message(FATAL_ERROR "SDRPlay development files not found...")
endif ()
message(STATUS "LIBSDRPLAY_INCLUDE_DIRS - ${LIBSDRPLAY_INCLUDE_DIRS}")
message(STATUS "LIBSDRPLAY_LIBRARIES - ${LIBSDRPLAY_LIBRARIES}")

set(CMAKE_BUILD_TYPE Release)
add_compile_options(-Wall -Wextra -pedantic -Werror -pthread -lpigpio -lrt)

set(SOURCE_FILES dual_tuner_recorder_updated.c)
include_directories(${LIBSDRPLAY_INCLUDE_DIRS})
include_directories(${PIGPIO_INCLUDE_DIRS})

add_executable(dual_tuner_recorder ${SOURCE_FILES})
target_link_libraries(dual_tuner_recorder ${LIBSDRPLAY_LIBRARIES})
target_link_libraries(dual_tuner_recorder ${PIGPIO_LIBRARIES})
