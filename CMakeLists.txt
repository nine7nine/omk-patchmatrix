cmake_minimum_required(VERSION 2.8)

project(osc.lv2)

include_directories(${PROJECT_SOURCE_DIR})

set(CMAKE_C_FLAGS "-std=gnu11 -Wextra -Wno-unused-parameter -ffast-math -fvisibility=hidden ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "-Wshadow -Wimplicit-function-declaration -Wredundant-decls -Wmissing-prototypes -Wstrict-prototypes ${CMAKE_C_FLAGS}")

include(CTest)

if(${BUILD_TESTING})
	add_executable(osc_test
		osc_test.c)
	add_executable(osc_server
		osc_server.c)
	add_executable(osc_client
		osc_client.c)

	add_test(NAME Forge-Test COMMAND osc_test)
	add_test(NAME Server-Test COMMAND osc_server)
	add_test(NAME Client-Test COMMAND osc_client)
endif()
