add_library(Diagnostics SHARED
    DiagnosticsManager.cpp
    DiagnosticsManager.h
)

set_target_properties(Diagnostics PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(Diagnostics
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if (WIN32)
    set_target_properties(Diagnostics PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
