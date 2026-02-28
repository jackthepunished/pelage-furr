struct FrameCB {
    float4x4 ViewProj;
    float4x4 World;
    float4x4 LightViewProj;
    float3 CameraPos;
    float Time;
    float3 Gravity;
    float WindStrength;
    float3 WindDirection;
    float Padding;
};
ConstantBuffer<FrameCB> g_Frame : register(b0);

struct VS_IN {
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD;
};

struct VS_OUT {
    float4 PosCS : SV_POSITION;
    float3 PosWS : POSITION;
    float3 NormalWS : NORMAL;
    float2 UV : TEXCOORD;
    float NormalizedHeight : HEIGHT;
};

// Fin VS just passes data to GS. We don't extrude here.
VS_OUT main(VS_IN input) {
    VS_OUT output;
    output.PosWS = mul(float4(input.Pos, 1.0f), g_Frame.World).xyz;
    output.PosCS = mul(float4(output.PosWS, 1.0f), g_Frame.ViewProj);
    output.NormalWS = normalize(mul(input.Normal, (float3x3)g_Frame.World));
    output.UV = input.UV;
    output.NormalizedHeight = 0.0f; // Base height
    return output;
}