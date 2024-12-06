cmake_minimum_required(VERSION 3.27)
project(prep VERSION 1.0.0 DESCRIPTION "Tiny extensible preprocessor")
add_executable(prep prep.c tokenizer.c plugin.c)
target_link_libraries(prep libghost)
