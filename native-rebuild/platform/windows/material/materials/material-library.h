#pragma once

#include <cstring>
#include "material-definition.h"
#include "liquid-glass.h"
#include "frosted-glass.h"

namespace lume_materials {

// Add future material definitions to this table. The renderer only depends on
// this small interface, never on a shader's internals.
inline constexpr const MaterialDefinition* kMaterials[] = {
  &kLiquidGlassMaterial,
  &kFrostedGlassMaterial,
};

inline const MaterialDefinition& FindMaterial(const char* id) {
  if (id != nullptr) {
    if (std::strcmp(id, "frosted-glass-disc") == 0) return kFrostedGlassMaterial;
    for (const auto* material : kMaterials) {
      if (std::strcmp(id, material->id) == 0) return *material;
    }
  }
  // Keep older widget callers working if they do not send a material ID or
  // request a material that is not installed in this build.
  return kLiquidGlassMaterial;
}

}  // namespace lume_materials
