#pragma once

namespace lume_materials {

// A material owns only GPU programs and a stable identifier. The native
// renderer supplies the live scene texture and geometry constant buffer.
struct MaterialDefinition {
  const char* id;
  const char* vertexShader;
  const char* pixelShader;
};

}  // namespace lume_materials
