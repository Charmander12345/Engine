#include "EngineLevel.h"

EngineLevel::EngineLevel()
{
	this->setAssetType(AssetType::Level);
}

EngineLevel::~EngineLevel()
{

}

std::vector<EngineObject>& EngineLevel::getWorldObjects()
{
	return Objects;
}

const std::unordered_map<std::string, Transform>& EngineLevel::getWorldObjectTransforms() const
{
	return m_objectTransforms;
}

const std::unordered_map<std::string, std::shared_ptr<EngineObject>>& EngineLevel::getLoadedDependencies() const
{
	return m_loadedDependencies;
}

void EngineLevel::clearLoadedDependencies()
{
	m_loadedDependencies.clear();
}

void EngineLevel::setLoadedDependency(const std::string& path, const std::shared_ptr<EngineObject>& obj)
{
	if (path.empty() || !obj)
	{
		return;
	}
	m_loadedDependencies[path] = obj;
}

bool EngineLevel::registerObject(const EngineObject& object)
{
	if (object.getPath().empty())
	{
		return false;
	}

	Objects.push_back(object);
	m_objectTransforms[object.getPath()] = object.getTransform();
	return true;
}

bool EngineLevel::unregisterObject(const EngineObject& object)
{
	if (object.getPath().empty())
	{
		return false;
	}

	for (auto it = Objects.begin(); it != Objects.end(); ++it)
	{
		if (it->getPath() == object.getPath())
		{
			Objects.erase(it);
			m_objectTransforms.erase(object.getPath());
			m_loadedDependencies.erase(object.getPath());
			return true;
		}
	}

	return false;
}

bool EngineLevel::setObjectTransform(const EngineObject& object, const Transform& transform)
{
	if (object.getPath().empty())
	{
		return false;
	}

	m_objectTransforms[object.getPath()] = transform;
	return true;
}
