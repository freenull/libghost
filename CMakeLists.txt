cmake_minimum_required(VERSION 3.27)
project(ghost VERSION 1.0.0 DESCRIPTION "Safe Lua runtime based on LuaJIT")
include(GNUInstallDirs)
include(CTest)

set(enumgen "tools/enumgen.py")
set(enum_error_src "intermediate/gh_error.csv")
set(enum_error_out_header "${PROJECT_SOURCE_DIR}/include/ghost/generated/gh_error.h")
set(enum_error_out_source "${PROJECT_SOURCE_DIR}/src/ghost/generated/gh_error.c")
set(enum_error_out ${enum_error_out_header} ${enum_error_out_source})

add_custom_command(
    OUTPUT ${enum_error_out}
    COMMAND ${PROJECT_SOURCE_DIR}/tools/enumgen.py -p GHR_ -c "${PROJECT_SOURCE_DIR}/intermediate/gh_error.csv" -H ${enum_error_out_header} -S ${enum_error_out_source} -n gh_error
    DEPENDS ${enum_error_src}
)
add_custom_target(ghost-enum-error DEPENDS ${enum_error_out})

file(GLOB_RECURSE ghost_src "src/ghost/*.c")
file(GLOB_RECURSE ghost_inc "include/ghost/*.h")

add_library(libghost-nojail SHARED "${ghost_src}")
add_dependencies(libghost-nojail ghost-enum-error)
set_target_properties(libghost-nojail PROPERTIES VERSION ${PROJECT_VERSION})
set_target_properties(libghost-nojail PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR})
set_target_properties(libghost-nojail PROPERTIES OUTPUT_NAME "ghost")
target_include_directories(libghost-nojail PRIVATE include)

target_sources(
    libghost-nojail
    PRIVATE ${ghost_src}
    PUBLIC FILE_SET HEADERS
    BASE_DIRS ${PROJECT_SOURCE_DIR}/include
    FILES ${ghost_inc}
)

file(GLOB_RECURSE jail_src "src/jail/*.c")
file(GLOB_RECURSE jail_inc "include/jail/*.h")

add_executable(ghost-jail "${jail_src}")
target_include_directories(ghost-jail PRIVATE include)
target_link_libraries(ghost-jail PUBLIC libghost-nojail)

add_custom_command(
    OUTPUT "${PROJECT_BINARY_DIR}/embedded_jail.c"
    COMMAND xxd -n gh_embeddedjail_exe_data -i "${PROJECT_BINARY_DIR}/ghost-jail" > "${PROJECT_BINARY_DIR}/embedded_jail.c"
    DEPENDS ${jail_src} ${jail_inc}
)
add_custom_target(ghost-embedded-jail DEPENDS "${PROJECT_BINARY_DIR}/embedded_jail.c")

add_custom_target(genexdebug COMMAND ${CMAKE_COMMAND} -E echo "a = $<LIST:FILTER,$<TARGET_OBJECTS:libghost-nojail>,EXCLUDE,.*\/embedded_jail.c>")


set(ghost_embedded_jail_src "src/ghost/embedded_jail.c")

add_library(
    libghost SHARED
    $<LIST:APPEND,$<LIST:FILTER,$<TARGET_OBJECTS:libghost-nojail>,EXCLUDE,.*\/embedded_jail.c>,${ghost_embedded_jail_src}>
)

add_dependencies(libghost ghost-enum-error ghost-embedded-jail ghost-jail libghost-nojail)
add_dependencies(libghost libghost-nojail)
set_target_properties(libghost PROPERTIES VERSION ${PROJECT_VERSION})
set_target_properties(libghost PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR})
set_target_properties(libghost PROPERTIES OUTPUT_NAME "ghost")
target_compile_definitions(libghost PUBLIC GH_EMBEDDEDJAIL_PROVIDED)
target_include_directories(libghost PRIVATE include)

target_sources(
    libghost
    PRIVATE "${PROJECT_BINARY_DIR}/embedded_jail.c"
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
