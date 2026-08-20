#
# This file is just a small shim required by newer ESP-IDF versions.
# The real per-target build lives in main/CMakeLists.txt.
#
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(voice_recorder)
