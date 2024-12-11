set(__ghost_test_namespace "unknown")

function(GhostTestNamespace namespace)
    set(__ghost_test_namespace ${namespace} PARENT_SCOPE)
endfunction()

function(GhostTest name)
    cmake_parse_arguments(
        GHTEST
        "MAYLEAK;NOSANDBOX;INTERACTIVE;NOVALGRIND"
        "TIMEOUT;STDIN"
        ""
        ${ARGN}
    )

    if (NOT DEFINED GHTEST_TIMEOUT)
        set(GHTEST_TIMEOUT 10)
    endif()

    set(src ${name}.c)

    add_executable(test-${__ghost_test_namespace}-${name} ${src})
    target_link_libraries(test-${__ghost_test_namespace}-${name} libghost)

    target_compile_options(test-${__ghost_test_namespace}-${name} PUBLIC -include ${CMAKE_SOURCE_DIR}/tests/test.h)

    set(env "")
    if (${GHTEST_NOSANDBOX})
        list(APPEND env "GH_SANDBOX_DISABLED=1")
    endif()

    set(test_workdir "${CMAKE_SOURCE_DIR}/tests/${__ghost_test_namespace}")

    set(extra_valgrind_opts "")
    if(NOT GHTEST_MAYLEAK)
        list(APPEND extra_valgrind_opts --leak-check=full --show-leak-kinds=all)
    endif()

    if(GHTEST_NOVALGRIND)
        add_test(NAME ${__ghost_test_namespace}/${name} COMMAND $<TARGET_FILE:test-${__ghost_test_namespace}-${name}> WORKING_DIRECTORY ${test_workdir})
    else()
        add_test(NAME ${__ghost_test_namespace}/${name} COMMAND valgrind --trace-children=yes ${extra_valgrind_opts} --error-exitcode=1 env ${env} $<TARGET_FILE:test-${__ghost_test_namespace}-${name}> WORKING_DIRECTORY ${test_workdir})
    endif()

    set_tests_properties(${__ghost_test_namespace}/${name} PROPERTIES TIMEOUT ${GHTEST_TIMEOUT})
endfunction()

file(GLOB tests_files LIST_DIRECTORIES true "*")
foreach(tests_file ${tests_files})
    if (IS_DIRECTORY ${tests_file})
        add_subdirectory(${tests_file})
    endif()
endforeach()
