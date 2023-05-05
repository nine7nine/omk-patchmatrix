# SPDX-FileCopyrightText: Hanspeter Portner <dev@open-music-kontrollers.ch>
# SPDX-License-Identifier: CC0-1.0

project('patchmatrix', 'c', default_options : [
	'buildtype=release',
	'warning_level=3',
	'werror=false',
	'b_lto=true',
	'c_std=gnu11'])

nk_pugl = subproject('nk_pugl')
varchunk = subproject('varchunk')

reuse = find_program('reuse', required : false)

version = get_option('version')
build_tests = get_option('build-tests')

prefix = get_option('prefix')
datadir = get_option('datadir')
bindir = get_option('bindir')

appdir = join_paths(prefix, datadir, 'applications')
pdatadir = join_paths(prefix, datadir, 'patchmatrix', '')

add_project_arguments('-DPATCHMATRIX_VERSION="'+version+'"', language : 'c')
add_project_arguments('-DPATCHMATRIX_DATA_DIR="'+pdatadir+'"', language : 'c')
add_project_arguments('-DPUGL_HAVE_GL', language : 'c')
add_project_arguments('-D_GNU_SOURCE', language : 'c')

conf_data = configuration_data()
conf_data.set('prefix', prefix)
conf_data.set('bindir', bindir)
conf_data.set('datadir', datadir)

cc = meson.get_compiler('c')

m_dep = cc.find_library('m')
rt_dep = cc.find_library('rt')
lv2_dep = dependency('lv2', version : '>=1.14.0')
jack_dep = dependency('jack')
threads_dep = dependency('threads')
nk_pugl_dep = nk_pugl.get_variable('nk_pugl_gl')
varchunk_dep = varchunk.get_variable('varchunk')
cousine_regular_ttf = nk_pugl.get_variable('cousine_regular_ttf')

dsp_deps = [m_dep, rt_dep, lv2_dep, jack_dep, threads_dep, varchunk_dep]
ui_deps = [nk_pugl_dep]

if cc.has_header('jack/metadata.h')
	add_project_arguments('-DJACK_HAS_METADATA_API', language : 'c')
endif

if cc.has_function('jack_set_port_rename_callback', prefix : '#include <jack/jack.h>')
	add_project_arguments('-DJACK_HAS_PORT_RENAME_CALLBACK', language : 'c')
endif

jackey_inc = include_directories('jackey')
osc_inc = include_directories('osc.lv2')

incs = [jackey_inc, osc_inc]

c_args = [
  '-fvisibility=hidden',
	'-ffast-math'
]

dsp_srcs = [
	join_paths('src', 'patchmatrix.c'),
	join_paths('src', 'patchmatrix_db.c'),
	join_paths('src', 'patchmatrix_jack.c'),
	join_paths('src', 'patchmatrix_nk.c')
]

executable('patchmatrix', dsp_srcs,
	c_args : c_args,
	dependencies : [dsp_deps, ui_deps],
	include_directories : incs,
	install : true)

mixer_srcs = [
  join_paths('src', 'patchmatrix_mixer.c')
]

executable('patchmatrix_mixer', mixer_srcs,
	c_args : c_args,
	dependencies : dsp_deps,
	include_directories : incs,
	install : true)

monitor_srcs = [
  join_paths('src', 'patchmatrix_monitor.c')
]

executable('patchmatrix_monitor', monitor_srcs,
	c_args : c_args,
	dependencies : dsp_deps,
	include_directories : incs,
	install : true)

configure_file(
	input : join_paths('share', 'patchmatrix.desktop.in'),
	output : 'patchmatrix.desktop',
	configuration : conf_data,
	install_dir : appdir,
	install : true)

configure_file(
	input : cousine_regular_ttf,
	output : 'Cousine-Regular.ttf',
	copy : true,
	install_dir : pdatadir,
	install : true)

install_man(join_paths('man', 'patchmatrix.1'))
install_man(join_paths('man', 'patchmatrix_mixer.1'))
install_man(join_paths('man', 'patchmatrix_monitor.1'))

install_data(join_paths('share', 'patchmatrix', 'patchmatrix.png'),
	install_dir : join_paths(prefix, datadir, 'icons', 'hicolor', '256x256', 'apps'))

install_data(join_paths('share', 'patchmatrix', 'audio.png'),
	install_dir : pdatadir)
install_data(join_paths('share', 'patchmatrix', 'midi.png'),
	install_dir : pdatadir)
install_data(join_paths('share', 'patchmatrix', 'osc.png'),
	install_dir : pdatadir)
install_data(join_paths('share', 'patchmatrix', 'cv.png'),
	install_dir : pdatadir)

if build_tests
  if reuse.found()
    test('REUSE', reuse, args : [
      '--root', meson.current_source_dir(),
      'lint'
    ])
  endif
endif
