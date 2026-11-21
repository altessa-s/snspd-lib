# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.21)
project(snspd_consumer LANGUAGES C CXX)
find_package(SNSpd REQUIRED)
add_executable(consumer_c ../consumer.c)
target_link_libraries(consumer_c PRIVATE SNSpd::snspd)
set_target_properties(consumer_c PROPERTIES C_STANDARD 23 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
add_executable(consumer_cpp ../consumer.cpp)
target_link_libraries(consumer_cpp PRIVATE SNSpd::snspd)
set_target_properties(consumer_cpp PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
