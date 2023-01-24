
#          Copyright Michael Florian Hava.
# Distributed under the Boost Software License, Version 1.0.
#    (See accompanying file LICENSE_1_0.txt or copy at
#          http://www.boost.org/LICENSE_1_0.txt)

cmake_minimum_required(VERSION 3.23)
project(P2774)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Catch2 CONFIG REQUIRED)

add_executable(p2774)
	file(GLOB_RECURSE SRC "inc/*" "test/*")
		source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC})
	target_sources(p2774 PRIVATE ${SRC})
	target_include_directories(p2774 PRIVATE "inc")
	if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
		target_compile_options(p2774 PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
	elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
		target_compile_options(p2774 PRIVATE /Zc:__cplusplus /W4 /permissive-)
	endif()
	target_link_libraries(p2774 PRIVATE Catch2::Catch2 Catch2::Catch2WithMain)

enable_testing()
add_test(NAME P2774 COMMAND p2774)
