cmake_minimum_required(VERSION 2.8)

project(patchmatrix)

set(CMAKE_C_FLAGS "-fdata-sections -ffunction-sections ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "-std=gnu11 -Wextra -Wno-unused-parameter -ffast-math -fvisibility=hidden ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "-Wshadow -Wimplicit-function-declaration -Wredundant-decls -Wmissing-prototypes -Wstrict-prototypes ${CMAKE_C_FLAGS}")
add_definitions("-D_GNU_SOURCE=1") # asprintf

set(LIBS "")
include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/jackey)
include_directories(${PROJECT_SOURCE_DIR}/varchunk)
include_directories(${PROJECT_SOURCE_DIR}/pugl)

# paths
set(PATCHMATRIX_BIN_DIR bin)
set(PATCHMATRIX_ICON_DIR share/icons/hicolor/256x256/apps)
set(PATCHMATRIX_DESKTOP_DIR share/applications)
set(PATCHMATRIX_DATA_DIR share/patchmatrix)
set(PATCHMATRIX_MAN_DIR share/man/man1)
add_definitions("-DPATCHMATRIX_DATA_DIR=\"${CMAKE_INSTALL_PREFIX}/${PATCHMATRIX_DATA_DIR}/\"")

# version
file(STRINGS "VERSION" PATCHMATRIX_VERSION)
string(REPLACE "." ";" VERSION_LIST ${PATCHMATRIX_VERSION})
list(GET VERSION_LIST 0 PATCHMATRIX_MAJOR_VERSION)
list(GET VERSION_LIST 1 PATCHMATRIX_MINOR_VERSION)
list(GET VERSION_LIST 2 PATCHMATRIX_MICRO_VERSION)
add_definitions("-DPATCHMATRIX_VERSION=\"${PATCHMATRIX_VERSION}\"")

# pkg-config
find_package(PkgConfig) # ${PKG_CONFIG_FOUND}

# LV2
pkg_search_module(LV2 REQUIRED lv2)
include_directories(${LV2_INCLUDE_DIRS})

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

# port_rename_callback
include(CheckLibraryExists)
CHECK_LIBRARY_EXISTS(jack jack_set_port_rename_callback "" JACK_HAS_PORT_RENAME_CALLBACK)
if(${JACK_HAS_PORT_RENAME_CALLBACK})
	add_definitions("-DJACK_HAS_PORT_RENAME_CALLBACK")
endif()

set(LIBS ${LIBS} m)
add_definitions("-DPUGL_HAVE_GL")

find_package(OpenGL)
if(${OPENGL_FOUND})
	set(LIBS ${LIBS} ${OPENGL_LIBRARIES})
else() # try pkg-config
	pkg_search_module(GL REQUIRED gl)
	if(${GL_FOUND})
		set(LIBS ${LIBS} ${GL_LDFLAGS})
	else()
		message(FATAL_ERROR "OpenGL not found")
	endif()
endif()
add_definitions("-DPUGL_HAVE_GL")

if(WIN32)
	find_library(GDI32_LIBRARY NAMES gdi32)
	if(GDI32_LIBRARY)
		set(LIBS ${LIBS} ${GDI32_LIBRARY})
	else()
		message(FATAL_ERROR "gdi32 library not found")
	endif()

	find_library(USER32_LIBRARY NAMES user32)
	if(USER32_LIBRARY)
		set(LIBS ${LIBS} ${USER32_LIBRARY})
	else()
		message(FATAL_ERROR "user32 library not found")
	endif()

	set(TAR_UI ${TAR_UI} pugl/pugl/pugl_win.cpp)

elseif(APPLE)
	find_library(COCOA_LIBRARY NAMES Cocoa)
	if(COCOA_LIBRARY)
		set(LIBS ${LIBS} ${COCOA_LIBRARY})
	else()
		message(FATAL_ERROR "Cocoa framework not found")
	endif()

	set(TAR_UI ${TAR_UI} pugl/pugl/pugl_osx.m)

else() # GNU/Linux
	pkg_search_module(X11 REQUIRED x11>=1.6)
	include_directories(${X11_INCLUDE_DIRS})
	set(LIBS ${LIBS} ${X11_LDFLAGS})

	pkg_search_module(XEXT REQUIRED xext>=1.3)
	include_directories(${XEXT_INCLUDE_DIRS})
	set(LIBS ${LIBS} ${XEXT_LDFLAGS})

	set(TAR_UI ${TAR_UI} pugl/pugl/pugl_x11.c)
endif()

# binary
add_executable(patchmatrix
	patchmatrix.c
	patchmatrix_jack.c
	patchmatrix_nk.c
	patchmatrix_db.c
	${TAR_UI})
target_link_libraries(patchmatrix ${LIBS})
install(TARGETS patchmatrix DESTINATION ${PATCHMATRIX_BIN_DIR})

# desktop 
configure_file(${PROJECT_SOURCE_DIR}/patchmatrix.desktop.in ${PROJECT_BINARY_DIR}/patchmatrix.desktop @ONLY)
install(FILES ${PROJECT_BINARY_DIR}/patchmatrix.desktop DESTINATION ${PATCHMATRIX_DESKTOP_DIR})

# logo
install(FILES pix/patchmatrix.png DESTINATION ${PATCHMATRIX_ICON_DIR})

# man page 
include(FindUnixCommands)
add_custom_command(
	OUTPUT ${PROJECT_BINARY_DIR}/patchmatrix.1.gz
	COMMAND ${GZIP} ARGS
	"-c9" ${CMAKE_CURRENT_SOURCE_DIR}/patchmatrix.1
		">" ${PROJECT_BINARY_DIR}/patchmatrix.1.gz
	DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/patchmatrix.1)
add_custom_target(PATCHMATRIX_MAN_GZ ALL DEPENDS ${PROJECT_BINARY_DIR}/patchmatrix.1.gz)
install(FILES ${PROJECT_BINARY_DIR}/patchmatrix.1.gz DESTINATION ${PATCHMATRIX_MAN_DIR})

# icons
install(FILES ${PROJECT_SOURCE_DIR}/pix/audio.png DESTINATION ${PATCHMATRIX_DATA_DIR})
install(FILES ${PROJECT_SOURCE_DIR}/pix/midi.png DESTINATION ${PATCHMATRIX_DATA_DIR})
install(FILES ${PROJECT_SOURCE_DIR}/pix/osc.png DESTINATION ${PATCHMATRIX_DATA_DIR})
install(FILES ${PROJECT_SOURCE_DIR}/pix/cv.png DESTINATION ${PATCHMATRIX_DATA_DIR})

# font
install(FILES ${PROJECT_SOURCE_DIR}/nuklear/extra_font/Cousine-Regular.ttf DESTINATION ${PATCHMATRIX_DATA_DIR})
