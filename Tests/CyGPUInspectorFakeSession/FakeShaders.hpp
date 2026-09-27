// CyGPUInspectorFakeSession — the shaders of the fake frame, as real HLSL.
//
// The fake session used to send empty DXBC shells: enough for the lists, not for the disassembly,
// the decompilers, the HLSL editor or the AI assistant, which then had nothing to show in the demo.
// These are small but real shaders of a deferred renderer, compiled at start-up by the compiler
// Windows ships (d3dcompiler_47.dll), so everything downstream works on genuine byte code.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <cstdint>

namespace fake
{
	struct ShaderSource
	{
		uint32_t shader_id;
		const char *target;
		const char *hlsl;
	};

	inline constexpr ShaderSource kShaderSources[] = {
		{ 1, "vs_5_0", R"hlsl(
cbuffer View : register(b0) { float4x4 ViewProjection; };
cbuffer Object : register(b1) { float4x4 World; };

float4 main(float3 position : POSITION) : SV_Position
{
    return mul(ViewProjection, mul(World, float4(position, 1.0)));
}
)hlsl" },

		{ 2, "ps_5_0", R"hlsl(
Texture2D Opacity : register(t0);
SamplerState LinearWrap : register(s0);

void main(float4 position : SV_Position, float2 uv : TEXCOORD0)
{
    clip(Opacity.Sample(LinearWrap, uv).a - 0.5);
}
)hlsl" },

		{ 3, "vs_5_0", R"hlsl(
cbuffer View : register(b0) { float4x4 ViewProjection; };
cbuffer Object : register(b1) { float4x4 World; };

struct Input { float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
struct Output { float4 position : SV_Position; float3 normal : NORMAL; float2 uv : TEXCOORD0; };

Output main(Input input)
{
    Output output;
    float4 world = mul(World, float4(input.position, 1.0));
    output.position = mul(ViewProjection, world);
    output.normal = normalize(mul((float3x3)World, input.normal));
    output.uv = input.uv;
    return output;
}
)hlsl" },

		{ 4, "ps_5_0", R"hlsl(
Texture2D Albedo : register(t0);
Texture2D NormalMap : register(t1);
Texture2D Material : register(t2);
SamplerState Anisotropic : register(s0);

struct Input { float4 position : SV_Position; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
struct Output { float4 albedo : SV_Target0; float4 normal : SV_Target1; float4 material : SV_Target2; };

Output main(Input input)
{
    Output output;
    output.albedo = Albedo.Sample(Anisotropic, input.uv);
    float3 detail = NormalMap.Sample(Anisotropic, input.uv).xyz * 2.0 - 1.0;
    float3 normal = normalize(input.normal + detail * 0.5);
    output.normal = float4(normal * 0.5 + 0.5, 1.0);
    float4 material = Material.Sample(Anisotropic, input.uv);
    output.material = float4(material.r, material.g, material.b, 1.0);
    return output;
}
)hlsl" },

		{ 5, "cs_5_0", R"hlsl(
Texture2D<float> Depth : register(t0);
Texture2D<float4> GBufferA : register(t1);
Texture2D<float4> GBufferB : register(t2);
Texture2D<float4> GBufferC : register(t3);
RWTexture2D<float4> SceneColor : register(u0);

cbuffer Light : register(b0)
{
    float3 SunDirection;
    float SunIntensity;
    float3 SunColor;
    float Ambient;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    float depth = Depth[id.xy];
    if (depth >= 1.0)
    {
        SceneColor[id.xy] = float4(0.02, 0.03, 0.05, 1.0);
        return;
    }
    float3 albedo = GBufferA[id.xy].rgb;
    float3 normal = GBufferB[id.xy].xyz * 2.0 - 1.0;
    float4 material = GBufferC[id.xy];
    float lambert = saturate(dot(normal, -SunDirection));
    float3 diffuse = albedo * SunColor * SunIntensity * lambert;
    float3 ambient = albedo * Ambient * material.b;
    SceneColor[id.xy] = float4(diffuse + ambient, 1.0);
}
)hlsl" },

		{ 6, "cs_5_0", R"hlsl(
Texture2D<float4> Source : register(t0);
SamplerState LinearClamp : register(s0);
RWTexture2D<float4> Destination : register(u0);

cbuffer Bloom : register(b0)
{
    float2 InverseSourceSize;
    float Threshold;
    float Knee;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    float2 uv = (float2(id.xy) * 2.0 + 1.0) * InverseSourceSize;
    float3 color = Source.SampleLevel(LinearClamp, uv, 0).rgb * 0.5;
    color += Source.SampleLevel(LinearClamp, uv + float2(-1.0, -1.0) * InverseSourceSize, 0).rgb * 0.125;
    color += Source.SampleLevel(LinearClamp, uv + float2( 1.0, -1.0) * InverseSourceSize, 0).rgb * 0.125;
    color += Source.SampleLevel(LinearClamp, uv + float2(-1.0,  1.0) * InverseSourceSize, 0).rgb * 0.125;
    color += Source.SampleLevel(LinearClamp, uv + float2( 1.0,  1.0) * InverseSourceSize, 0).rgb * 0.125;

    float brightness = max(color.r, max(color.g, color.b));
    float soft = clamp(brightness - Threshold + Knee, 0.0, 2.0 * Knee);
    soft = soft * soft / (4.0 * Knee + 0.0001);
    float contribution = max(soft, brightness - Threshold) / max(brightness, 0.0001);
    Destination[id.xy] = float4(color * contribution, 1.0);
}
)hlsl" },

		{ 7, "cs_5_0", R"hlsl(
Texture2D<float4> Source : register(t0);
SamplerState LinearClamp : register(s0);
RWTexture2D<float4> Destination : register(u0);

cbuffer Blur : register(b0)
{
    float2 Direction;
    float2 InverseSize;
};

static const float Weights[5] = { 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 };

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    float2 uv = (float2(id.xy) + 0.5) * InverseSize;
    float3 sum = Source.SampleLevel(LinearClamp, uv, 0).rgb * Weights[0];
    [unroll] for (int i = 1; i < 5; ++i)
    {
        float2 offset = Direction * InverseSize * i;
        sum += Source.SampleLevel(LinearClamp, uv + offset, 0).rgb * Weights[i];
        sum += Source.SampleLevel(LinearClamp, uv - offset, 0).rgb * Weights[i];
    }
    Destination[id.xy] = float4(sum, 1.0);
}
)hlsl" },

		{ 8, "ps_5_0", R"hlsl(
Texture2D SceneColor : register(t0);
Texture2D Bloom : register(t1);
SamplerState LinearClamp : register(s0);

cbuffer Tonemap : register(b0)
{
    float Exposure;
    float BloomIntensity;
    float2 Padding;
};

float3 ACESFilm(float3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    float3 color = SceneColor.Sample(LinearClamp, uv).rgb;
    color += Bloom.Sample(LinearClamp, uv).rgb * BloomIntensity;
    color = ACESFilm(color * Exposure);
    return float4(pow(color, 1.0 / 2.2), 1.0);
}
)hlsl" },

		{ 9, "ps_5_0", R"hlsl(
Texture2D Atlas : register(t0);
SamplerState PointClamp : register(s0);

float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0, float4 color : COLOR0) : SV_Target
{
    return Atlas.Sample(PointClamp, uv) * color;
}
)hlsl" },
	};
}
