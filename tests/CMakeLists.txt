set(__ghost_test_namespace "unknown")

function(GhostTestNamespace namespace)
    set(__ghost_test_namespace ${namespace} PARENT_SCOPE)
endfunction()

function(GhostTest name)
    add_executable(test-${__ghost_test_namespace}-${name} ${name}.c)
    target_link_libraries(test-${__ghost_test_namespace}-${name} libghost)
    add_test(NAME ${__ghost_test_namespace}/${name} COMMAND test-${__ghost_test_namespace}-${name})
endfunction()

file(GLOB tests_files LIST_DIRECTORIES true "*")
foreach(tests_file ${tests_files})
    if (IS_DIRECTORY ${tests_file})
        add_subdirectory(${tests_file})
    endif()
endforeach()
