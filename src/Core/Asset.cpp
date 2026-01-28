#include "Asset.h"

AssetData::AssetData()
{

}

AssetData::~AssetData()
{

}

void AssetData::setId(unsigned int id)
{
	m_id = id;
}

unsigned int AssetData::getId() const
{
	return m_id;
}

void AssetData::setData(json data)
{
	m_data = data;
}

json& AssetData::getData()
{
	return m_data;
}

void AssetData::setType(AssetType type)
{
	m_type = type;
}

AssetType& AssetData::getType()
{
	return m_type;
}
