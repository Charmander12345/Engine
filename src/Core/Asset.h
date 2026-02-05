#pragma once 
#include "EngineObject.h"
#include "../AssetManager/json.hpp"
#include "AssetTypes.h"

using json = nlohmann::json;

class AssetData : public EngineObject
{
public:
	AssetData();
	~AssetData();
	//Set the assets ID.
	void setId(unsigned int id);
	//Get the assets ID.
	unsigned int getId() const;
	//Set the Assets data as a json object.
	void setData(json data);
	//Get the Assets data as a json object.
	json& getData();
	//Set the assets type.
	void setType(AssetType type);
	//Get the assets type.
	AssetType& getType();
private:
	unsigned int m_id{ 0 };
	json m_data;
	AssetType m_type{ AssetType::Unknown };
};