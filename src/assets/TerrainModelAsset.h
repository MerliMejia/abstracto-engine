#pragma once

#include "assets/ModelAsset.h"
#include "world/Terrain.h"
#include <string>
#include <utility>
#include <vector>

class TerrainModelAsset : public ModelAsset {
public:
  void setTerrain(TerrainConfig config,
                  std::string sourcePathValue = "terrain") {
    sourcePath = std::move(sourcePathValue);
    terrainConfig = std::move(config);
    rebuild();
  }

  const TerrainConfig &config() const { return terrainConfig; }

  ImportedGeometryAsset &mesh() override { return geometryMesh; }
  const ImportedGeometryAsset &mesh() const override { return geometryMesh; }

  const std::vector<ImportedMaterialData> &materials() const override {
    return geometryMesh.materialsData();
  }

  std::vector<ImportedMaterialData> &mutableMaterials() override {
    return geometryMesh.mutableMaterialsData();
  }

  const std::vector<ImportedModelSubmesh> &submeshes() const override {
    return geometryMesh.submeshData();
  }

  const std::string &path() const override { return sourcePath; }

private:
  void rebuild() {
    TerrainMeshData terrainMesh = TerrainGenerator::buildMesh(terrainConfig);

    std::vector<ImportedGeometryVertex> vertices;
    vertices.reserve(terrainMesh.vertices.size());
    for (const auto &vertex : terrainMesh.vertices) {
      vertices.push_back(ImportedGeometryVertex{
          .pos = vertex.position,
          .normal = vertex.normal,
          .texCoord = vertex.uv,
          .color = vertex.color,
      });
    }

    std::vector<ImportedModelSubmesh> submeshes;
    std::vector<ImportedMaterialData> materials;
    if (!terrainMesh.indices.empty()) {
      submeshes.push_back(ImportedModelSubmesh{
          .name = sourcePath.empty() ? "terrain" : sourcePath,
          .indexOffset = 0,
          .indexCount = static_cast<uint32_t>(terrainMesh.indices.size()),
          .materialIndex = 0,
          .shapeIndex = 0,
      });
      materials.push_back(ImportedMaterialData{
          .name = "Terrain Material",
      });
    }

    geometryMesh.setImportedGeometry(std::move(vertices),
                                     std::move(terrainMesh.indices),
                                     std::move(submeshes),
                                     std::move(materials));
  }

  std::string sourcePath = "terrain";
  TerrainConfig terrainConfig{};
  ImportedGeometryAsset geometryMesh;
};
