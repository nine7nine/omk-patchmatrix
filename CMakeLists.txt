cmake_minimum_required(VERSION 2.8)

project(patchmatrix)

set(CMAKE_C_FLAGS "-std=gnu11 -Wextra -Wno-unused-parameter -ffast-math -fvisibility=hidden ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "-Wshadow -Wimplicit-function-declaration -Wmissing-prototypes -Wstrict-prototypes ${CMAKE_C_FLAGS}")
add_definitions("-D_GNU_SOURCE=1") # asprintf

set(LIBS "")
include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/libosc)

# paths
set(PATCHMATRIX_BIN_DIR "bin")
set(PATCHMATRIX_ICON_DIR "share/icons/hicolor/256x256/apps")
set(PATCHMATRIX_DESKTOP_DIR "share/applications")
set(PATCHMATRIX_DATA_DIR "share/patchmatrix")
add_definitions("-DPATCHMATRIX_DATA_DIR=\"${CMAKE_INSTALL_PREFIX}/${PATCHMATRIX_DATA_DIR}\"")

# version
set(PATCHMATRIX_MAJOR_VERSION 0)
set(PATCHMATRIX_MINOR_VERSION 1)
set(PATCHMATRIX_MICRO_VERSION 0)
set(PATCHMATRIX_VERSION "${PATCHMATRIX_MAJOR_VERSION}.${PATCHMATRIX_MINOR_VERSION}.${PATCHMATRIX_MICRO_VERSION}")
add_definitions("-DPATCHMATRIX_VERSION=\"${PATCHMATRIX_VERSION}\"")

# pkg-config
find_package(PkgConfig) # ${PKG_CONFIG_FOUND}

# jack
pkg_search_module(JACK REQUIRED jack)
include_directories(${JACK_INCLUDE_DIRS})
set(LIBS ${LIBS} ${JACK_LDFLAGS})

# metadata
include(CheckIncludeFiles)
CHECK_INCLUDE_FILES("jack/metadata.h" JACK_HAS_METADATA_API)
if(${JACK_HAS_METADATA_API})
	add_definitions("-DJACK_HAS_METADATA_API")
endif()

# elementary
pkg_search_module(ELEMENTARY REQUIRED elementary>=1.8)
include_directories(${ELEMENTARY_INCLUDE_DIRS})
set(LIBS ${LIBS} ${ELEMENTARY_LDFLAGS})

# sqlite3
pkg_search_module(SQLITE3 REQUIRED sqlite3>=3.0)
include_directories(${SQLITE3_INCLUDE_DIRS})
set(LIBS ${LIBS} ${SQLITE3_LDFLAGS})

# binary
add_executable(patchmatrix
	patchmatrix.c
	patcher.c)
target_link_libraries(patchmatrix ${LIBS})
install(TARGETS patchmatrix DESTINATION ${PATCHMATRIX_BIN_DIR})

# desktop 
configure_file(${PROJECT_SOURCE_DIR}/patchmatrix.desktop.in ${PROJECT_BINARY_DIR}/patchmatrix.desktop @ONLY)
install(FILES ${PROJECT_BINARY_DIR}/patchmatrix.desktop DESTINATION ${PATCHMATRIX_DESKTOP_DIR})

# icon
install(FILES patchmatrix.png DESTINATION ${PATCHMATRIX_ICON_DIR})

# theme
find_program(EDJE_CC NAMES edje_cc)
if(EDJE_CC_NOTFOUND)
	message(SEND_ERROR "edje_cc not found")
else(EDJE_CC_NOTFOUND)
	message(STATUS "edje_cc found: " ${EDJE_CC})
endif(EDJE_CC_NOTFOUND)

add_custom_command(
	OUTPUT ${PROJECT_BINARY_DIR}/patchmatrix.edj
	COMMAND ${EDJE_CC} ARGS
		"-fd" ${CMAKE_CURRENT_SOURCE_DIR}/font
		"-id" ${CMAKE_CURRENT_SOURCE_DIR}/pix
		${CMAKE_CURRENT_SOURCE_DIR}/patchmatrix.edc
		${PROJECT_BINARY_DIR}/patchmatrix.edj
	DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/patchmatrix.edc
		${CMAKE_CURRENT_SOURCE_DIR}/patcher.edc
		${CMAKE_CURRENT_SOURCE_DIR}/list.edc)
add_custom_target(THEME ALL DEPENDS ${PROJECT_BINARY_DIR}/patchmatrix.edj)
install(FILES ${PROJECT_BINARY_DIR}/patchmatrix.edj DESTINATION ${PATCHMATRIX_DATA_DIR})

install(FILES ${PROJECT_SOURCE_DIR}/pix/audio.png DESTINATION ${PATCHMATRIX_DATA_DIR})
install(FILES ${PROJECT_SOURCE_DIR}/pix/midi.png DESTINATION ${PATCHMATRIX_DATA_DIR})
install(FILES ${PROJECT_SOURCE_DIR}/pix/osc.png DESTINATION ${PATCHMATRIX_DATA_DIR})
install(FILES ${PROJECT_SOURCE_DIR}/pix/cv.png DESTINATION ${PATCHMATRIX_DATA_DIR})
install(FILES ${PROJECT_SOURCE_DIR}/pix/omk_logo_256x256.png DESTINATION ${PATCHMATRIX_DATA_DIR})
