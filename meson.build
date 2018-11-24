
project('lewis', 'cpp')

incl = include_directories('include')

add_project_arguments('-std=c++17', language: 'cpp')

lib = shared_library('lewis',
    [
        'lib/elf/create-headers-pass.cpp',
        'lib/elf/commit-headers-pass.cpp',
        'lib/elf/layout-pass.cpp',
        'lib/elf/object.cpp',
    ],
    include_directories: incl)

lib_dep = declare_dependency(link_with: lib,
    include_directories: incl)

executable('test-elf', 'tools/test-elf.cpp',
    dependencies: lib_dep)

