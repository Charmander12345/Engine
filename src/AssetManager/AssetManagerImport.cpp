// ---------------------------------------------------------------------------
// AssetManagerImport.cpp – Split implementation file for AssetManager
// Contains: Import dialog, asset import pipeline (Assimp, texture, audio, etc.)
// ---------------------------------------------------------------------------
#include "AssetManager.h"

#if ENGINE_EDITOR

#include <filesystem>
#include <fstream>
#include <cstdint>
#include <cctype>

#include "stb_image.h"
#include "AssetTypes.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace fs = std::filesystem;

namespace
{
	struct ImportDialogContext
	{
		AssetType preferredType{ AssetType::Unknown };
		unsigned int ActionID{ 0 };
	};

	// Map file extension -> asset type (extensible)
	static AssetType DetectAssetTypeFromPath(const fs::path& p)
	{
		std::string ext = p.extension().string();
		for (auto& c : ext)
		{
			if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
		}
		// Textures
		if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" || ext == ".hdr" || ext == ".dds")
			return AssetType::Texture;

		if (ext == ".wav")
			return AssetType::Audio;

		// 3D Models (Assimp-supported)
		if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb" ||
			ext == ".dae" || ext == ".3ds" || ext == ".blend" || ext == ".stl" ||
			ext == ".ply" || ext == ".x3d")
			return AssetType::Model3D;

		// Shaders
		if (ext == ".glsl" || ext == ".vert" || ext == ".frag" || ext == ".geom" || ext == ".comp")
			return AssetType::Shader;

		// Scripts
		if (ext == ".py")
			return AssetType::Script;

		return AssetType::Unknown;
	}


	static void SDLCALL OnImportDialogClosed(void* userdata, const char* const* filelist, int filter)
	{
		Logger::Instance().log(Logger::Category::AssetManagement, "Import dialog closed callback invoked.", Logger::LogLevel::INFO);
		auto* ctx = static_cast<ImportDialogContext*>(userdata);
		if (!ctx)
		{
			Logger::Instance().log(Logger::Category::AssetManagement, "Import dialog context is null!", Logger::LogLevel::ERROR);
			return;
		}

		if (!filelist || !filelist[0])
		{
			Logger::Instance().log(Logger::Category::AssetManagement, "Import dialog cancelled or no file selected.", Logger::LogLevel::INFO);
			DiagnosticsManager::Instance().updateActionProgress(ctx->ActionID, false);
			delete ctx;
			return;
		}

		const std::string selectedPath = filelist[0];
		Logger::Instance().log(Logger::Category::AssetManagement, "Queueing import job for file: " + selectedPath, Logger::LogLevel::INFO);
		AssetManager::Instance().importAssetFromPath(selectedPath, ctx->preferredType, ctx->ActionID);
		delete ctx;
	}

	static std::string sanitizeName(const std::string& name)
	{
		std::string out;
		out.reserve(name.size());
		for (char c : name)
		{
			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')
			{
				out.push_back(c);
			}
			else
			{
				out.push_back('_');
			}
		}
		if (out.empty()) out = "Imported";
		return out;
	}
} // anonymous namespace

bool AssetManager::OpenImportDialog(SDL_Window* parentWindow /* = nullptr */, AssetType forcedType /* = AssetType::Unknown */, SyncState syncState /* = Sync */)
{
    auto& logger = Logger::Instance();
    auto& diagnostics = DiagnosticsManager::Instance();
	auto action = diagnostics.registerAction(DiagnosticsManager::ActionType::ImportingAsset);
    if (parentWindow)
    {
        ImportDialogContext* ctx = new ImportDialogContext();
        ctx->preferredType = forcedType;
		ctx->ActionID = action.ID;

        // SDL3: Filter als SDL_DialogFileFilter-Array
        SDL_DialogFileFilter filters[] = {
            { "All Supported", "png;jpg;jpeg;bmp;tga;hdr;wav;obj;fbx;gltf;glb;dae;3ds;blend;stl;ply;x3d;glsl;vert;frag;geom;comp;py" },
            { "Image Files", "png;jpg;jpeg;bmp;tga;hdr" },
            { "3D Models", "obj;fbx;gltf;glb;dae;3ds;blend;stl;ply;x3d" },
            { "Audio Files", "wav" },
            { "Shaders", "glsl;vert;frag;geom;comp" },
            { "Scripts", "py" },
            { "All Files", "*" }
        };

        logger.log(Logger::Category::AssetManagement, "Opening import asset dialog with SDL...", Logger::LogLevel::INFO);

        // Korrekte SDL3-Funktion verwenden
        SDL_ShowOpenFileDialog(
            OnImportDialogClosed, // Callback (bereits im File definiert)
            ctx,                  // Userdata
            parentWindow,         // Window
            filters,              // Filter-Array
            SDL_arraysize(filters), // Anzahl Filter
            nullptr,              // Default Location
            false                 // allow_many: nur eine Datei
        );
        return true;
    }
    logger.log(Logger::Category::AssetManagement, "Opening import asset dialog...", Logger::LogLevel::INFO);
    return false;
}

void AssetManager::importAssetFromPath(std::string path, AssetType preferredType, unsigned int ActionID)
{
	auto& logger = Logger::Instance();
	auto& diagnostics = DiagnosticsManager::Instance();
	logger.log(Logger::Category::AssetManagement, "Importing asset from path: " + path, Logger::LogLevel::INFO);

	if (!diagnostics.isProjectLoaded())
	{
		logger.log(Logger::Category::AssetManagement, "Import failed: no project loaded.", Logger::LogLevel::ERROR);
		diagnostics.enqueueToastNotification("Import failed: no project loaded.", 4.0f, DiagnosticsManager::NotificationLevel::Error);
		diagnostics.updateActionProgress(ActionID, false);
		return;
	}

	if (!fs::exists(path))
	{
		logger.log(Logger::Category::AssetManagement, "Import asset failed: file does not exist: " + path, Logger::LogLevel::ERROR);
		diagnostics.enqueueToastNotification("Import failed: file not found.", 4.0f, DiagnosticsManager::NotificationLevel::Error);
		diagnostics.updateActionProgress(ActionID, false);
		return;
	}

	const fs::path sourcePath(path);
	const AssetType detectedType = (preferredType != AssetType::Unknown)
		? preferredType
		: DetectAssetTypeFromPath(sourcePath);

	if (detectedType == AssetType::Unknown)
	{
		logger.log(Logger::Category::AssetManagement, "Import failed: unsupported file format: " + sourcePath.extension().string(), Logger::LogLevel::ERROR);
		diagnostics.enqueueToastNotification("Import failed: unsupported format " + sourcePath.extension().string(), 4.0f, DiagnosticsManager::NotificationLevel::Error);
		diagnostics.updateActionProgress(ActionID, false);
		return;
	}

	const std::string assetName = sanitizeName(sourcePath.stem().string());
	const fs::path contentDir = fs::path(diagnostics.getProjectInfo().projectPath) / "Content";
	const fs::path destAssetPath = contentDir / (assetName + ".asset");
	const std::string relPath = fs::relative(destAssetPath, contentDir).generic_string();

	json data = json::object();
	bool success = false;

	switch (detectedType)
	{
	case AssetType::Texture:
	{
		// Copy source file to Content folder
		const fs::path destSourcePath = contentDir / sourcePath.filename();
		std::error_code ec;
		fs::copy_file(sourcePath, destSourcePath, fs::copy_options::overwrite_existing, ec);

		const std::string relSourcePath = fs::relative(destSourcePath, fs::path(diagnostics.getProjectInfo().projectPath)).generic_string();
		data["m_sourcePath"] = relSourcePath;

		std::string srcExt = sourcePath.extension().string();
		for (auto& c : srcExt)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

		if (srcExt == ".dds")
		{
			// DDS files are loaded at runtime via DDSLoader; store minimal metadata
			data["m_compressed"] = true;
			data["m_width"] = 0;
			data["m_height"] = 0;
			data["m_channels"] = 0;
		}
		else
		{
			int width = 0, height = 0, channels = 0;
			unsigned char* imgData = stbi_load(path.c_str(), &width, &height, &channels, 4);
			if (!imgData)
			{
				logger.log(Logger::Category::AssetManagement, "Import failed: stb_image could not load: " + path, Logger::LogLevel::ERROR);
				break;
			}
			channels = 4;

			data["m_width"] = width;
			data["m_height"] = height;
			data["m_channels"] = channels;

			stbi_image_free(imgData);
		}

		success = true;
		break;
	}

	case AssetType::Audio:
	{
		// Copy source .wav to Content folder
		const fs::path destSourcePath = contentDir / sourcePath.filename();
		std::error_code ec;
		fs::copy_file(sourcePath, destSourcePath, fs::copy_options::overwrite_existing, ec);

		const std::string relSourcePath = fs::relative(destSourcePath, fs::path(diagnostics.getProjectInfo().projectPath)).generic_string();
		data["m_sourcePath"] = relSourcePath;
		data["m_format"] = "wav";
		success = true;
		break;
	}

	case AssetType::Model3D:
	{
		Assimp::Importer importer;
		const aiScene* scene = importer.ReadFile(path,
			aiProcess_Triangulate |
			aiProcess_GenNormals |
			aiProcess_FlipUVs |
			aiProcess_JoinIdenticalVertices |
			aiProcess_OptimizeMeshes |
			aiProcess_LimitBoneWeights);

		if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE))
		{
			logger.log(Logger::Category::AssetManagement, "Import failed: Assimp error: " + std::string(importer.GetErrorString()), Logger::LogLevel::ERROR);
			break;
		}

		// ── Log scene contents ──
		logger.log(Logger::Category::AssetManagement,
			"Import 3D model '" + assetName + "': " + std::to_string(scene->mNumMeshes) + " mesh(es), "
			+ std::to_string(scene->mNumMaterials) + " material(s), "
			+ std::to_string(scene->mNumTextures) + " embedded texture(s), "
			+ std::to_string(scene->mNumAnimations) + " animation(s)",
			Logger::LogLevel::INFO);

		for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi)
		{
			const aiMesh* m = scene->mMeshes[mi];
			std::string meshName = m->mName.length > 0 ? m->mName.C_Str() : ("Mesh_" + std::to_string(mi));
			int uvChannels = 0;
			for (int ch = 0; ch < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++ch)
			{
				if (m->mTextureCoords[ch]) ++uvChannels; else break;
			}
			logger.log(Logger::Category::AssetManagement,
				"  Mesh[" + std::to_string(mi) + "] '" + meshName + "': "
				+ std::to_string(m->mNumVertices) + " vertices, "
				+ std::to_string(m->mNumFaces) + " faces, "
				+ std::to_string(uvChannels) + " UV channel(s), "
				+ (m->HasNormals() ? "normals" : "no normals") + ", "
				+ (m->HasTangentsAndBitangents() ? "tangents" : "no tangents") + ", "
				+ "materialIndex=" + std::to_string(m->mMaterialIndex),
				Logger::LogLevel::INFO);
		}

		for (unsigned int mi = 0; mi < scene->mNumMaterials; ++mi)
		{
			const aiMaterial* mat = scene->mMaterials[mi];
			aiString matNameLog;
			mat->Get(AI_MATKEY_NAME, matNameLog);
			std::string mName = matNameLog.length > 0 ? matNameLog.C_Str() : ("Material_" + std::to_string(mi));

			auto countTex = [&](aiTextureType t) { return mat->GetTextureCount(t); };
			logger.log(Logger::Category::AssetManagement,
				"  Material[" + std::to_string(mi) + "] '" + mName + "': "
				+ std::to_string(countTex(aiTextureType_DIFFUSE)) + " diffuse, "
				+ std::to_string(countTex(aiTextureType_SPECULAR)) + " specular, "
				+ std::to_string(countTex(aiTextureType_NORMALS)) + " normal, "
				+ std::to_string(countTex(aiTextureType_HEIGHT)) + " height, "
				+ std::to_string(countTex(aiTextureType_AMBIENT)) + " ambient, "
				+ std::to_string(countTex(aiTextureType_EMISSIVE)) + " emissive",
				Logger::LogLevel::INFO);
		}

		// Collect all meshes into a single vertex/index buffer (pos3 + uv2 layout)
		std::vector<float> vertices;
		std::vector<uint32_t> indices;
		uint32_t indexOffset = 0;

		for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
		{
			const aiMesh* mesh = scene->mMeshes[m];
			for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
			{
				vertices.push_back(mesh->mVertices[v].x);
				vertices.push_back(mesh->mVertices[v].y);
				vertices.push_back(mesh->mVertices[v].z);

				if (mesh->mTextureCoords[0])
				{
					vertices.push_back(mesh->mTextureCoords[0][v].x);
					vertices.push_back(mesh->mTextureCoords[0][v].y);
				}
				else
				{
					vertices.push_back(0.0f);
					vertices.push_back(0.0f);
				}
			}

			for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
			{
				const aiFace& face = mesh->mFaces[f];
				for (unsigned int i = 0; i < face.mNumIndices; ++i)
				{
					indices.push_back(indexOffset + face.mIndices[i]);
				}
			}

			indexOffset += mesh->mNumVertices;
		}

		data["m_vertices"] = vertices;
		data["m_indices"] = indices;

		logger.log(Logger::Category::AssetManagement,
			"Import 3D model: " + std::to_string(vertices.size() / 5) + " vertices, " + std::to_string(indices.size()) + " indices",
			Logger::LogLevel::INFO);

		// ── Extract bone data for skeletal animation ──
		bool hasBones = false;
		for (unsigned int mi = 0; mi < scene->mNumMeshes && !hasBones; ++mi)
		{
			if (scene->mMeshes[mi]->mNumBones > 0)
				hasBones = true;
		}

		if (hasBones)
		{
			data["m_hasBones"] = true;

			// Build bone list and per-vertex bone weights
			const size_t totalVertices = vertices.size() / 5;
			std::vector<float> boneIdsFlat(totalVertices * 4, 0.0f);
			std::vector<float> boneWeightsFlat(totalVertices * 4, 0.0f);

			// Track per-vertex how many bones have been assigned
			std::vector<int> boneSlots(totalVertices, 0);

			std::unordered_map<std::string, int> boneNameToIndex;
			json bonesJson = json::array();

			uint32_t globalVertexOffset = 0;
			for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi)
			{
				const aiMesh* mesh = scene->mMeshes[mi];
				for (unsigned int bi = 0; bi < mesh->mNumBones; ++bi)
				{
					const aiBone* bone = mesh->mBones[bi];
					std::string boneName = bone->mName.C_Str();

					int boneIndex = -1;
					auto it = boneNameToIndex.find(boneName);
					if (it != boneNameToIndex.end())
					{
						boneIndex = it->second;
					}
					else
					{
						boneIndex = static_cast<int>(boneNameToIndex.size());
						boneNameToIndex[boneName] = boneIndex;

						// Store offset matrix (4x4, row-major)
						json boneJson;
						boneJson["name"] = boneName;
						const auto& om = bone->mOffsetMatrix;
						boneJson["offsetMatrix"] = {
							om.a1, om.a2, om.a3, om.a4,
							om.b1, om.b2, om.b3, om.b4,
							om.c1, om.c2, om.c3, om.c4,
							om.d1, om.d2, om.d3, om.d4
						};
						bonesJson.push_back(boneJson);
					}

					// Assign weights to vertices
					for (unsigned int wi = 0; wi < bone->mNumWeights; ++wi)
					{
						unsigned int vertexId = globalVertexOffset + bone->mWeights[wi].mVertexId;
						float weight = bone->mWeights[wi].mWeight;
						if (vertexId < totalVertices)
						{
							int slot = boneSlots[vertexId];
							if (slot < 4)
							{
								boneIdsFlat[vertexId * 4 + slot] = static_cast<float>(boneIndex);
								boneWeightsFlat[vertexId * 4 + slot] = weight;
								boneSlots[vertexId] = slot + 1;
							}
							else
							{
								// Replace the slot with smallest weight
								int minSlot = 0;
								for (int s = 1; s < 4; ++s)
									if (boneWeightsFlat[vertexId*4+s] < boneWeightsFlat[vertexId*4+minSlot])
										minSlot = s;
								if (weight > boneWeightsFlat[vertexId*4+minSlot])
								{
									boneIdsFlat[vertexId*4+minSlot] = static_cast<float>(boneIndex);
									boneWeightsFlat[vertexId*4+minSlot] = weight;
								}
							}
						}
					}
				}
				globalVertexOffset += mesh->mNumVertices;
			}

			// Normalize bone weights per vertex
			for (size_t v = 0; v < totalVertices; ++v)
			{
				float total = 0;
				for (int j = 0; j < 4; ++j) total += boneWeightsFlat[v*4+j];
				if (total > 0.0f) {
					float inv = 1.0f / total;
					for (int j = 0; j < 4; ++j) boneWeightsFlat[v*4+j] *= inv;
				}
			}

			data["m_bones"] = bonesJson;
			data["m_boneIds"] = boneIdsFlat;
			data["m_boneWeights"] = boneWeightsFlat;

			// ── Build node hierarchy ──
			{
				json nodesJson = json::array();
				std::unordered_map<const aiNode*, int> nodeMap;
				std::function<void(const aiNode*, int)> buildNodes = [&](const aiNode* node, int parentIdx)
				{
					int idx = static_cast<int>(nodesJson.size());
					nodeMap[node] = idx;
					json nj;
					nj["name"] = std::string(node->mName.C_Str());
					nj["parent"] = parentIdx;
					const auto& t = node->mTransformation;
					nj["transform"] = {
						t.a1, t.a2, t.a3, t.a4,
						t.b1, t.b2, t.b3, t.b4,
						t.c1, t.c2, t.c3, t.c4,
						t.d1, t.d2, t.d3, t.d4
					};
					auto bit = boneNameToIndex.find(std::string(node->mName.C_Str()));
					nj["boneIndex"] = (bit != boneNameToIndex.end()) ? bit->second : -1;
					nodesJson.push_back(nj);
					for (unsigned int c = 0; c < node->mNumChildren; ++c)
						buildNodes(node->mChildren[c], idx);
				};
				buildNodes(scene->mRootNode, -1);
				data["m_nodes"] = nodesJson;
			}

			// ── Extract animations ──
			if (scene->mNumAnimations > 0)
			{
				json animsJson = json::array();
				for (unsigned int ai = 0; ai < scene->mNumAnimations; ++ai)
				{
					const aiAnimation* anim = scene->mAnimations[ai];
					json animJson;
					animJson["name"] = std::string(anim->mName.C_Str());
					animJson["duration"] = anim->mDuration;
					animJson["ticksPerSecond"] = (anim->mTicksPerSecond > 0) ? anim->mTicksPerSecond : 25.0;

					json channelsJson = json::array();
					for (unsigned int ci = 0; ci < anim->mNumChannels; ++ci)
					{
						const aiNodeAnim* ch = anim->mChannels[ci];
						json chJson;
						chJson["boneName"] = std::string(ch->mNodeName.C_Str());

						json posKeys = json::array();
						for (unsigned int k = 0; k < ch->mNumPositionKeys; ++k)
						{
							const auto& pk = ch->mPositionKeys[k];
							posKeys.push_back({ {"t", pk.mTime}, {"v", {pk.mValue.x, pk.mValue.y, pk.mValue.z}} });
						}
						chJson["positionKeys"] = posKeys;

						json rotKeys = json::array();
						for (unsigned int k = 0; k < ch->mNumRotationKeys; ++k)
						{
							const auto& rk = ch->mRotationKeys[k];
							rotKeys.push_back({ {"t", rk.mTime}, {"q", {rk.mValue.x, rk.mValue.y, rk.mValue.z, rk.mValue.w}} });
						}
						chJson["rotationKeys"] = rotKeys;

						json sclKeys = json::array();
						for (unsigned int k = 0; k < ch->mNumScalingKeys; ++k)
						{
							const auto& sk = ch->mScalingKeys[k];
							sclKeys.push_back({ {"t", sk.mTime}, {"v", {sk.mValue.x, sk.mValue.y, sk.mValue.z}} });
						}
						chJson["scalingKeys"] = sclKeys;

						channelsJson.push_back(chJson);
					}
					animJson["channels"] = channelsJson;
					animsJson.push_back(animJson);
				}
				data["m_animations"] = animsJson;
			}

			logger.log(Logger::Category::AssetManagement,
				"Import 3D model: " + std::to_string(boneNameToIndex.size()) + " bones, "
				+ std::to_string(scene->mNumAnimations) + " animation(s)",
				Logger::LogLevel::INFO);
		}

		// ── Extract materials and textures from the Assimp scene ──
		if (scene->HasMaterials())
		{
			const fs::path sourceDir = sourcePath.parent_path();
			const fs::path texturesDir = contentDir / "Textures";
			const fs::path materialsDir = contentDir / "Materials";
			std::error_code dirEc;
			fs::create_directories(texturesDir, dirEc);
			fs::create_directories(materialsDir, dirEc);

			json createdMaterials = json::array();
			int createdTextureCount = 0;

			for (unsigned int mi = 0; mi < scene->mNumMaterials; ++mi)
			{
				const aiMaterial* aiMat = scene->mMaterials[mi];
				std::string matName = (scene->mNumMaterials == 1)
					? assetName
					: (assetName + "_Material_" + std::to_string(mi));

				json matData = json::object();
				json textureAssetPaths = json::array();

				// Helper: import a texture of a given Assimp type
				auto importTexture = [&](aiTextureType texType, const std::string& label) -> std::string
				{
					if (aiMat->GetTextureCount(texType) == 0)
						return {};

					aiString aiTexPath;
					if (aiMat->GetTexture(texType, 0, &aiTexPath) != AI_SUCCESS)
						return {};

					std::string texPathStr(aiTexPath.C_Str());
					if (texPathStr.empty())
						return {};

					// Check for embedded textures (path starts with '*')
					const aiTexture* embeddedTex = scene->GetEmbeddedTexture(texPathStr.c_str());
					fs::path destImagePath;
					std::string texAssetName;

					if (embeddedTex)
					{
						// Embedded texture – write to disk
						std::string embeddedExt = ".png";
						if (embeddedTex->achFormatHint[0] != '\0')
						{
							embeddedExt = std::string(".") + embeddedTex->achFormatHint;
						}
						texAssetName = sanitizeName(assetName + "_" + label);
						destImagePath = texturesDir / (texAssetName + embeddedExt);

						if (embeddedTex->mHeight == 0)
						{
							// Compressed data (e.g. PNG/JPG stored as-is)
							std::ofstream imgOut(destImagePath, std::ios::binary);
							if (imgOut.is_open())
							{
								imgOut.write(reinterpret_cast<const char*>(embeddedTex->pcData), embeddedTex->mWidth);
							}
						}
						else
						{
							// Raw RGBA pixel data – write as TGA
							destImagePath.replace_extension(".tga");
							std::ofstream imgOut(destImagePath, std::ios::binary);
							if (imgOut.is_open())
							{
								const int w = static_cast<int>(embeddedTex->mWidth);
								const int h = static_cast<int>(embeddedTex->mHeight);
								uint8_t header[18] = {};
								header[2] = 2;
								header[12] = static_cast<uint8_t>(w & 0xFF);
								header[13] = static_cast<uint8_t>((w >> 8) & 0xFF);
								header[14] = static_cast<uint8_t>(h & 0xFF);
								header[15] = static_cast<uint8_t>((h >> 8) & 0xFF);
								header[16] = 32;
								header[17] = 0x28;
								imgOut.write(reinterpret_cast<const char*>(header), 18);
								for (int y = 0; y < h; ++y)
								{
									for (int x = 0; x < w; ++x)
									{
										const auto& px = embeddedTex->pcData[y * w + x];
										const uint8_t bgra[4] = { px.b, px.g, px.r, px.a };
										imgOut.write(reinterpret_cast<const char*>(bgra), 4);
									}
								}
							}
						}
					}
					else
					{
						// External texture file – resolve relative to source model directory
						fs::path externalPath = fs::path(texPathStr);
						if (!externalPath.is_absolute())
						{
							externalPath = sourceDir / externalPath;
						}
						if (!fs::exists(externalPath))
						{
							logger.log(Logger::Category::AssetManagement,
								"Import: texture file not found: " + externalPath.string(),
								Logger::LogLevel::WARNING);
							return {};
						}
						texAssetName = sanitizeName(assetName + "_" + label);
						destImagePath = texturesDir / externalPath.filename();
						std::error_code cpEc;
						fs::copy_file(externalPath, destImagePath, fs::copy_options::skip_existing, cpEc);
					}

					if (texAssetName.empty() || !fs::exists(destImagePath))
						return {};

					// Create texture .asset file
					const std::string texRelSourcePath = fs::relative(destImagePath, fs::path(diagnostics.getProjectInfo().projectPath)).generic_string();
					const fs::path texAssetPath = texturesDir / (texAssetName + ".asset");
					const std::string texRelPath = fs::relative(texAssetPath, contentDir).generic_string();

					// Skip if texture asset already exists
					if (!fs::exists(texAssetPath))
					{
						int width = 0, height = 0, channels = 0;
						unsigned char* imgData = stbi_load(destImagePath.string().c_str(), &width, &height, &channels, 4);
						if (imgData)
						{
							channels = 4;
							json texData = json::object();
							texData["m_sourcePath"] = texRelSourcePath;
							texData["m_width"] = width;
							texData["m_height"] = height;
							texData["m_channels"] = channels;

							json texFileJson = json::object();
							texFileJson["magic"] = 0x41535453;
							texFileJson["version"] = 2;
							texFileJson["type"] = static_cast<int>(AssetType::Texture);
							texFileJson["name"] = texAssetName;
							texFileJson["data"] = texData;

							std::ofstream texOut(texAssetPath, std::ios::out | std::ios::trunc);
							if (texOut.is_open())
							{
								texOut << texFileJson.dump(4);
								texOut.close();

								AssetRegistryEntry texRegEntry;
								texRegEntry.name = texAssetName;
								texRegEntry.path = texRelPath;
								texRegEntry.type = AssetType::Texture;
								registerAssetInRegistry(texRegEntry);

								logger.log(Logger::Category::AssetManagement,
										"Import: created texture asset: " + texAssetName,
										Logger::LogLevel::INFO);
									createdTextureCount++;
								}
							stbi_image_free(imgData);
						}
					}

					return texRelPath;
				};

				// Import diffuse texture
				std::string diffuseTexPath = importTexture(aiTextureType_DIFFUSE, "Diffuse");

				// Import specular texture
				std::string specularTexPath = importTexture(aiTextureType_SPECULAR, "Specular");

				// Import normal map
				std::string normalTexPath = importTexture(aiTextureType_NORMALS, "Normal");
				if (normalTexPath.empty())
				{
					normalTexPath = importTexture(aiTextureType_HEIGHT, "Normal");
				}

				// Import emissive map
				std::string emissiveTexPath = importTexture(aiTextureType_EMISSIVE, "Emissive");

				// Import metallic/roughness map (PBR)
				std::string metallicRoughnessTexPath = importTexture(aiTextureType_METALNESS, "MetallicRoughness");
				if (metallicRoughnessTexPath.empty())
				{
					metallicRoughnessTexPath = importTexture(aiTextureType_DIFFUSE_ROUGHNESS, "MetallicRoughness");
				}
				if (metallicRoughnessTexPath.empty())
				{
					metallicRoughnessTexPath = importTexture(aiTextureType_UNKNOWN, "MetallicRoughness");
				}

				// Build texture array with fixed slot ordering:
				// Slot 0 = Diffuse, Slot 1 = Specular, Slot 2 = Normal, Slot 3 = Emissive, Slot 4 = MetallicRoughness
				// Use null entries for missing intermediate slots so indices stay correct.
				{
					int lastSlot = -1;
					if (!metallicRoughnessTexPath.empty()) lastSlot = 4;
					else if (!emissiveTexPath.empty()) lastSlot = 3;
					else if (!normalTexPath.empty()) lastSlot = 2;
					else if (!specularTexPath.empty()) lastSlot = 1;
					else if (!diffuseTexPath.empty()) lastSlot = 0;

					const std::string* slotPaths[] = { &diffuseTexPath, &specularTexPath, &normalTexPath, &emissiveTexPath, &metallicRoughnessTexPath };
					for (int s = 0; s <= lastSlot; ++s)
					{
						if (!slotPaths[s]->empty())
							textureAssetPaths.push_back(*slotPaths[s]);
						else
							textureAssetPaths.push_back(nullptr); // null → RenderResourceManager pushes nullptr
					}
				}

				if (!textureAssetPaths.empty())
				{
					matData["m_textureAssetPaths"] = textureAssetPaths;
				}

				// Extract material properties
				aiColor3D diffuseColor(1.0f, 1.0f, 1.0f);
				if (aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuseColor) == AI_SUCCESS)
				{
					matData["m_diffuseColor"] = json{ {"x", diffuseColor.r}, {"y", diffuseColor.g}, {"z", diffuseColor.b} };
				}

				aiColor3D specularColor(0.0f, 0.0f, 0.0f);
				if (aiMat->Get(AI_MATKEY_COLOR_SPECULAR, specularColor) == AI_SUCCESS)
				{
					matData["m_specularColor"] = json{ {"x", specularColor.r}, {"y", specularColor.g}, {"z", specularColor.b} };
				}

				float shininess = 32.0f;
				if (aiMat->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS)
				{
					matData["m_shininess"] = shininess;
				}

				// PBR metallic/roughness properties
				float metallicFactor = 0.0f;
				if (aiMat->Get(AI_MATKEY_METALLIC_FACTOR, metallicFactor) == AI_SUCCESS)
				{
					matData["m_metallic"] = metallicFactor;
				}
				float roughnessFactor = 0.5f;
				if (aiMat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughnessFactor) == AI_SUCCESS)
				{
					matData["m_roughness"] = roughnessFactor;
				}
				// Auto-enable PBR when metallic/roughness data is present
				if (matData.contains("m_metallic") || matData.contains("m_roughness") || !metallicRoughnessTexPath.empty())
				{
					matData["m_pbrEnabled"] = true;
				}

				// Write material .asset file
				const fs::path matAssetPath = materialsDir / (matName + ".asset");
				const std::string matRelPath = fs::relative(matAssetPath, contentDir).generic_string();

				if (!fs::exists(matAssetPath))
				{
					json matFileJson = json::object();
					matFileJson["magic"] = 0x41535453;
					matFileJson["version"] = 2;
					matFileJson["type"] = static_cast<int>(AssetType::Material);
					matFileJson["name"] = matName;
					matFileJson["data"] = matData;

					std::ofstream matOut(matAssetPath, std::ios::out | std::ios::trunc);
					if (matOut.is_open())
					{
						matOut << matFileJson.dump(4);
						matOut.close();

						AssetRegistryEntry matRegEntry;
						matRegEntry.name = matName;
						matRegEntry.path = matRelPath;
						matRegEntry.type = AssetType::Material;
						registerAssetInRegistry(matRegEntry);

						logger.log(Logger::Category::AssetManagement,
							"Import: created material asset: " + matName,
							Logger::LogLevel::INFO);
					}
				}

				createdMaterials.push_back(matRelPath);
			}

			if (!createdMaterials.empty())
			{
				data["m_materialAssetPaths"] = createdMaterials;
				logger.log(Logger::Category::AssetManagement,
					"Import: created " + std::to_string(createdMaterials.size()) + " material(s) for model " + assetName,
					Logger::LogLevel::INFO);
			}

			// Enhanced toast for 3D models showing material/texture counts
			{
				std::string toastMsg = "Imported " + assetName;
				if (!createdMaterials.empty() || createdTextureCount > 0)
				{
					toastMsg += " with";
					if (!createdMaterials.empty())
						toastMsg += " " + std::to_string(createdMaterials.size()) + " material(s)";
					if (!createdMaterials.empty() && createdTextureCount > 0)
						toastMsg += " and";
					if (createdTextureCount > 0)
						toastMsg += " " + std::to_string(createdTextureCount) + " texture(s)";
				}
				diagnostics.enqueueToastNotification(toastMsg, 4.0f, DiagnosticsManager::NotificationLevel::Success);
			}
		}

		success = true;
		break;
	}

	case AssetType::Shader:
	{
		// Copy shader source to Content folder
		const fs::path destSourcePath = contentDir / sourcePath.filename();
		std::error_code ec;
		fs::copy_file(sourcePath, destSourcePath, fs::copy_options::overwrite_existing, ec);

		const std::string relSourcePath = fs::relative(destSourcePath, fs::path(diagnostics.getProjectInfo().projectPath)).generic_string();
		data["m_sourcePath"] = relSourcePath;
		data["m_shaderType"] = sourcePath.extension().string();
		success = true;
		break;
	}

	case AssetType::Script:
	{
		// Copy script to Content folder
		const fs::path destSourcePath = contentDir / sourcePath.filename();
		std::error_code ec;
		fs::copy_file(sourcePath, destSourcePath, fs::copy_options::overwrite_existing, ec);

		const std::string relSourcePath = fs::relative(destSourcePath, fs::path(diagnostics.getProjectInfo().projectPath)).generic_string();
		data["m_sourcePath"] = relSourcePath;
		data["m_scriptPath"] = relSourcePath;
		success = true;
		break;
	}

	default:
		logger.log(Logger::Category::AssetManagement, "Import: unhandled asset type for: " + path, Logger::LogLevel::WARNING);
		break;
	}

	if (!success)
	{
		const std::string failName = sourcePath.filename().string();
		diagnostics.enqueueToastNotification("Import failed: " + failName, 4.0f, DiagnosticsManager::NotificationLevel::Error);
		diagnostics.updateActionProgress(ActionID, false);
		return;
	}

	// Write the .asset file
	std::error_code ec;
	fs::create_directories(destAssetPath.parent_path(), ec);

	std::ofstream out(destAssetPath, std::ios::out | std::ios::trunc);
	if (!out.is_open())
	{
		logger.log(Logger::Category::AssetManagement, "Import failed: could not create .asset file: " + destAssetPath.string(), Logger::LogLevel::ERROR);
		diagnostics.enqueueToastNotification("Import failed: could not write asset file.", 4.0f, DiagnosticsManager::NotificationLevel::Error);
		diagnostics.updateActionProgress(ActionID, false);
		return;
	}

	json fileJson = json::object();
	fileJson["magic"] = 0x41535453;
	fileJson["version"] = 2;
	fileJson["type"] = static_cast<int>(detectedType);
	fileJson["name"] = assetName;
	fileJson["data"] = data;

	out << fileJson.dump(4);
	out.close();

	// Register in asset registry
	AssetRegistryEntry regEntry;
	regEntry.name = assetName;
	regEntry.path = relPath;
	regEntry.type = detectedType;
	registerAssetInRegistry(regEntry);

	diagnostics.updateActionProgress(ActionID, false);
	logger.log(Logger::Category::AssetManagement,
		"Import successful: " + assetName + " (" + relPath + ")",
		Logger::LogLevel::INFO);
	// Model3D sends its own detailed toast with material/texture counts
	if (detectedType != AssetType::Model3D)
		diagnostics.enqueueToastNotification("Imported: " + assetName, 3.0f, DiagnosticsManager::NotificationLevel::Success);

	if (m_onImportCompleted)
	{
		m_onImportCompleted();
	}
}

#endif // ENGINE_EDITOR
