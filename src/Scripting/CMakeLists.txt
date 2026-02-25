add_library(Scripting SHARED
    PythonScripting.cpp
    PythonScripting.h
)

set_target_properties(Scripting PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(Scripting
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${Python3_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/src/Core
        ${CMAKE_SOURCE_DIR}/src/Renderer
        ${CMAKE_SOURCE_DIR}/src/AssetManager
        ${CMAKE_SOURCE_DIR}/src/Diagnostics
        ${CMAKE_SOURCE_DIR}/external/SDL3/include
        ${CMAKE_SOURCE_DIR}/src/Physics
)

target_link_libraries(Scripting
    PRIVATE
        SDL3::SDL3
        Renderer
        Logger
        AssetManager
        Diagnostics
        Core
        Physics
        ${Python3_LIBRARY_RELEASE}
)

if (WIN32)
    set_target_properties(Scripting PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
