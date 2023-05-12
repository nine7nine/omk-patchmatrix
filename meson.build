# SPDX-FileCopyrightText: Hanspeter Portner <dev@open-music-kontrollers.ch>
# SPDX-License-Identifier: CC0-1.0

project('varchunk', 'c', default_options : [
	'buildtype=release',
	'warning_level=3',
	'werror=false',
	'b_lto=true',
	'c_std=c11'])

build_tests = get_option('build-tests')

reuse = find_program('reuse', required : false)

add_project_arguments('-D_GNU_SOURCE', language : 'c')

cc = meson.get_compiler('c')
rt_dep = cc.find_library('rt')

lib_deps = [
  rt_dep
]

lib_incs = [
  include_directories('')
]

lib_srcs = [
  join_paths('src', 'varchunk.c')
]

varchunk = declare_dependency(
  include_directories : lib_incs,
  dependencies : lib_deps,
  sources : lib_srcs)

if build_tests
  thread_dep = dependency('threads')

  test_deps = [
    varchunk,
    thread_dep
  ]

  test_srcs = [
    join_paths('test', 'test.c')
  ]

  test_varchunk = executable('Test', test_srcs,
    dependencies : test_deps,
    install : false)

  test('Test', test_varchunk,
    args : ['100000'],
    timeout : 360, # seconds
    suite: ['memcheck'])

  if reuse.found()
    test('REUSE', reuse, args : [
      '--root', meson.current_source_dir(),
      'lint'
    ])
  endif
endif
