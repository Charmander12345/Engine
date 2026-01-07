add_library(Basics SHARED
    EngineObject.cpp
    EngineObject.h
    Object2D.cpp
    Object2D.h
    Object3D.cpp
    Object3D.h
)

set_target_properties(Basics PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(Basics
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/AssetManager
)

if (WIN32)
    set_target_properties(Basics PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
