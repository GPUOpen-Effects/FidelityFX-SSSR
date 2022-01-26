/**********************************************************************
Copyright (c) 2020 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
********************************************************************/

[[vk::binding(0)]] Texture2D<float4> reflectionTarget              : register(t0);
[[vk::binding(1)]] Texture2D<float4> normalsTexture                : register(t1);
[[vk::binding(2)]] Texture2D<float4> specularRoughnessTexture      : register(t2);
[[vk::binding(3)]] Texture2D<float4> brdfTexture                   : register(t3);

[[vk::binding(4)]] SamplerState linearSampler : register(s0);

[[vk::binding(5)]] cbuffer Constants : register(b0){
    float4 viewDirection;
    uint showReflectionTarget;
    uint drawReflections;
};

struct VertexInput
{
    uint vertexId : SV_VertexID;
};

struct VertexOut
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

VertexOut vs_main(VertexInput input){
    VertexOut output;
    output.texcoord = float2((input.vertexId << 1) & 2, input.vertexId & 2);
    output.position = float4(output.texcoord.xy * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

// Important bits from the PBR shader
float3 getIBLContribution(float perceptualRoughness, float3 specularColor, float3 specularLight, float3 n, float3 v){
    float NdotV = clamp(dot(n, v), 0.0, 1.0);
    float2 brdfSamplePoint = clamp(float2(NdotV, perceptualRoughness), float2(0.0, 0.0), float2(1.0, 1.0));
    // retrieve a scale and bias to F0. See [1], Figure 3
    float2 brdf = brdfTexture.Sample(linearSampler, brdfSamplePoint).rg;

    float3 specular = specularLight * (specularColor * brdf.x + brdf.y); 
    return specular;
}

float4 ps_main(VertexOut input) : SV_Target0
{
    input.texcoord.y = 1 - input.texcoord.y;
    float3 radiance = reflectionTarget.Sample(linearSampler, input.texcoord).xyz;
    float4 specularRoughness = specularRoughnessTexture.Sample(linearSampler, input.texcoord);
    float3 specularColor = specularRoughness.xyz;
    float perceptualRoughness = sqrt(specularRoughness.w); // specularRoughness.w contains alphaRoughness
    float3 normal = 2 * normalsTexture.Sample(linearSampler, input.texcoord).xyz - 1;
    float3 view = viewDirection.xyz;

    if (showReflectionTarget == 1)
    {
        // Show just the reflection view
        return float4(radiance, 0);
    }
    else if (drawReflections == 1)
    {
        radiance = getIBLContribution(perceptualRoughness, specularColor, radiance, normal, view);
        return float4(radiance, 1); // Show the reflections applied to the scene
    }
    else
    {
        // Show just the scene
        return float4(0, 0, 0, 1);
    }
}