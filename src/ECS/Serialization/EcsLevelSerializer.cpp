#include "ECS/Serialization/EcsLevelSerializer.h"

#include <fstream>
#include <unordered_map>
#include <vector>

#include "ECS/World.h"
#include "ECS/Serialization/BinaryIO.h"

#include "AssetManager/json.hpp"

namespace ecs::ser
{
	using json = nlohmann::json;

	bool saveWorldToStream(std::ostream& out, const ecs::World& world)
	{
		json root;
		root["magic"] = "ELVL";
		root["version"] = kEcsLevelVersion;
		root["entities"] = json::array();

		for (auto eid : world.ids().entities())
		{
			const auto* idc = world.ids().tryGet(eid);
			if (!idc || idc->guid == 0)
				continue;

			json e;
			e["guid"] = idc->guid;

			if (world.names().has(eid))
			{
				const auto& n = world.names().get(eid);
				e["name"] = n.name;
			}
			if (world.transforms().has(eid))
			{
				const auto& t = world.transforms().get(eid);
				e["transform"] = {
					{ "position", { t.position.x, t.position.y, t.position.z } },
					{ "rotation", { t.rotation.x, t.rotation.y, t.rotation.z } },
					{ "scale", { t.scale.x, t.scale.y, t.scale.z } }
				};
			}
			if (world.renders().has(eid))
			{
				const auto& r = world.renders().get(eid);
				e["render"] = {
					{ "mesh", r.meshAssetPath },
					{ "material", r.materialAssetPath },
					{ "tint", { r.overrides.tint.x, r.overrides.tint.y, r.overrides.tint.z } }
				};
			}
			if (world.groups().has(eid))
			{
				const auto& g = world.groups().get(eid);
				e["group"] = {
					{ "id", g.groupId },
					{ "instanced", g.instanced }
				};
			}

			root["entities"].push_back(std::move(e));
		}

		out << root.dump(2);
		return out.good();
	}

	bool loadWorldFromStream(std::istream& in, ecs::World& world)
	{
		json root;
		try
		{
			in >> root;
		}
		catch (...)
		{
			return false;
		}

		if (!root.is_object())
			return false;
		if (root.value("magic", "") != "ELVL")
			return false;
		if (root.value("version", 0u) != kEcsLevelVersion)
			return false;

		const auto& ents = root["entities"];
		if (!ents.is_array())
			return false;

		for (const auto& je : ents)
		{
			if (!je.is_object())
				continue;
			const std::uint64_t guid = je.value("guid", 0ull);
			if (guid == 0)
				continue;
			auto e = world.createEntityWithId(guid);

			if (je.contains("name"))
			{
				ecs::NameComponent n;
				n.name = je.value("name", std::string{});
				world.emplaceName(e, n);
			}
			if (je.contains("transform"))
			{
				const auto& jt = je["transform"];
				ecs::TransformComponent t;
				if (jt.contains("position") && jt["position"].is_array() && jt["position"].size() == 3)
				{
					t.position.x = jt["position"][0].get<float>();
					t.position.y = jt["position"][1].get<float>();
					t.position.z = jt["position"][2].get<float>();
				}
				if (jt.contains("rotation") && jt["rotation"].is_array() && jt["rotation"].size() == 3)
				{
					t.rotation.x = jt["rotation"][0].get<float>();
					t.rotation.y = jt["rotation"][1].get<float>();
					t.rotation.z = jt["rotation"][2].get<float>();
				}
				if (jt.contains("scale") && jt["scale"].is_array() && jt["scale"].size() == 3)
				{
					t.scale.x = jt["scale"][0].get<float>();
					t.scale.y = jt["scale"][1].get<float>();
					t.scale.z = jt["scale"][2].get<float>();
				}
				world.emplaceTransform(e, t);
			}
			if (je.contains("render"))
			{
				const auto& jr = je["render"];
				ecs::RenderComponent r;
				r.meshAssetPath = jr.value("mesh", std::string{});
				r.materialAssetPath = jr.value("material", std::string{});
				if (jr.contains("tint") && jr["tint"].is_array() && jr["tint"].size() == 3)
				{
					r.overrides.tint.x = jr["tint"][0].get<float>();
					r.overrides.tint.y = jr["tint"][1].get<float>();
					r.overrides.tint.z = jr["tint"][2].get<float>();
				}
				world.emplaceRender(e, r);
			}
			if (je.contains("group"))
			{
				const auto& jg = je["group"];
				ecs::GroupComponent g;
				g.groupId = jg.value("id", std::string{});
				g.instanced = jg.value("instanced", false);
				world.emplaceGroup(e, g);
			}
		}

		return true;
	}

	bool saveWorld(const std::string& filePath, const ecs::World& world)
	{
		std::ofstream out(filePath, std::ios::out);
		if (!out.is_open())
			return false;
		return saveWorldToStream(out, world);
	}

	bool loadWorld(const std::string& filePath, ecs::World& world)
	{
		std::ifstream in(filePath, std::ios::in);
		if (!in.is_open())
			return false;
		return loadWorldFromStream(in, world);
	}
}
