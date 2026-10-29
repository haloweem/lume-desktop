#include <windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include "materials/material-library.h"

// Compile the existing material sources with the runtime's original flags.
// Only this build tool depends on D3DCompiler; opening a panel needs no compiler.
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    std::ofstream output(argv[1], std::ios::binary | std::ios::trunc);
    if (!output) return 3;
    output << "#pragma once\nnamespace lume_material_bytecode {\n";
    const auto& liquid = lume_materials::FindMaterial("liquid-glass");
    const auto& frosted = lume_materials::FindMaterial("frosted-glass");
    struct Shader { const char* name; const char* source; const char* target; };
    for (const auto& shader : {
        Shader{"vertex", liquid.vertexShader, "vs_5_0"},
        Shader{"liquid", liquid.pixelShader, "ps_5_0"},
        Shader{"frosted", frosted.pixelShader, "ps_5_0"},
        Shader{"sceneBlit", "Texture2D t:register(t0); SamplerState s:register(s0); float4 main(float4 p:SV_POSITION,float2 uv:TEXCOORD0):SV_TARGET{return t.Sample(s,uv);}", "ps_5_0"},
        Shader{"contentBlit", "Texture2D t:register(t0); SamplerState s:register(s0); cbuffer C:register(b1){float4 k;}; float4 main(float4 p:SV_POSITION,float2 uv:TEXCOORD0):SV_TARGET{return t.Sample(s,uv)*k.x;}", "ps_5_0"},
        Shader{"sceneClear", "float4 main(float4 p:SV_POSITION):SV_TARGET{return 0;}", "ps_5_0"}}) {
        Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
        const auto hr = D3DCompile(shader.source, std::strlen(shader.source),
            nullptr, nullptr, nullptr, "main", shader.target,
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.GetAddressOf(), errors.GetAddressOf());
        if (FAILED(hr)) {
            std::cerr << "Material shader compilation failed: " << shader.name << '\n';
            if (errors) std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()),
                static_cast<std::streamsize>(errors->GetBufferSize()));
            return 4;
        }
        output << "inline constexpr unsigned char " << shader.name << "[] = {\n";
        const auto bytes = static_cast<const unsigned char*>(code->GetBufferPointer());
        for (std::size_t i = 0; i < code->GetBufferSize(); ++i) {
            output << static_cast<unsigned>(bytes[i]) << ',';
            if (i % 24 == 23) output << '\n';
        }
        output << "\n};\n";
    }
    output << "}\n";
    output.close();
    return output ? 0 : 5;
}
