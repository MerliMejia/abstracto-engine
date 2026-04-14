#pragma once

#include "resources/Mesh.h"

inline TypedMesh<Vertex> buildCameraGizmoMesh() {
  TypedMesh<Vertex> mesh;
  const glm::vec3 color(1.0f, 1.0f, 1.0f);
  const std::vector<Vertex> vertices = {
      {{-0.32f, -0.18f, -0.18f}, color, {0.0f, 0.0f}},
      {{0.32f, -0.18f, -0.18f}, color, {0.0f, 0.0f}},
      {{0.32f, 0.18f, -0.18f}, color, {0.0f, 0.0f}},
      {{-0.32f, 0.18f, -0.18f}, color, {0.0f, 0.0f}},
      {{-0.32f, -0.18f, 0.12f}, color, {0.0f, 0.0f}},
      {{0.32f, -0.18f, 0.12f}, color, {0.0f, 0.0f}},
      {{0.32f, 0.18f, 0.12f}, color, {0.0f, 0.0f}},
      {{-0.32f, 0.18f, 0.12f}, color, {0.0f, 0.0f}},
      {{-0.62f, -0.40f, 0.95f}, color, {0.0f, 0.0f}},
      {{0.62f, -0.40f, 0.95f}, color, {0.0f, 0.0f}},
      {{0.62f, 0.40f, 0.95f}, color, {0.0f, 0.0f}},
      {{-0.62f, 0.40f, 0.95f}, color, {0.0f, 0.0f}},
      {{0.0f, 0.32f, -0.10f}, color, {0.0f, 0.0f}},
      {{0.0f, 0.58f, -0.10f}, color, {0.0f, 0.0f}},
      {{0.0f, 0.0f, 1.18f}, color, {0.0f, 0.0f}},
  };
  const std::vector<uint32_t> indices = {
      0, 1, 1, 2, 2, 3, 3, 0,
      4, 5, 5, 6, 6, 7, 7, 4,
      0, 4, 1, 5, 2, 6, 3, 7,
      4, 8, 5, 9, 6, 10, 7, 11,
      8, 9, 9, 10, 10, 11, 11, 8,
      12, 13,
      5, 14,
  };
  mesh.setGeometry(vertices, indices);
  return mesh;
}
