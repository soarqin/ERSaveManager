project(lzma C)
add_library(lzma STATIC
    LzmaLib.c LzmaEnc.c LzmaDec.c LzFind.c LzFindOpt.c Alloc.c CpuArch.c
)
target_include_directories(lzma PUBLIC .)
target_compile_definitions(lzma PRIVATE Z7_ST)
target_compile_options(lzma PRIVATE $<$<OR:$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:GNU>>:-w>)
