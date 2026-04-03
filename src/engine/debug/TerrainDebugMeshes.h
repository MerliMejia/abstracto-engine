#pragma once

#include "resources/Mesh.h"
#include "world/Terrain.h"
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <glm/gtc/constants.hpp>
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

inline TypedMesh<Vertex> buildTerrainBrushIndicatorMesh() {
  TypedMesh<Vertex> mesh;
  const glm::vec3 color(1.0f, 1.0f, 1.0f);
  constexpr uint32_t segmentCount = 48;

  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  vertices.reserve(segmentCount + 2);
  indices.reserve(segmentCount * 2 + 2);

  for (uint32_t index = 0; index < segmentCount; ++index) {
    const float angle =
        glm::two_pi<float>() * static_cast<float>(index) /
        static_cast<float>(segmentCount);
    vertices.push_back(Vertex{
        .pos = {std::cos(angle), 0.0f, std::sin(angle)},
        .color = color,
        .texCoord = {0.0f, 0.0f},
    });
  }

  for (uint32_t index = 0; index < segmentCount; ++index) {
    indices.push_back(index);
    indices.push_back((index + 1) % segmentCount);
  }

  const uint32_t lineStart = static_cast<uint32_t>(vertices.size());
  vertices.push_back(Vertex{
      .pos = {0.0f, 0.0f, 0.0f},
      .color = color,
      .texCoord = {0.0f, 0.0f},
  });
  vertices.push_back(Vertex{
      .pos = {0.0f, 1.0f, 0.0f},
      .color = color,
      .texCoord = {0.0f, 0.0f},
  });
  indices.push_back(lineStart);
  indices.push_back(lineStart + 1);

  mesh.setGeometry(std::move(vertices), std::move(indices));
  return mesh;
}
