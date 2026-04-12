add_library(Core SHARED
    EngineObject.cpp
    EngineObject.h
    "EngineLevel.cpp"
    "EngineLevel.h"
    "AudioManager.cpp"
    "AudioManager.h"
    "UndoRedoManager.cpp"
    "UndoRedoManager.h"
    "TransactionManager.cpp"
    "TransactionManager.h"
    "ShortcutManager.cpp"
    "ShortcutManager.h"
    "../Renderer/Texture.cpp"
    "../Renderer/Texture.h"
    "../Renderer/Material.h"
    "MathTypes.h"
    "IEditorBridge.h"
    "ECS/DataStructs/SparseSet.h"
    "ECS/ECS.h"
    "ECS/Components.h"
    "ECS/ComponentSerialization.h"
    "ECS/ECS.cpp" "Asset.cpp"
    "Asset.h" "Core.h"
    "InputActionManager.cpp"
    "InputActionManager.h"
    "Actor/Actor.h"
    "Actor/Actor.cpp"
    "Actor/ActorComponent.h"
    "Actor/ActorRegistry.h"
    "Actor/BuiltinComponents.h"
    "Actor/BuiltinActors.h"
    "Actor/World.h"
    "Actor/World.cpp"
    "Actor/CDO.h"
    "Actor/CDO.cpp"
    "Actor/ActorAssetData.h"
    "Actor/ActorAssetData.cpp"
    "NameID.h"
    "ObjectHandle.h"
    "Archive.h"
    "Archive.cpp"
    "Reflection.h"
    "Reflection.cpp"
    "ECS/ComponentReflection.h"
    "ECS/Archetype/ArchetypeTypes.h"
    "ECS/Archetype/Chunk.h"
    "ECS/Archetype/Archetype.h"
    "ECS/Archetype/ArchetypeECS.h"
    "ECS/Archetype/ArchetypeECS.cpp"
    "ECS/Archetype/MassProcessor.h"
    "ECS/Archetype/MassBridge.h"
    "ECS/Archetype/MassBridge.cpp"
    "Actor/GameFramework/GameMode.h"
    "Actor/GameFramework/GameState.h"
    "Actor/GameFramework/PlayerController.h"
    "Actor/GameFramework/Pawn.h"
    "AnimationBlending.h")

set_target_properties(Core PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(Core
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/AssetManager
        ${CMAKE_SOURCE_DIR}/src/Renderer
        ${CMAKE_SOURCE_DIR}/src/Diagnostics
        ${CMAKE_SOURCE_DIR}/src/Physics
        ${CMAKE_SOURCE_DIR}/src/Scripting
        ${CMAKE_SOURCE_DIR}/external
        ${CMAKE_SOURCE_DIR}/external/SDL3/include
        ${CMAKE_SOURCE_DIR}/ECS
        ${Python3_INCLUDE_DIRS}
)

target_link_libraries(Core PRIVATE
    Logger
    OpenAL::OpenAL
    SDL3::SDL3
)

if (WIN32)
    set_target_properties(Core PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
