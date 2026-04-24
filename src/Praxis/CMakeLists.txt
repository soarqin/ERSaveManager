add_project(praxis WIN32
    main.c
    config.c
    config.h
    locale.c
    locale.h
    save_tree.c
    save_tree.h
    game_backend.h
    backend_registry.c
    backend_registry.h
    ring_backup.c
    ring_backup.h
    restore_safe.c
    restore_safe.h
    backends/er_backend.c
    resource.h
    app.rc
    VERSION 1.0.0
    FOLDER "app"
    OUTPUT_NAME "Praxis"
)

# Generate version header from CMake project version
configure_file(version.h.in "${CMAKE_CURRENT_BINARY_DIR}/version.h" @ONLY)

# Add build directory to include path for the generated version header
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# Enable Unicode character set
target_compile_definitions(${PROJECT_NAME} PRIVATE UNICODE _UNICODE)

# Link against required libraries
target_link_libraries(${PROJECT_NAME} PRIVATE ersave_common uxtheme shell32)

# Recompile app.rc when dependent headers or assets change
set_property(SOURCE app.rc PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/app.manifest"
    "${CMAKE_CURRENT_SOURCE_DIR}/app.ico"
    "${CMAKE_CURRENT_SOURCE_DIR}/resource.h"
    "${CMAKE_CURRENT_BINARY_DIR}/version.h"
)

if (MSVC)
    # Disable manifest generation for MSVC as we embed it in resources
    set_target_properties(${PROJECT_NAME} PROPERTIES
        LINK_FLAGS "/MANIFEST:NO"
    )
else ()
    # Enable Unicode entry point for MinGW/Clang
    target_link_options(${PROJECT_NAME} PRIVATE -municode)
    string(REPLACE "-Xlinker /MANIFEST:EMBED" "-Xlinker /MANIFEST:NO"
        CMAKE_C_LINK_EXECUTABLE "${CMAKE_C_LINK_EXECUTABLE}")
endif ()
