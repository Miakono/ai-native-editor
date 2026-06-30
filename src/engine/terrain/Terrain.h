#pragma once

#include "engine/cave/CaveVolume.h"
#include "engine/scene/Component.h"
#include "engine/scene/Entity.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace aine {

struct TerrainLayer {
    std::string name = "Layer";
    std::array<float, 4> displayColor{0.36f, 0.48f, 0.28f, 1.0f};
    std::string material;
    std::string albedoTexture;
    std::string normalTexture;
    std::string maskTexture;
    float tiling = 8.0f;
    float roughness = 0.85f;
    float metalness = 0.0f;
    float detailScale = 18.0f;
    float detailStrength = 0.12f;
};

enum class TerrainBackend {
    Heightfield,
    Volumetric
};

struct TerrainData {
    int version = 1;
    TerrainBackend backend = TerrainBackend::Heightfield;
    int resolution = 33;
    int chunkSize = 16;
    std::array<float, 3> size{32.0f, 6.0f, 32.0f};
    bool collisionEnabled = true;
    bool volumeEnabled = false;
    int editRevision = 0;
    int materialRevision = 0;
    std::vector<float> heights;
    std::vector<TerrainLayer> layers;
    std::vector<float> weights;
    std::vector<std::uint8_t> holes;
    std::vector<int> dirtyChunks;
    std::vector<int> dirtyMaterialChunks;
    TerrainVolumeData volume;
};

struct TerrainSample {
    bool valid = false;
    float height = 0.0f;
    std::array<float, 3> normal{0.0f, 1.0f, 0.0f};
    std::vector<float> layerWeights;
    bool hole = false;
};

struct TerrainRayHit {
    bool hit = false;
    float distance = 0.0f;
    std::array<float, 3> point{0.0f, 0.0f, 0.0f};
    std::array<float, 3> normal{0.0f, 1.0f, 0.0f};
    int entityId = 0;
    std::string entityName;
    std::string surfaceType = "Heightfield";
};

struct TerrainMeshVertex {
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    std::array<float, 3> normal{0.0f, 1.0f, 0.0f};
    std::array<float, 2> uv{0.0f, 0.0f};
    std::array<float, 4> color{0.36f, 0.48f, 0.28f, 1.0f};
};

struct TerrainMeshChunk {
    int index = 0;
    int chunkX = 0;
    int chunkZ = 0;
    int sampleMinX = 0;
    int sampleMaxX = 0;
    int sampleMinZ = 0;
    int sampleMaxZ = 0;
    std::array<float, 3> boundsMin{0.0f, 0.0f, 0.0f};
    std::array<float, 3> boundsMax{0.0f, 0.0f, 0.0f};
    std::vector<TerrainMeshVertex> vertices;
    std::vector<unsigned int> indices;
};

struct TerrainMesh {
    int version = 1;
    int resolution = 0;
    int chunkSize = 0;
    int chunkCountX = 0;
    int chunkCountZ = 0;
    std::array<float, 3> size{0.0f, 0.0f, 0.0f};
    std::array<float, 3> boundsMin{0.0f, 0.0f, 0.0f};
    std::array<float, 3> boundsMax{0.0f, 0.0f, 0.0f};
    std::vector<TerrainMeshChunk> chunks;
};

struct TerrainDirtyRegion {
    bool valid = false;
    int minX = 0;
    int maxX = 0;
    int minZ = 0;
    int maxZ = 0;
    std::vector<int> chunks;
};

enum class TerrainBrushMode {
    RaiseLower,
    Flatten,
    Smooth,
    SetHeight,
    Noise,
    Paint,
    Hole,
    VolumeTunnel,
    VolumeDig,
    VolumeFill,
    VolumeSmooth,
    VolumeFlatten,
    VolumePaint,
    AddMaterial = VolumeFill,
    RemoveMaterial = VolumeDig,
    PaintMaterial = VolumePaint
};

enum class TerrainFalloffCurve {
    Smooth,
    Linear,
    Constant,
    Bell
};

struct TerrainBrushSettings {
    TerrainBrushMode mode = TerrainBrushMode::RaiseLower;
    TerrainFalloffCurve falloff = TerrainFalloffCurve::Smooth;
    float radius = 2.5f;
    float strength = 0.45f;
    float opacity = 1.0f;
    float targetHeight = 1.0f;
    bool invert = false;
    float spacing = 0.35f;
    int activeLayerIndex = 0;
    unsigned int noiseSeed = 1337;
};

struct TerrainBrushResult {
    bool changed = false;
    bool materialOnly = false;
    int affectedSamples = 0;
    TerrainDirtyRegion dirty;
};

Component MakeTerrainComponent(int resolution = 33, std::array<float, 3> size = {32.0f, 6.0f, 32.0f},
                               int chunkSize = 16);

bool LoadTerrainDataFromComponent(const Component& component, TerrainData* outData, std::string* error = nullptr);
void SaveTerrainDataToComponent(const TerrainData& data, Component* component);
void NormalizeTerrainData(TerrainData* data);

int TerrainSampleCount(const TerrainData& data);
int TerrainLayerCount(const TerrainData& data);
int TerrainChunkCountX(const TerrainData& data);
int TerrainChunkCountZ(const TerrainData& data);
int TerrainChunkIndex(const TerrainData& data, int chunkX, int chunkZ);
std::vector<int> TerrainChunksForDirtyRegion(const TerrainData& data, int minX, int maxX, int minZ, int maxZ);
void ClearTerrainDirtyChunks(Component* component);

bool TerrainWorldToUv(const TerrainData& data, const Entity& entity, std::array<float, 3> world,
                      std::array<float, 2>* outUv);
std::array<float, 3> TerrainUvToWorld(const TerrainData& data, const Entity& entity, std::array<float, 2> uv,
                                      float localHeight);
TerrainSample SampleTerrainLocal(const TerrainData& data, float localX, float localZ);
TerrainSample SampleTerrainWorld(const TerrainData& data, const Entity& entity, std::array<float, 3> world);
float TerrainHeightAtSample(const TerrainData& data, int x, int z);
std::array<float, 3> TerrainNormalAtSample(const TerrainData& data, int x, int z);
bool TerrainRaycastWorld(const TerrainData& data, const Entity& entity, std::array<float, 3> origin,
                         std::array<float, 3> direction, float maxDistance, TerrainRayHit* outHit = nullptr,
                         bool includeHoles = false);
float TerrainLayerWeightAtUv(const TerrainData& data, int layerIndex, std::array<float, 2> uv);
bool TerrainIsHoleAtUv(const TerrainData& data, std::array<float, 2> uv);
TerrainBackend TerrainBackendFromString(std::string value);
const char* TerrainBackendLabel(TerrainBackend backend);
bool TerrainUsesHeightfield(const TerrainData& data);
bool TerrainUsesVolumetric(const TerrainData& data);
bool TerrainHasVolume(const TerrainData& data);
void EnsureTerrainVolume(TerrainData* data);
Entity TerrainVolumeProxyEntity(const TerrainData& data, const Entity& terrainEntity);
bool TerrainVolumeWorldToLocal(const TerrainData& data, const Entity& entity, std::array<float, 3> world,
                               std::array<float, 3>* outLocal);
std::array<float, 3> TerrainVolumeLocalToWorld(const TerrainData& data, const Entity& entity,
                                               std::array<float, 3> local);
bool TerrainVolumeRaycastWorld(const TerrainData& data, const Entity& entity, std::array<float, 3> origin,
                               std::array<float, 3> direction, float maxDistance,
                               TerrainVolumeRayHit* outHit = nullptr);
bool TerrainVolumeContainsAirAtWorld(const TerrainData& data, const Entity& entity, std::array<float, 3> world);
TerrainVolumeBrushResult ApplyTerrainVolumeBrush(TerrainData* data, std::array<float, 3> centerLocal,
                                                 const TerrainVolumeBrushSettings& settings);

float EvaluateTerrainBrushFalloff(float normalizedDistance, TerrainFalloffCurve curve);
TerrainBrushResult ApplyTerrainBrush(TerrainData* data, std::array<float, 2> centerUv,
                                     const TerrainBrushSettings& settings);
void NormalizeTerrainLayerWeights(TerrainData* data);
void NormalizeTerrainLayerWeightsAt(TerrainData* data, int sampleIndex);

TerrainMesh BuildTerrainMesh(const TerrainData& data);
TerrainMeshChunk BuildTerrainMeshChunk(const TerrainData& data, int chunkX, int chunkZ);
bool RebuildTerrainMeshChunks(const TerrainData& data, const std::vector<int>& chunkIndices, TerrainMesh* mesh);
bool RefreshTerrainMeshChunkMaterials(const TerrainData& data, const std::vector<int>& chunkIndices, TerrainMesh* mesh);

std::string TerrainBrushModeLabel(TerrainBrushMode mode);
std::string TerrainFalloffLabel(TerrainFalloffCurve curve);
bool RunTerrainSelfTests(std::vector<std::string>* diagnostics);

} // namespace aine
