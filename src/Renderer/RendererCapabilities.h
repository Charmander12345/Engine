#pragma once

struct RendererCapabilities
{
    bool supportsShadows            = false;
    bool supportsOcclusion          = false;
    bool supportsWireframe          = false;
    bool supportsVSync              = false;
    bool supportsEntityPicking      = false;
    bool supportsGizmos             = false;
    bool supportsSkybox             = false;
    bool supportsPopupWindows       = false;
    bool supportsPostProcessing     = false;
    bool supportsTextureCompression = false;
};
