#pragma once

#include "assets/ModelAsset.h"
#include <string>
#include <utility>
#include <vector>

class TerrainGrassChunkModelAsset : public ModelAsset {
public:
  void setChunkGeometry(std::vector<ImportedGeometryVertex> vertices,
                        std::vector<uint32_t> indices,
                        ImportedMaterialData material,
                        std::string sourcePathValue = "terrain_grass_chunk") {
    sourcePath = std::move(sourcePathValue);

    std::vector<ImportedModelSubmesh> submeshes;
    std::vector<ImportedMaterialData> materials;
    if (!indices.empty()) {
      submeshes.push_back(ImportedModelSubmesh{
          .name = sourcePath,
          .indexOffset = 0,
          .indexCount = static_cast<uint32_t>(indices.size()),
          .materialIndex = 0,
          .shapeIndex = 0,
      });
      materials.push_back(std::move(material));
    }

    geometryMesh.setImportedGeometry(std::move(vertices), std::move(indices),
                                     std::move(submeshes),
                                     std::move(materials));
  }

  ImportedGeometryAsset &mesh() override { return geometryMesh; }
  const ImportedGeometryAsset &mesh() const override { return geometryMesh; }

  std::vector<ImportedMaterialData> &mutableMaterials() override {
    return geometryMesh.mutableMaterialsData();
  }

  const std::vector<ImportedMaterialData> &materials() const override {
    return geometryMesh.materialsData();
  }

  const std::vector<ImportedModelSubmesh> &submeshes() const override {
    return geometryMesh.submeshData();
  }

  const std::string &path() const override { return sourcePath; }

private:
  std::string sourcePath = "terrain_grass_chunk";
  ImportedGeometryAsset geometryMesh;
};
