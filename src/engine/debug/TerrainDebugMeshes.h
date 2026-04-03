#pragma once

#include "resources/Mesh.h"
#include "world/Terrain.h"
#include <algorithm>
#include <cstdint>
#include <unordered_set>

inline TypedMesh<Vertex> buildTerrainWireframeMesh(const TerrainConfig &config) {
  TypedMesh<Vertex> mesh;
  const TerrainMeshData terrainMesh = TerrainGenerator::buildMesh(config);
  const glm::vec3 color(1.0f, 1.0f, 1.0f);

  std::vector<Vertex> vertices;
  vertices.reserve(terrainMesh.vertices.size());
  for (const auto &terrainVertex : terrainMesh.vertices) {
    vertices.push_back(Vertex{
        .pos = terrainVertex.position,
        .color = color,
        .texCoord = terrainVertex.uv,
    });
  }

  std::vector<uint32_t> lineIndices;
  lineIndices.reserve(terrainMesh.indices.size() * 2);
  std::unordered_set<uint64_t> uniqueEdges;

  auto appendEdge = [&](uint32_t a, uint32_t b) {
    if (a == b || a >= vertices.size() || b >= vertices.size()) {
      return;
    }
    const uint32_t minIndex = std::min(a, b);
    const uint32_t maxIndex = std::max(a, b);
    const uint64_t key =
        (static_cast<uint64_t>(minIndex) << 32) | maxIndex;
    if (!uniqueEdges.insert(key).second) {
      return;
    }
    lineIndices.push_back(a);
    lineIndices.push_back(b);
  };

  for (size_t index = 0; index + 2 < terrainMesh.indices.size(); index += 3) {
    const uint32_t i0 = terrainMesh.indices[index];
    const uint32_t i1 = terrainMesh.indices[index + 1];
    const uint32_t i2 = terrainMesh.indices[index + 2];
    appendEdge(i0, i1);
    appendEdge(i1, i2);
    appendEdge(i2, i0);
  }

  mesh.setGeometry(std::move(vertices), std::move(lineIndices));
  return mesh;
}
