#pragma once

#include <string>
#include <memory>

class Texture;

/// Loads a DDS file (S3TC / BCn compressed) and populates a Texture object.
/// Supports BC1 (DXT1), BC2 (DXT3), BC3 (DXT5), BC4 (ATI1/RGTC1),
/// BC5 (ATI2/RGTC2), and BC7 (BPTC) via the DX10 extended header.
/// Returns nullptr on failure.
std::shared_ptr<Texture> loadDDS(const std::string& filePath);
