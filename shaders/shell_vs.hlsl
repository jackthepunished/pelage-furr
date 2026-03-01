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

struct FurCB {
    float FurLength;
    uint ShellCount;
    float Density;
    float Thickness;
    float3 FurColor;
    float Padding;
};
ConstantBuffer<FurCB> g_Fur : register(b1);

struct VS_IN {
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD;
    uint InstanceID : SV_InstanceID;
};

struct VS_OUT {
    float4 PosCS : SV_POSITION;
    float3 PosWS : POSITION;
    float3 NormalWS : NORMAL;
    float2 UV : TEXCOORD;
    float NormalizedHeight : HEIGHT;
};

VS_OUT main(VS_IN input) {
    VS_OUT output;
    
    // Normalized height 'h' goes from 0.0 (skin) to 1.0 (tips)
    float h = (float)input.InstanceID / (float)(g_Fur.ShellCount - 1);
    
    // Create strand frizz/jitter using the UV and instance ID
    // Magic numbers are just arbitrary non-collinear primes for hashing
    float noise1 = frac(sin(dot(input.UV, float2(12.9898, 78.233))) * 43758.5453);
    float noise2 = frac(sin(dot(input.UV, float2(39.346, 11.135))) * 43758.5453);
    float3 jitter = float3(noise1 * 2.0 - 1.0, 0.0, noise2 * 2.0 - 1.0);
    
    // Transform base position and normal to World Space FIRST
    float3 basePosWS = mul(float4(input.Pos, 1.0f), g_Frame.World).xyz;
    float3 normalWS = normalize(mul(input.Normal, (float3x3)g_Frame.World));
    
    // Apply jitter to the normal vector, scaling the jitter intensity by height 
    // so roots stay attached but tips frizz out. 
    // Scale down the overall frizz amount to keep it subtle
    float3 frizzNormalWS = normalize(normalWS + jitter * h * 0.4f);
    
    // Stage 1: Extrude vertex along frizz normal in World Space
    float3 extrusion = frizzNormalWS * h * g_Fur.FurLength;
    
    // Stage 2: Gravity droop (quadratic stiffness: t^2 weighting)
    float stiffness = h * h; 
    float3 droop = g_Frame.Gravity * stiffness;
    
    // Stage 3: Animated wind (multi-frequency sine, per-vertex phase offset)
    float phaseOffset = dot(basePosWS, float3(12.9898f, 78.233f, 37.719f)); 
    float time = g_Frame.Time;
    float windWave1 = sin(time * 2.0f + phaseOffset);
    float windWave2 = sin(time * 3.7f + phaseOffset * 1.5f) * 0.5f; 
    float windIntensity = (windWave1 + windWave2) * g_Frame.WindStrength;
    
    float3 windOffset = g_Frame.WindDirection * windIntensity;
    
    // Apply gravity and wind
    float3 combinedDisplacement = extrusion + droop + (windOffset * stiffness);
    
    // Length preservation
    float currentLen = length(combinedDisplacement);
    float3 strandDir = combinedDisplacement / currentLen;
    float3 finalPosWS = basePosWS + strandDir * (h * g_Fur.FurLength);
    
    output.PosWS = finalPosWS;
    output.PosCS = mul(float4(finalPosWS, 1.0f), g_Frame.ViewProj);
    output.NormalWS = normalWS;
    output.UV = input.UV;
    output.NormalizedHeight = h;
    
    return output;
}