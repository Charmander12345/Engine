#include "RenderResourceManager.h"

#include <memory>

#include "../Diagnostics/DiagnosticsManager.h"
#include "../Logger/Logger.h"

#include "../Basics/EngineLevel.h"
#include "../Basics/Object2D.h"
#include "../Basics/Object3D.h"

#include "OpenGLRenderer/OpenGLObject2D.h"
#include "OpenGLRenderer/OpenGLObject3D.h"
#include "OpenGLRenderer/OpenGLMaterial.h"

bool RenderResourceManager::prepareActiveLevel()
{
    auto& diagnostics = DiagnosticsManager::Instance();
    EngineLevel* level = diagnostics.getActiveLevel();
    if (!level)
        return false;

    auto& logger = Logger::Instance();
    logger.log(Logger::Category::Rendering, "RenderResourceManager: prepareActiveLevel() start", Logger::LogLevel::INFO);

    switch (diagnostics.getRHIType())
    {
    case DiagnosticsManager::RHIType::OpenGL:
        logger.log(Logger::Category::Rendering, "RenderResourceManager: selected backend OpenGL", Logger::LogLevel::INFO);
        return prepareOpenGL(*level);
    default:
        logger.log(Logger::Category::Rendering, "RenderResourceManager: unsupported backend", Logger::LogLevel::ERROR);
        return false;
    }
}

bool RenderResourceManager::prepareOpenGL(EngineLevel& level)
{
    auto& logger = Logger::Instance();

    const auto& objs = level.getWorldObjects();
    bool ok = true;

    logger.log(Logger::Category::Rendering,
        "RenderResourceManager: OpenGL prepare begin. objectCount=" + std::to_string(objs.size()),
        Logger::LogLevel::INFO);

    for (const auto& obj : objs)
    {
        if (!obj)
            continue;

        // 2D objects
        if (auto obj2d = std::dynamic_pointer_cast<Object2D>(obj))
        {
            logger.log(Logger::Category::Rendering,
                "RenderResourceManager: Object2D geometry: vertexFloats=" + std::to_string(obj2d->getVertices().size()) +
                ", indexCount=" + std::to_string(obj2d->getIndices().size()),
                Logger::LogLevel::INFO);
            logger.log(Logger::Category::Rendering, "RenderResourceManager: preparing Object2D '" + obj2d->getPath() + "'", Logger::LogLevel::INFO);
            if (!prepareOpenGLObject2D(obj2d))
            {
                logger.log(Logger::Category::Rendering, "RenderResourceManager: failed to prepare OpenGL resources for Object2D: " + obj2d->getPath(), Logger::LogLevel::ERROR);
                ok = false;
            }
            else
            {
                logger.log(Logger::Category::Rendering, "RenderResourceManager: prepared Object2D '" + obj2d->getPath() + "'", Logger::LogLevel::INFO);
            }
        }

        // 3D objects
        if (auto obj3d = std::dynamic_pointer_cast<Object3D>(obj))
        {
            logger.log(Logger::Category::Rendering,
                "RenderResourceManager: Object3D geometry: vertexFloats=" + std::to_string(obj3d->getVertices().size()) +
                ", indexCount=" + std::to_string(obj3d->getIndices().size()),
                Logger::LogLevel::INFO);
            logger.log(Logger::Category::Rendering, "RenderResourceManager: preparing Object3D '" + obj3d->getPath() + "'", Logger::LogLevel::INFO);
            if (!prepareOpenGLObject3D(obj3d))
            {
                logger.log(Logger::Category::Rendering, "RenderResourceManager: failed to prepare OpenGL resources for Object3D: " + obj3d->getPath(), Logger::LogLevel::ERROR);
                ok = false;
            }
            else
            {
                logger.log(Logger::Category::Rendering, "RenderResourceManager: prepared Object3D '" + obj3d->getPath() + "'", Logger::LogLevel::INFO);
            }
        }
    }

    logger.log(Logger::Category::Rendering,
        std::string("RenderResourceManager: OpenGL prepare end. result=") + (ok ? "success" : "failure"),
        ok ? Logger::LogLevel::INFO : Logger::LogLevel::WARNING);

    return ok;
}

bool RenderResourceManager::prepareOpenGLObject3D(const std::shared_ptr<Object3D>& obj3d)
{
    if (!obj3d)
        return false;

    // Only treat as prepared if the material is already backend-specific.
    {
        auto existing = obj3d->getMaterial();
        if (std::dynamic_pointer_cast<OpenGLMaterial>(existing))
        {
            return true;
        }
    }

    OpenGLObject3D glObj(obj3d);
    return glObj.prepare();
}

bool RenderResourceManager::prepareOpenGLObject2D(const std::shared_ptr<Object2D>& obj2d)
{
    if (!obj2d)
        return false;

    // Only treat as prepared if the material is already backend-specific.
    {
        auto existing = obj2d->getMaterial();
        if (std::dynamic_pointer_cast<OpenGLMaterial>(existing))
        {
            return true;
        }
    }

    OpenGLObject2D glObj(obj2d);
    return glObj.prepare();
}
