function(GhostTest name file)
    add_executable(test-${name} ${file})
    target_link_libraries(test-${name} libghost)
    add_test(NAME ${name} COMMAND test-${name})
endfunction()

add_subdirectory(sandbox)
