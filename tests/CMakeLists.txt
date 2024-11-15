set(__ghost_test_namespace "unknown")

function(GhostTestNamespace namespace)
    set(__ghost_test_namespace ${namespace} PARENT_SCOPE)
endfunction()

function(GhostTest name)
    cmake_parse_arguments(
        GHTEST
        "MAYLEAK;VALGRINDWORKAROUND"
        ""
        ""
        ${ARGN}
    )

    set(src ${name}.c)
    if (${GHTEST_VALGRINDWORKAROUND})
        list(APPEND src ${CMAKE_SOURCE_DIR}/tests/valgrind_fexecve.c)
    endif()

    add_executable(test-${__ghost_test_namespace}-${name} ${src})
    target_link_libraries(test-${__ghost_test_namespace}-${name} libghost)

    target_compile_options(test-${__ghost_test_namespace}-${name} PUBLIC -include ${CMAKE_SOURCE_DIR}/tests/test.h)

    if (${GHTEST_VALGRINDWORKAROUND})
        target_compile_definitions(test-${__ghost_test_namespace}-${name} PUBLIC GHOST_JAIL_EXE_PATH="$<TARGET_FILE:ghost-jail>")
    endif()

    if(${GHTEST_MAYLEAK})
        add_test(NAME ${__ghost_test_namespace}/${name} COMMAND valgrind --error-exitcode=1 $<TARGET_FILE:test-${__ghost_test_namespace}-${name}>)
    else()
        add_test(NAME ${__ghost_test_namespace}/${name} COMMAND valgrind --leak-check=full --error-exitcode=1 $<TARGET_FILE:test-${__ghost_test_namespace}-${name}>)
    endif()
endfunction()

file(GLOB tests_files LIST_DIRECTORIES true "*")
foreach(tests_file ${tests_files})
    if (IS_DIRECTORY ${tests_file})
        add_subdirectory(${tests_file})
    endif()
endforeach()
