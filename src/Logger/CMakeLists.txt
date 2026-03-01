add_library(Logger SHARED
    Logger.h
    Logger.cpp
)
set_target_properties(Logger PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(Logger
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
if (WIN32)
    set_target_properties(Logger PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
