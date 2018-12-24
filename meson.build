
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
        'lib/target-x86_64/lower-code.cpp',
        'lib/target-x86_64/mc-emitter.cpp'
    ],
    include_directories: incl,
    dependencies: frigg_dep)

lib_dep = declare_dependency(link_with: lib,
    include_directories: incl)

executable('test-elf', 'tools/test-elf.cpp',
    dependencies: [frigg_dep, lib_dep])

