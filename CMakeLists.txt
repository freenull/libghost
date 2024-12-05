cmake_minimum_required(VERSION 3.27)
project(ghost VERSION 1.0.0 DESCRIPTION "Safe Lua runtime based on LuaJIT")
include(GNUInstallDirs)
include(CTest)
include(CMakeParseArguments)
include(ExternalProject)

set(libghost_cflags "")
function(GhostAddCFlags)
    # string (REPLACE ";" " " flag_str "${ARGN}")
    set(flag_str "${ARGN}")
    set(libghost_cflags "${libghost_cflags};${flag_str}" PARENT_SCOPE)
endfunction()

if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    GhostAddCFlags(-Weverything)

    GhostAddCFlags(-Wno-unused-macros)
    # RATIONALE: Clang sometimes doesn't like _GNU_SOURCE with this warning enabled.
    GhostAddCFlags(-Wno-disabled-macro-expansion)
    # RATIONALE: This is well documented and expected behavior. It triggers on certain standard macros like SOCK_DGRAM.
    GhostAddCFlags(-Wno-class-varargs)
    # RATIONALE: We are not using C++, and there are no classes. This causes issues when passing structs through varargs, which is fine in C.
    GhostAddCFlags(-Wno-covered-switch-default)
    # RATIONALE: Warning concerns default cases when all cases have been covered. Not only is this harmless, the default case could still be triggered by casts of bogus values to an enum.
    GhostAddCFlags(-Wno-gnu-statement-expression-from-macro-expansion)
    # RATIONALE: Project currently only supports compilers that support GNU statement expressions.
    GhostAddCFlags(-Wno-unsafe-buffer-usage)
    # RATIONALE: Intended for C++. Triggers on simple access to stack array in C. Bogus.
    GhostAddCFlags(-Wno-extra-semi-stmt)
    # RATIONALE: Messes with macros that can't be done any other way than with a useless semicolon.
    GhostAddCFlags(-Wno-assign-enum)
    # RATIONALE: This makes using bitflags impossible, as it wants all assignments to an enum to be the value of a single enum key.
elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    GhostAddCFlags(-Wextra -Wall)
else()
    message(FATAL_ERROR "Unsupported compiler: ${CMAKE_C_COMPILER_ID} (${CMAKE_C_COMPILER})")
endif()

GhostAddCFlags(-std=c11)
GhostAddCFlags(-Werror)

GhostAddCFlags(-Wno-declaration-after-statement)
# RATIONALE: Project supports C11+.
GhostAddCFlags(-Wno-padded)
# RATIONALE: Padding added by the compiler is treated carefully and is not an error.
GhostAddCFlags(-Wno-vla)
# RATIONALE: Project currently only supports compilers with VLA support.
GhostAddCFlags(-Wno-error=unused-variable)
# RATIONALE: Creates issues when the variable is used in an assert(), which is not compiled in at all in release mode.
GhostAddCFlags(-Wno-error=unused-function)
# RATIONALE: Creates issues when the function is used in an assert(), which is not compiled in at all in release mode.
GhostAddCFlags(-Wno-unused-value)
# RATIONALE: LuaJIT macros like luaL_dostring use operator shortcircuiting to save on writing if conditions sometimes. This triggers unused value warnings.
GhostAddCFlags(-Wno-unknown-pragmas)
# RATIONALE: Sometimes #pragma clang is used instead of #pragma GCC to only affect Clang.

GhostAddCFlags(-fno-strict-aliasing)
# The library may potentially contain strict aliasing violations. It's safer to turn this on than hope everything works correctly.

set(enumgen "tools/enumgen.py")
set(enum_error_src "intermediate/gh_error.csv")
set(enum_error_out_header "${PROJECT_SOURCE_DIR}/include/ghost/generated/gh_error.h")
set(enum_error_out_source "${PROJECT_SOURCE_DIR}/src/ghost/generated/gh_error.c")
set(enum_error_out ${enum_error_out_header} ${enum_error_out_source})

add_custom_command(
    OUTPUT ${enum_error_out}
    COMMAND ${PROJECT_SOURCE_DIR}/tools/enumgen.py -p GHR_ -c "${PROJECT_SOURCE_DIR}/intermediate/gh_error.csv" -H ${enum_error_out_header} -S ${enum_error_out_source} -n gh_error -d "Set of all error codes returned by libghost functions."
    DEPENDS ${enum_error_src}
)
add_custom_target(ghost-enum-error DEPENDS ${enum_error_out})

set(enum_permfs_mode_src "intermediate/gh_permfs_mode.csv")
set(enum_permfs_mode_out_header "${PROJECT_SOURCE_DIR}/include/ghost/generated/gh_permfs_mode.h")
set(enum_permfs_mode_out_source "${PROJECT_SOURCE_DIR}/src/ghost/generated/gh_permfs_mode.c")
set(enum_permfs_mode_out ${enum_permfs_mode_out_header} ${enum_permfs_mode_out_source})

add_custom_command(
    OUTPUT ${enum_permfs_mode_out}
    COMMAND ${PROJECT_SOURCE_DIR}/tools/enumgen.py -i -u NONE -p GH_PERMFS_ -c "${PROJECT_SOURCE_DIR}/intermediate/gh_permfs_mode.csv" -H ${enum_permfs_mode_out_header} -S ${enum_permfs_mode_out_source} -n gh_permfs_mode
    DEPENDS ${enum_permfs_mode_src}
)
add_custom_target(ghost-enum-permfs_mode DEPENDS ${enum_permfs_mode_out})

function(GhostCodegenTarget target_name)
    set(codegen "${PROJECT_SOURCE_DIR}/tools/codegen.py")

    cmake_parse_arguments(
        args
        ""
        "INPUT"
        "DEPENDS;OUTPUT"
        ${ARGN}
    )

    set(output_path_list "")
    set(output_args "")
    foreach(output_mapping ${args_OUTPUT})
        string(REGEX REPLACE "^[^=]*=" "" output_path ${output_mapping})
        string(REGEX REPLACE "(^[^=]*)=.*" "\\1" output_key ${output_mapping})

        list(APPEND output_args "-o${output_key}=${output_path}")

        list(APPEND output_path_list ${output_path})
    endforeach()

    add_custom_command(
        OUTPUT ${output_path_list}
        COMMAND "${codegen}" "${PROJECT_SOURCE_DIR}/${args_INPUT}" ${output_args}
        DEPENDS ${args_DEPENDS} "${PROJECT_SOURCE_DIR}/${args_INPUT}" "${codegen}"
    )

    add_custom_target(${target_name} DEPENDS ${output_path_list})
endfunction()

file(GLOB ghost_src "src/ghost/*.c")
file(GLOB ghost_perms_src "src/ghost/perms/*.c")
list(APPEND ghost_src ${ghost_perms_src})
list(APPEND ghost_src "src/ghost/generated/gh_error.c")
list(APPEND ghost_src "src/ghost/generated/gh_permfs_mode.c")
file(GLOB ghost_inc "include/ghost/*.h")

set(cembed "${PROJECT_SOURCE_DIR}/tools/cembed.py")

add_custom_command(
    OUTPUT "${PROJECT_BINARY_DIR}/lua_init.c"
    COMMAND "${cembed}" "${PROJECT_SOURCE_DIR}/intermediate/init.lua" -e -n gh_luainit_script_data -o "${PROJECT_BINARY_DIR}/lua_init.c"
    DEPENDS "${PROJECT_SOURCE_DIR}/intermediate/init.lua" ${cembed}
)
add_custom_target(ghost-jail-luainit DEPENDS "${PROJECT_BINARY_DIR}/lua_init.c")

add_custom_command(
    OUTPUT "${PROJECT_BINARY_DIR}/lua_stdlib.c"
    COMMAND "${cembed}" "${PROJECT_SOURCE_DIR}/intermediate/stdlib.lua" -e -n gh_luastdlib_script_data -o "${PROJECT_BINARY_DIR}/lua_stdlib.c"
    DEPENDS "${PROJECT_SOURCE_DIR}/intermediate/stdlib.lua" ${cembed}
)
add_custom_target(ghost-jail-luastdlib DEPENDS "${PROJECT_SOURCE_DIR}/intermediate/stdlib.lua" ${cembed} "${PROJECT_BINARY_DIR}/lua_stdlib.c")

set(luajit_include "${CMAKE_BINARY_DIR}/luajit/usr/local/include")
add_library(libluajit STATIC IMPORTED)
add_dependencies(libluajit luajit)
set_target_properties(libluajit PROPERTIES IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/libs/luajit/src/libluajit.a")

set(SHA256_PROVIDER_LIB "ssl" CACHE INTERNAL "Library needed to link the SHA256 provider.")
set(SHA256_PROVIDER_INC "" CACHE INTERNAL "Compiler include paths needed to link the SHA256 provider.")
set(SHA256_PROVIDER_IMPL "${CMAKE_SOURCE_DIR}/src/sha256provider_openssl.c" CACHE STRING "Source file implementing the SHA256 provider interface.")

list(APPEND ghost_src ${enum_error_out_source})
list(APPEND ghost_src "${PROJECT_BINARY_DIR}/lua_init.c")
list(APPEND ghost_src "${PROJECT_BINARY_DIR}/lua_stdlib.c")
list(APPEND ghost_src "${SHA256_PROVIDER_IMPL}")
list(APPEND ghost_inc ${enum_error_out_header})

add_library(libghost-nojail SHARED "${ghost_src}")
add_dependencies(libghost-nojail ghost-enum-error ghost-enum-permfs_mode ghost-jail-luainit ghost-jail-luastdlib)
set_target_properties(libghost-nojail PROPERTIES VERSION ${PROJECT_VERSION})
set_target_properties(libghost-nojail PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR})
set_target_properties(libghost-nojail PROPERTIES OUTPUT_NAME "ghost")
target_link_libraries(libghost-nojail PRIVATE ${SHA256_PROVIDER_LIB})
target_include_directories(libghost-nojail PRIVATE include ${SHA256_PROVIDER_INC})
target_compile_options(libghost-nojail PRIVATE ${libghost_cflags})

target_sources(
    libghost-nojail
    PRIVATE ${ghost_src}
    PUBLIC FILE_SET HEADERS
    BASE_DIRS ${PROJECT_SOURCE_DIR}/include
    FILES ${ghost_inc}
)

ExternalProject_Add(luajit
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/libs/luajit
    INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/luajit
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ${CMAKE_COMMAND} -E env
        CC=${CMAKE_C_COMPILER}
        make -C <SOURCE_DIR>
    BUILD_IN_SOURCE TRUE
    INSTALL_COMMAND make DESTDIR=<INSTALL_DIR> install
)

file(GLOB_RECURSE jail_src "src/jail/*.c")
file(GLOB_RECURSE jail_inc "include/jail/*.h")

set(libs_include "${CMAKE_SOURCE_DIR}/libs")

add_executable(ghost-jail "${jail_src}")
add_dependencies(ghost-jail luajit)
target_link_libraries(ghost-jail PUBLIC libghost-nojail libluajit m)
target_include_directories(ghost-jail PRIVATE ${luajit_include} ${libs_include})
target_compile_options(ghost-jail PRIVATE ${libghost_cflags})

add_custom_command(
    OUTPUT "${PROJECT_BINARY_DIR}/embedded_jail.c"
    COMMAND "${cembed}" "${PROJECT_BINARY_DIR}/ghost-jail" -e -n gh_embeddedjail_exe_data -o "${PROJECT_BINARY_DIR}/embedded_jail.c"
    DEPENDS ${jail_src} ${jail_inc} ${cembed}
)
add_custom_target(ghost-embedded-jail DEPENDS "${PROJECT_BINARY_DIR}/embedded_jail.c" ghost-jail)

set(ghost_embedded_jail_src "${PROJECT_BINARY_DIR}/embedded_jail.c")

# In the future, this can be used to avoid the need to build libghost sources twice
# In its current form, it causes an issue with dependency resolution
# (CMake doesn't realize that when a piece of source code changes, and thus the libghost-nojail object files are rebuit, that this target has to run again)
# add_library(
#     libghost SHARED
#     $<LIST:APPEND,$<LIST:FILTER,$<TARGET_OBJECTS:libghost-nojail>,EXCLUDE,.*\/embedded_jail.c>,${ghost_embedded_jail_src}>
# )

add_library(
    libghost SHARED
    ${ghost_src} ${ghost_embedded_jail_src}
)

add_dependencies(libghost ghost-enum-error ghost-enum-permfs_mode ghost-embedded-jail ghost-jail ghost-jail-luainit ghost-jail-luastdlib)
set_target_properties(libghost PROPERTIES VERSION ${PROJECT_VERSION})
set_target_properties(libghost PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR})
set_target_properties(libghost PROPERTIES OUTPUT_NAME "ghost")
target_compile_definitions(libghost PUBLIC GH_EMBEDDEDJAIL_PROVIDED)
target_link_libraries(libghost PRIVATE ${SHA256_PROVIDER_LIB})
target_include_directories(libghost PRIVATE include ${SHA256_PROVIDER_INC})
target_compile_options(libghost PRIVATE ${libghost_cflags})

target_sources(
    libghost
    PRIVATE ${ghost_src}
    PUBLIC FILE_SET HEADERS
    BASE_DIRS ${PROJECT_SOURCE_DIR}/include
    FILES ${ghost_inc}
)


install(
    TARGETS libghost
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET HEADERS
)

install(
    TARGETS ghost-jail
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

add_subdirectory(tests)

option(EXAMPLES "Build examples" OFF)

if (EXAMPLES)
    add_subdirectory(examples)
endif()
