#include "EngineLevel.h"

#include <algorithm>



EngineLevel::EngineLevel()
{
	this->setAssetType(AssetType::Level);
}

EngineLevel::~EngineLevel()
{

}

void EngineLevel::clearLoadedDependencies()
{
	Objects.clear();
}

bool EngineLevel::registerObject(const std::shared_ptr<EngineObject>& object, Transform transform, const std::string& groupID)
{
	if (!object || object->getPath().empty())
	{
		return false;
	}
	Objects.push_back(std::make_shared<ObjectInstance>(ObjectInstance{ object, transform, groupID }));
	return true;
}

bool EngineLevel::unregisterObject(const std::shared_ptr<EngineObject>& object)
{
	if (!object || object->getPath().empty())
	{
		return false;
	}

	for (auto it = Objects.begin(); it != Objects.end(); ++it)
	{
		if (*it && (*it)->object->getPath() == object->getPath())
		{
			Objects.erase(it);
			return true;
		}
	}

	return false;
}

std::vector<std::shared_ptr<EngineLevel::ObjectInstance>>& EngineLevel::getWorldObjects()
{
	return Objects;
}
std::vector<EngineLevel::group>& EngineLevel::getGroups()
{
	return m_groups;
}

bool EngineLevel::setObjectTransform(const std::string& objectPath, const Transform& transform)
{
	if (objectPath.empty())
	{
		return false;
	}

	for (auto& instance : Objects)
	{
		if (instance && instance->object->getPath() == objectPath)
		{
			instance->transform = transform;
			return true;
		}
	}
	return false;
}

void EngineLevel::disableInstancing(const std::string& groupID)
{
	if (groupID.empty())
	{
		return;
	}

	auto groupIt = std::find_if(m_groups.begin(), m_groups.end(), [&](const group& g) { return g.id == groupID; });
	if (groupIt == m_groups.end())
	{
		return;
	}

	// Re-create per-instance world entries so objects are rendered individually again.
	// Keep the entries in the group as well (they remain associated with groupID).
	for (size_t i = 0; i < groupIt->transforms.size(); ++i)
	{
		const Transform& t = groupIt->transforms[i];
		std::shared_ptr<EngineObject> obj;
		if (i < groupIt->objects.size())
		{
			obj = groupIt->objects[i];
		}
		else if (!groupIt->objects.empty())
		{
			// Fallback: if there are fewer objects than transforms, reuse the first.
			obj = groupIt->objects.front();
		}

		if (obj)
		{
			Objects.push_back(std::make_shared<ObjectInstance>(ObjectInstance{ obj, t, groupID }));
		}
	}

	groupIt->isInstanced = false;
}

bool EngineLevel::enableInstancing(const std::string& groupID)
{
	std::vector<ObjectInstance> objectsToInstance;
	objectsToInstance.reserve(Objects.size());

	for (const auto& obj : Objects)
	{
		if (obj && obj->groupID == groupID)
		{
			objectsToInstance.push_back(*obj);
		}
	}

	if (objectsToInstance.empty())
	{
		return false;
	}

	auto groupIt = std::find_if(m_groups.begin(), m_groups.end(), [&](const group& g) { return g.id == groupID; });
	if (groupIt == m_groups.end())
	{
		return false;
	}

	groupIt->isInstanced = true;
	groupIt->objects.clear();
	groupIt->transforms.clear();

	// Store per-instance objects and transforms (do NOT de-duplicate by asset path).
	for (const auto& inst : objectsToInstance)
	{
		groupIt->objects.push_back(inst.object);
		groupIt->transforms.push_back(inst.transform);
	}

	Objects.erase(
		std::remove_if(Objects.begin(), Objects.end(), [&](const std::shared_ptr<ObjectInstance>& inst)
			{
				return inst && inst->groupID == groupID;
			}),
		Objects.end());

	return true;
}

bool EngineLevel::createGroup(const std::string& groupID, bool instanced)
{
	for (const auto& g : m_groups)
	{
		if (g.id == groupID)
		{
			return false;
		}
	}
	group g;
	g.id = groupID;
	g.isInstanced = instanced;
	m_groups.push_back(g);
	return true;
}
void EngineLevel::deleteGroup(const std::string& groupID)
{
	for (auto it = m_groups.begin(); it != m_groups.end(); ++it)
	{
		if (it->id == groupID)
		{
			m_groups.erase(it);
			return;
		}
	}
}

