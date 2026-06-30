#pragma once

#include "engine/scene/Component.h"
#include "engine/scene/Entity.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace aine {

struct TerrainVolumeLayer {
    std::string name = "Rock";
    std::array<float, 4> displayColor{0.42f, 0.41f, 0.39f, 1.0f};
    std::string material = "CaveRock";
    std::string albedoTexture;
    float tiling = 8.0f;
    float roughness = 0.94f;
    float metalness = 0.0f;
};

struct TerrainVolumeData {
    int version = 1;
    std::array<int, 3> resolution{25, 17, 25};
    int chunkSize = 8;
    std::array<float, 3> size{28.0f, 14.0f, 28.0f};
    bool collisionEnabled = true;
    std::string collisionUpdateMode = "Deferred";
    int editRevision = 0;
    int materialRevision = 0;
    std::vector<float> densities;
    std::vector<TerrainVolumeLayer> layers;
    std::vector<float> weights;
    std::vector<int> dirtyChunks;
    std::vector<int> dirtyMaterialChunks;
};

struct TerrainVolumeSample {
    bool valid = false;
    float density = 1.0f;
    std::array<float, 3> normal{0.0f, 1.0f, 0.0f};
    std::vector<float> layerWeights;
};

struct TerrainVolumeRayHit {
    bool hit = false;
    float distance = 0.0f;
    std::array<float, 3> point{0.0f, 0.0f, 0.0f};
    std::array<float, 3> normal{0.0f, 1.0f, 0.0f};
    int entityId = 0;
    std::string entityName;
};

struct TerrainVolumeMeshVertex {
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    std::array<float, 3> normal{0.0f, 1.0f, 0.0f};
    std::array<float, 3> uvw{0.0f, 0.0f, 0.0f};
    std::array<float, 4> color{0.42f, 0.41f, 0.39f, 1.0f};
};

struct TerrainVolumeMeshChunk {
    int index = 0;
    int chunkX = 0;
    int chunkY = 0;
    int chunkZ = 0;
    int sampleMinX = 0;
    int sampleMaxX = 0;
    int sampleMinY = 0;
    int sampleMaxY = 0;
    int sampleMinZ = 0;
    int sampleMaxZ = 0;
    std::array<float, 3> boundsMin{0.0f, 0.0f, 0.0f};
    std::array<float, 3> boundsMax{0.0f, 0.0f, 0.0f};
    std::vector<TerrainVolumeMeshVertex> vertices;
    std::vector<unsigned int> indices;
};

struct TerrainVolumeMesh {
    int version = 1;
    std::array<int, 3> resolution{0, 0, 0};
    int chunkSize = 0;
    int chunkCountX = 0;
    int chunkCountY = 0;
    int chunkCountZ = 0;
    std::array<float, 3> size{0.0f, 0.0f, 0.0f};
    std::array<float, 3> boundsMin{0.0f, 0.0f, 0.0f};
    std::array<float, 3> boundsMax{0.0f, 0.0f, 0.0f};
    std::vector<TerrainVolumeMeshChunk> chunks;
};

struct TerrainVolumeMeshValidationChunk {
    int chunkIndex = 0;
    std::array<int, 3> chunkCoord{0, 0, 0};
    int vertexCount = 0;
    int triangleCount = 0;
    int invalidIndexCount = 0;
    int nanVertexCount = 0;
    int nanNormalCount = 0;
    int degenerateTriangleCount = 0;
    int openEdgeCount = 0;
    int nonManifoldEdgeCount = 0;
    int upwardNormalCount = 0;
    int sideNormalCount = 0;
    int downwardNormalCount = 0;
    std::array<float, 3> boundsMin{0.0f, 0.0f, 0.0f};
    std::array<float, 3> boundsMax{0.0f, 0.0f, 0.0f};
    std::array<int, 3> sampleMin{0, 0, 0};
    std::array<int, 3> sampleMax{0, 0, 0};
};

struct TerrainVolumeMeshValidationResult {
    bool valid = false;
    int sampleCount = 0;
    int solidSampleCount = 0;
    int airSampleCount = 0;
    int nearZeroSampleCount = 0;
    int nanDensityCount = 0;
    int vertexCount = 0;
    int triangleCount = 0;
    int invalidIndexCount = 0;
    int nanVertexCount = 0;
    int nanNormalCount = 0;
    int degenerateTriangleCount = 0;
    int openEdgeCount = 0;
    int boundaryOpenEdgeCount = 0;
    int interiorOpenEdgeCount = 0;
    int interiorOpenEdgeTolerance = 0;
    int nonManifoldEdgeCount = 0;
    int nonManifoldEdgeTolerance = 0;
    int upwardNormalCount = 0;
    int sideNormalCount = 0;
    int downwardNormalCount = 0;
    int mixedSignCellCount = 0;
    int mixedSignCellsWithoutTriangles = 0;
    std::array<float, 3> boundsMin{0.0f, 0.0f, 0.0f};
    std::array<float, 3> boundsMax{0.0f, 0.0f, 0.0f};
    std::vector<TerrainVolumeMeshValidationChunk> chunks;
};

struct TerrainVolumeDirtyRegion {
    bool valid = false;
    int minX = 0;
    int maxX = 0;
    int minY = 0;
    int maxY = 0;
    int minZ = 0;
    int maxZ = 0;
    std::vector<int> chunks;
};

enum class TerrainVolumeBrushMode {
    RemoveMaterial,
    AddMaterial,
    Smooth,
    Flatten,
    Tunnel,
    PaintMaterial,
    Dig = RemoveMaterial,
    Fill = AddMaterial,
    Paint = PaintMaterial
};

enum class TerrainVolumeFalloffCurve {
    Smooth,
    Linear,
    Constant,
    Bell
};

struct TerrainVolumeBrushSettings {
    TerrainVolumeBrushMode mode = TerrainVolumeBrushMode::RemoveMaterial;
    TerrainVolumeFalloffCurve falloff = TerrainVolumeFalloffCurve::Smooth;
    float radius = 2.0f;
    float strength = 0.85f;
    float opacity = 1.0f;
    float targetDensity = 0.0f;
    bool invert = false;
    float spacing = 0.35f;
    int activeLayerIndex = 0;
    bool useSegment = false;
    std::array<float, 3> segmentStart{0.0f, 0.0f, 0.0f};
    std::array<float, 3> segmentEnd{0.0f, 0.0f, 0.0f};
};

struct TerrainVolumeBrushResult {
    bool changed = false;
    bool materialOnly = false;
    int affectedSamples = 0;
    TerrainVolumeDirtyRegion dirty;
};

struct TerrainVolumeComponent {
    TerrainVolumeData data;
};

struct TerrainChunk {
    int index = 0;
    std::array<int, 3> originCell{0, 0, 0};
    std::array<int, 3> sizeCells{0, 0, 0};
};

struct TerrainBrush {
    TerrainVolumeBrushSettings settings;
};

struct TerrainEditCommand {
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    TerrainVolumeBrushSettings brush;
};

struct TerrainMaterialField {
    std::vector<TerrainVolumeLayer> layers;
    std::vector<float> weights;
};

struct TerrainSurfaceExtractor {
    static TerrainVolumeMesh Extract(const TerrainVolumeData& data);
};

struct TerrainMeshBuilder {
    static TerrainVolumeMesh Build(const TerrainVolumeData& data);
};

struct TerrainCollisionBuilder {
    static TerrainVolumeMesh BuildTriangleCollisionMesh(const TerrainVolumeData& data);
};

struct TerrainSerializer {
    static bool Load(const Component& component, TerrainVolumeData* outData, std::string* error = nullptr);
    static void Save(const TerrainVolumeData& data, Component* component);
};

using CaveLayer = TerrainVolumeLayer;
using CaveVolumeData = TerrainVolumeData;
using CaveSample = TerrainVolumeSample;
using CaveRayHit = TerrainVolumeRayHit;
using CaveMeshVertex = TerrainVolumeMeshVertex;
using CaveMeshChunk = TerrainVolumeMeshChunk;
using CaveMesh = TerrainVolumeMesh;
using CaveDirtyRegion = TerrainVolumeDirtyRegion;
using CaveBrushMode = TerrainVolumeBrushMode;
using CaveFalloffCurve = TerrainVolumeFalloffCurve;
using CaveBrushSettings = TerrainVolumeBrushSettings;
using CaveBrushResult = TerrainVolumeBrushResult;

Component MakeCaveVolumeComponent(std::array<int, 3> resolution = {25, 17, 25},
                                  std::array<float, 3> size = {28.0f, 14.0f, 28.0f},
                                  int chunkSize = 8);

bool LoadCaveVolumeDataFromComponent(const Component& component, CaveVolumeData* outData,
                                     std::string* error = nullptr);
void SaveCaveVolumeDataToComponent(const CaveVolumeData& data, Component* component);
void NormalizeCaveVolumeData(CaveVolumeData* data);

int CaveSampleCount(const CaveVolumeData& data);
int CaveLayerCount(const CaveVolumeData& data);
int CaveSampleIndex(const CaveVolumeData& data, int x, int y, int z);
int CaveChunkCountX(const CaveVolumeData& data);
int CaveChunkCountY(const CaveVolumeData& data);
int CaveChunkCountZ(const CaveVolumeData& data);
int CaveChunkIndex(const CaveVolumeData& data, int chunkX, int chunkY, int chunkZ);
std::vector<int> CaveChunksForDirtyRegion(const CaveVolumeData& data, int minX, int maxX, int minY, int maxY,
                                          int minZ, int maxZ);

bool CaveWorldToLocal(const CaveVolumeData& data, const Entity& entity, std::array<float, 3> world,
                      std::array<float, 3>* outLocal);
std::array<float, 3> CaveLocalToWorld(const CaveVolumeData& data, const Entity& entity,
                                      std::array<float, 3> local);
bool CaveLocalToUv(const CaveVolumeData& data, std::array<float, 3> local, std::array<float, 3>* outUv);
std::array<float, 3> CaveUvToLocal(const CaveVolumeData& data, std::array<float, 3> uv);
CaveSample SampleCaveLocal(const CaveVolumeData& data, std::array<float, 3> local);
CaveSample SampleCaveWorld(const CaveVolumeData& data, const Entity& entity, std::array<float, 3> world);
float CaveDensityAtSample(const CaveVolumeData& data, int x, int y, int z);
std::array<float, 3> CaveNormalLocal(const CaveVolumeData& data, std::array<float, 3> local);
float CaveLayerWeightAtLocal(const CaveVolumeData& data, int layerIndex, std::array<float, 3> local);
bool CaveRaycastWorld(const CaveVolumeData& data, const Entity& entity, std::array<float, 3> origin,
                      std::array<float, 3> direction, float maxDistance, CaveRayHit* outHit = nullptr);

float EvaluateCaveBrushFalloff(float normalizedDistance, CaveFalloffCurve curve);
CaveBrushResult ApplyCaveBrush(CaveVolumeData* data, std::array<float, 3> centerLocal,
                               const CaveBrushSettings& settings);
void NormalizeCaveLayerWeights(CaveVolumeData* data);
void NormalizeCaveLayerWeightsAt(CaveVolumeData* data, int sampleIndex);

CaveMesh BuildCaveMesh(const CaveVolumeData& data);
CaveMeshChunk BuildCaveMeshChunk(const CaveVolumeData& data, int chunkX, int chunkY, int chunkZ);
bool RebuildCaveMeshChunks(const CaveVolumeData& data, const std::vector<int>& chunkIndices, CaveMesh* mesh);
bool RefreshCaveMeshChunkMaterials(const CaveVolumeData& data, const std::vector<int>& chunkIndices, CaveMesh* mesh);
TerrainVolumeMeshValidationResult ValidateCaveMesh(const CaveVolumeData& data, const CaveMesh& mesh);
std::string FormatCaveMeshValidation(const TerrainVolumeMeshValidationResult& validation);

std::string CaveBrushModeLabel(CaveBrushMode mode);
std::string CaveFalloffLabel(CaveFalloffCurve curve);
bool RunCaveVolumeSelfTests(std::vector<std::string>* diagnostics);

} // namespace aine
