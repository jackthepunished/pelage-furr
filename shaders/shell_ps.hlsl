struct FrameCB {
    float4x4 ViewProj;
    float4x4 World;
    float3 CameraPos;
    float Time;
    float3 Gravity;
    float WindStrength;
    float3 WindDirection;
    float Padding;
};
ConstantBuffer<FrameCB> g_Frame : register(b0);

struct FurCB {
    float FurLength;
    uint ShellCount;
    float Density;
    float Thickness;
    float3 FurColor;
    float Padding;
};
ConstantBuffer<FurCB> g_Fur : register(b1);

Texture2D<float> g_NoiseTex : register(t0);
SamplerState g_SamLinear : register(s0);

struct VS_OUT {
    float4 PosCS : SV_POSITION;
    float3 PosWS : POSITION;
    float3 NormalWS : NORMAL;
    float2 UV : TEXCOORD;
    float NormalizedHeight : HEIGHT;
};

float4 main(VS_OUT input) : SV_TARGET {
    // TRADEOFF: Sampling a pre-computed Voronoi texture is significantly faster 
    // on mid-range GPUs than computing cellular noise procedurally in the PS.
    float noiseValue = g_NoiseTex.Sample(g_SamLinear, input.UV * g_Fur.Density).r;
    
    // Shape the strand: thicker at bottom, tapers at the top.
    // We subtract the shell height from the noise, scaled by thickness.
    float strandShape = noiseValue - (input.NormalizedHeight * g_Fur.Thickness);
    
    // Alpha discard
    clip(strandShape);
    
    // Basic lighting (Phong ambient + diffuse)
    float3 lightDir = normalize(float3(1, 1, -1));
    float ndotl = saturate(dot(input.NormalWS, lightDir));
    float3 lighting = g_Fur.FurColor * (ndotl * 0.8f + 0.2f); // 20% ambient
    
    // Darken roots for pseudo-AO
    lighting *= lerp(0.3f, 1.0f, input.NormalizedHeight);
    
    return float4(lighting, 1.0f);
}