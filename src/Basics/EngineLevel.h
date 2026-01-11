#pragma once 

#include <vector>
#include "EngineObject.h"
#include "../AssetManager/AssetTypes.h"

class EngineLevel : public EngineObject
{
public:
	EngineLevel();
	~EngineLevel();

	std::vector<EngineObject>& getWorldObjects();

private:

	std::vector<EngineObject> Objects;

};