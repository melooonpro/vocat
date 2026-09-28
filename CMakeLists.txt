cmake_minimum_required(VERSION 3.16)

# Keep the firmware small: build only main and its BSP dependencies.
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(vocat_flip_clock)
