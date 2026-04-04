# Add facedataman executable project with WIN32 subsystem
add_project(saveman WIN32
    main.c
    ersave.c ersave.h
    config.c config.h
    locale.c locale.h

    embedded_face_data.c embedded_face_data.h

    face_dialog.c face_dialog.h

    file_dialog.c file_dialog.h

    ui_controls.c ui_controls.h

    resource.h
    app.rc
    FOLDER "app"
    OUTPUT_NAME "ERSaveManager"
)

# Enable Unicode character set
target_compile_definitions(${PROJECT_NAME} PRIVATE UNICODE _UNICODE)

# Link against required libraries:
# - md5: MD5 hash library
# - inih: INI parser
# - shlwapi: Shell Light-Weight API
# - comctl32: Common Controls library
# - uxtheme: Theme API
target_link_libraries(${PROJECT_NAME} PRIVATE md5 inih shlwapi comctl32 uxtheme)

# Recompile app.rc when app.manifest changes
set_property(SOURCE app.rc PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/app.manifest"
    "${CMAKE_CURRENT_SOURCE_DIR}/app.ico"
    "${CMAKE_CURRENT_SOURCE_DIR}/resource.h"
)

if (MSVC)
    # Disable manifest generation for MSVC as we embed it in resources
    set_target_properties(${PROJECT_NAME} PROPERTIES
        LINK_FLAGS "/MANIFEST:NO"
    )
else ()
    # Enable Unicode entry point for MinGW
    target_link_options(${PROJECT_NAME} PRIVATE -municode)
endif ()
