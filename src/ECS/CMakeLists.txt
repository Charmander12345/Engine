add_library(ECS
    World.cpp
    Serialization/EcsLevelSerializer.cpp
)

target_include_directories(ECS
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(ECS PUBLIC cxx_std_20)
