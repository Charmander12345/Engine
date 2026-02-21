#pragma once

#include <string>

namespace ECS { using Entity = unsigned int; }

struct LandscapeParams
{
    std::string name        { "Landscape" };
    float       width       { 100.0f };   // X extent
    float       depth       { 100.0f };   // Z extent
    int         subdivisionsX{ 32 };
    int         subdivisionsZ{ 32 };
};

class LandscapeManager
{
public:
    // Generates a flat grid mesh, saves it as a Model3D .asset inside the
    // project's Content/Landscape folder, creates an ECS entity and returns
    // its ID.  Returns 0 on failure.
    static ECS::Entity spawnLandscape(const LandscapeParams& params);
};
