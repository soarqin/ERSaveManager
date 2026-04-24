add_project(ersave_common STATIC
    common.c
    file_dialog.c file_dialog.h
    save_compress.c save_compress.h
    FOLDER "common"
)

target_include_directories(ersave_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(ersave_common PRIVATE UNICODE _UNICODE)
target_link_libraries(ersave_common PUBLIC md5 inih lzma shlwapi comctl32 ole32 oleaut32)
