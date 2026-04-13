#pragma once

#include "resources/Mesh.h"
#include <cmath>
#include <cstdint>
#include <glm/gtc/constants.hpp>

inline TypedMesh<Vertex> buildCharacterControllerRingMesh() {
  TypedMesh<Vertex> mesh;
  const glm::vec3 color(1.0f, 1.0f, 1.0f);
  constexpr uint32_t segmentCount = 48;

  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  vertices.reserve(segmentCount);
  indices.reserve(segmentCount * 2);

  for (uint32_t index = 0; index < segmentCount; ++index) {
    const float angle =
        glm::two_pi<float>() * static_cast<float>(index) /
        static_cast<float>(segmentCount);
    vertices.push_back(Vertex{
        .pos = {std::cos(angle), 0.0f, std::sin(angle)},
        .color = color,
        .texCoord = {0.0f, 0.0f},
    });
    indices.push_back(index);
    indices.push_back((index + 1) % segmentCount);
  }

  mesh.setGeometry(std::move(vertices), std::move(indices));
  return mesh;
}

inline TypedMesh<Vertex> buildCharacterControllerVerticalLineMesh() {
  TypedMesh<Vertex> mesh;
  const glm::vec3 color(1.0f, 1.0f, 1.0f);
  mesh.setGeometry(
      {
          {{0.0f, -1.0f, 0.0f}, color, {0.0f, 0.0f}},
          {{0.0f, 1.0f, 0.0f}, color, {0.0f, 0.0f}},
      },
      {0, 1});
  return mesh;
}
