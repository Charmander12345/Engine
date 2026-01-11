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
