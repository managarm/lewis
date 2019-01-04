# Copyright the lewis authors (AUTHORS.md) 2018
# SPDX-License-Identifier: MIT

project('lewis', 'cpp')

add_project_arguments('-std=c++17', language: 'cpp')

frigg_project = subproject('frigg')
frigg_dep = frigg_project.get_variable('frigg_dep')

incl = include_directories('include')

lib = shared_library('lewis',
    [
        'lib/elf/create-headers-pass.cpp',
        'lib/elf/file-emitter.cpp',
        'lib/elf/layout-pass.cpp',
        'lib/elf/object.cpp',
        'lib/ir.cpp',
        'lib/target-x86_64/alloc-regs.cpp',
        'lib/target-x86_64/lower-code.cpp',
        'lib/target-x86_64/mc-emitter.cpp'
    ],
    include_directories: incl,
    dependencies: frigg_dep,
    install: true)

lib_dep = declare_dependency(link_with: lib,
    include_directories: incl)

executable('test-elf', 'tools/test-elf.cpp',
    dependencies: [frigg_dep, lib_dep])

install_headers(
    'include/lewis/ir.hpp',
    'include/lewis/hierarchy.hpp',
    'include/lewis/passes.hpp',
    subdir: 'lewis')

install_headers(
    'include/lewis/util/byte-encode.hpp',
    subdir: 'lewis/util')

install_headers(
    'include/lewis/target-x86_64/arch-passes.hpp',
    'include/lewis/target-x86_64/mc-emitter.hpp',
    'include/lewis/target-x86_64/arch-ir.hpp',
    subdir: 'lewis/target-x86_64')

install_headers(
    'include/lewis/elf/object.hpp',
    'include/lewis/elf/file-emitter.hpp',
    'include/lewis/elf/utils.hpp',
    'include/lewis/elf/passes.hpp',
    subdir: 'lewis/elf')
