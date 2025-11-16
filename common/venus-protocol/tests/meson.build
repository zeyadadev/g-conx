# Copyright 2020 Google LLC
# SPDX-License-Identifier: MIT

cc = meson.get_compiler('c')

warnings = [
  '-Wno-unused-parameter',
  '-Wno-unused-but-set-parameter',
]
foreach w : warnings
  if cc.has_argument(w)
    add_project_arguments(w, language: 'c')
  endif
endforeach

vn_test_files = files(
  'main.c',
  'driver.c',
  'renderer.c',
)

vn_protocol_tests = executable(
  'vn_protocol_tests',
  [vn_test_files,
   vn_protocol_driver,
   vn_protocol_renderer],
  include_directories: ['../include',
                        '../include/vulkan'],
)
