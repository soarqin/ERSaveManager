project(md5 C)
add_library(md5 STATIC
    md5.c md5.h
)
target_include_directories(md5 PUBLIC .)
