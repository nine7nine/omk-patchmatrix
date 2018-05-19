project('patchmatrix', 'c', default_options : [
	'buildtype=release',
	'warning_level=1',
	'werror=false',
	'b_lto=false',
	'c_std=c11'])

version = run_command('cat', 'VERSION').stdout().strip()

prefix = get_option('prefix')
datadir = get_option('datadir')
bindir = get_option('bindir')

appdir = join_paths(prefix, datadir, 'applications')
pdatadir = join_paths(prefix, datadir, 'patchmatrix', '')

add_project_arguments('-DPATCHMATRIX_VERSION="'+version+'"', language : 'c')
add_project_arguments('-DPATCHMATRIX_DATA_DIR="'+pdatadir+'"', language : 'c')
add_project_arguments('-D_GNU_SOURCE', language : 'c')
add_project_arguments('-DPUGL_HAVE_GL', language : 'c')

conf_data = configuration_data()
conf_data.set('prefix', prefix)
conf_data.set('bindir', bindir)
conf_data.set('datadir', datadir)

cc = meson.get_compiler('c')

if cc.has_header('jack/metadata.h')
	add_project_arguments('-DJACK_HAS_METADATA_API', language : 'c')
endif

if cc.has_function('jack_set_port_rename_callback', prefix : '#include <jack/jack.h>')
	add_project_arguments('-DJACK_HAS_PORT_RENAME_CALLBACK', language : 'c')
endif

m_dep = cc.find_library('m')
rt_dep = cc.find_library('rt')
lv2_dep = dependency('lv2', version : '>=1.14.0')
jack_dep = dependency('jack')
threads_dep = dependency('threads')

dsp_deps = [m_dep, rt_dep, lv2_dep, jack_dep, threads_dep]
ui_deps = []

pugl_inc = include_directories('pugl')
varchunk_inc = include_directories('varchunk')
jackey_inc = include_directories('jackey')

incs = [pugl_inc, varchunk_inc, jackey_inc]

c_args = ['-fvisibility=hidden',
	'-ffast-math',
	'-Wno-misleading-indentation',
	'-Wno-unused-function']

dsp_srcs = [
	'patchmatrix.c',
	'patchmatrix_db.c',
	'patchmatrix_jack.c',
	'patchmatrix_nk.c'
]
ui_srcs = []

if host_machine.system() == 'windows'
	add_languages('cpp')
	ui_deps += cc.find_library('opengl32')
	ui_deps += cc.find_library('gdi32')
	ui_deps += cc.find_library('user32')
	ui_srcs += 'pugl/pugl/pugl_win.cpp'
elif host_machine.system() == 'darwin'
	#add_languages('objc')
	#ui_deps += cc.find_library('Cocoa')
	#ui_deps += cc.find_library('gl')
	#ui_deps += dependency('appleframeworks', modules : 'cocoa')
	#ui_srcs += 'pugl/pugl/pugl_osx.m'
else
	ui_deps += dependency('gl')
	ui_deps += dependency('x11', version : '>=1.6.0')
	ui_deps += dependency('xext', version : '>=1.3.0')
	ui_srcs += 'pugl/pugl/pugl_x11.c'
endif

cjson_lib = static_library('cJSON', join_paths('cJSON', 'cJSON.c'))

executable('patchmatrix', [dsp_srcs, ui_srcs],
	c_args : c_args,
	dependencies : [dsp_deps, ui_deps],
	include_directories : incs,
	link_with : cjson_lib,
	install : true)

executable('patchmatrix_mixer', 'patchmatrix_mixer.c',
	c_args : c_args,
	dependencies : dsp_deps,
	include_directories : incs,
	link_with : cjson_lib,
	install : true)

executable('patchmatrix_monitor', 'patchmatrix_monitor.c',
	c_args : c_args,
	dependencies : dsp_deps,
	include_directories : incs,
	link_with : cjson_lib,
	install : true)

configure_file(input : 'patchmatrix.desktop.in', output : 'patchmatrix.desktop',
	configuration : conf_data,
	install_dir : appdir,
	install : true)

install_man('patchmatrix.1')
install_man('patchmatrix_mixer.1')
install_man('patchmatrix_monitor.1')

install_data(join_paths('pix', 'patchmatrix.png'),
	install_dir : join_paths(prefix, datadir, 'icons', 'hicolor', '256x256', 'apps'))

install_data(join_paths('pix', 'audio.png'),
	install_dir : pdatadir)
install_data(join_paths('pix', 'midi.png'),
	install_dir : pdatadir)
install_data(join_paths('pix', 'osc.png'),
	install_dir : pdatadir)
install_data(join_paths('pix', 'cv.png'),
	install_dir : pdatadir)
install_data(join_paths('nuklear', 'extra_font', 'Cousine-Regular.ttf'),
	install_dir : pdatadir)
