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

struct PS_OUT {
    float4 Layer0 : SV_Target0;
    float4 Layer1 : SV_Target1;
    float4 Layer2 : SV_Target2;
    float4 Layer3 : SV_Target3;
};

PS_OUT main(VS_OUT input) {
    float noiseValue = g_NoiseTex.Sample(g_SamLinear, input.UV * g_Fur.Density).r;
    float strandShape = noiseValue - (input.NormalizedHeight * g_Fur.Thickness);
    clip(strandShape);
    
    // We are inside the strand. Output opacity.
    float opacity = (1.0f - input.NormalizedHeight) * (1.0f / (float)g_Fur.ShellCount); // Prevent white-out
    
    // Determine depth slice in light space (0.0 to 1.0)
    // input.PosCS.z is already normalized depth in D3D12
    float sliceDepth = input.PosCS.z * 4.0f; 
    
    PS_OUT output = (PS_OUT)0;
    
    // Assign opacity to the correct MRT using ternary selects
    output.Layer0.r = (sliceDepth < 1.0f) ? opacity : 0.0f;
    output.Layer1.r = (sliceDepth >= 1.0f && sliceDepth < 2.0f) ? opacity : 0.0f;
    output.Layer2.r = (sliceDepth >= 2.0f && sliceDepth < 3.0f) ? opacity : 0.0f;
    output.Layer3.r = (sliceDepth >= 3.0f) ? opacity : 0.0f;
    
    return output;
}