#pragma once

#include <string>
#include "../AssetManager/AssetTypes.h"
#include "../AssetManager/json.hpp"
#include "MathTypes.h"

using json = nlohmann::json;

class EngineObject
{
public:
    EngineObject() = default;
    virtual ~EngineObject() = default;

    virtual void render() {}

	bool getIsSaved() const { return isSaved; }
    void setIsSaved(bool saved) { isSaved = saved; }

    void setPath(const std::string& path) { m_path = path; }
    const std::string& getPath() const { return m_path; }

    void setName(const std::string& name) { m_name = name; }
    const std::string& getName() const { return m_name; }

    void setAssetType(AssetType type) { m_type = type; }
    AssetType getAssetType() const { return m_type; }

    void setTransform(const Transform& t) { m_transform = t; }
    const Transform& getTransform() const { return m_transform; }

private:

    std::string m_path;
    std::string m_name;
    AssetType m_type{AssetType::Unknown};
	bool isSaved{ false };

    Transform m_transform{};
};
