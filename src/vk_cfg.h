#pragma once
#include <cstdint>

constexpr int NUM_TEXTURE_TYPES = 4;

constexpr int NUM_GBUFFER_ATTACHMENTS = 4;

constexpr uint32_t DIFFUSE_TEX_SLOT = 0;
constexpr uint32_t METALLIC_TEX_SLOT = 1;
constexpr uint32_t ROUGHNESS_TEX_SLOT = 2;
constexpr uint32_t NORMAL_MAP_SLOT = 3;
constexpr uint32_t TLAS_SLOT = 4;

constexpr uint32_t GBUFFER_POSITION_SLOT = 0;
constexpr uint32_t GBUFFER_NORMAL_SLOT = 1;
constexpr uint32_t GBUFFER_ALBEDO_SLOT = 2;
constexpr uint32_t GBUFFER_METALLIC_ROUGHNESS_SLOT = 3;

constexpr float DRAW_DISTANCE = 6000.0f;

constexpr size_t MAX_BINDING_SLOTS_PER_SET = 20;

