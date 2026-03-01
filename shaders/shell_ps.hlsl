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

Texture2D<float> g_NoiseTex : register(t0);
Texture2D<float> g_OsmTex[4] : register(t1);
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
    
    // Kajiya-Kay Shading (Physically-based hair approximation)
    // For shell fur extruded along the normal, the strand tangent 'T' is the normal itself.
    float3 T = normalize(input.NormalWS);
    float3 L = normalize(float3(1.0f, 1.0f, -1.0f)); // Light direction
    float3 V = normalize(g_Frame.CameraPos - input.PosWS); // View direction
    
    // Hair diffuse is not Lambertian (dot(N,L)). It's based on scattering through the cylindrical strand.
    // Approximated by the sine of the angle between light and strand.
    float dotTL = dot(T, L);
    float sinTL = sqrt(max(0.0f, 1.0f - dotTL * dotTL));
    float diffuse = sinTL;
    
    // Kajiya-Kay Specular Highlight
    float dotTV = dot(T, V);
    float sinTV = sqrt(max(0.0f, 1.0f - dotTV * dotTV));
    // The specular alignment term: T.L * T.V + sin(theta_L) * sin(theta_V)
    float specAlignment = max(0.0f, (dotTL * dotTV) + (sinTL * sinTV));
    float specular = pow(specAlignment, 32.0f); // Primary shininess
    
    // Secondary shifted highlight (internal reflection)
    float shift = -0.1f;
    float3 shiftedT = normalize(T + V * shift);
    float dotShiftedTL = dot(shiftedT, L);
    float dotShiftedTV = dot(shiftedT, V);
    float sinShiftedTL = sqrt(max(0.0f, 1.0f - dotShiftedTL * dotShiftedTL));
    float sinShiftedTV = sqrt(max(0.0f, 1.0f - dotShiftedTV * dotShiftedTV));
    float specAlignment2 = max(0.0f, (dotShiftedTL * dotShiftedTV) + (sinShiftedTL * sinShiftedTV));
    float secondarySpecular = pow(specAlignment2, 12.0f) * 0.4f;

    // Combine terms
    float3 ambient = g_Fur.FurColor * 0.15f;
    float3 diffuseLight = g_Fur.FurColor * diffuse * 0.85f;
    float3 specularLight = float3(1.0f, 0.9f, 0.8f) * (specular + secondarySpecular) * 0.6f; // Slightly warm
    
    // OSM Shadowing
    float4 posLightCS = mul(float4(input.PosWS, 1.0f), g_Frame.LightViewProj);
    posLightCS.xyz /= posLightCS.w;
    
    // Convert NDC to UV
    float2 shadowUV = posLightCS.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
    
    float shadowFactor = 1.0f;
    if (shadowUV.x >= 0.0f && shadowUV.x <= 1.0f && shadowUV.y >= 0.0f && shadowUV.y <= 1.0f) {
        // Sample all 4 layers
        float accumulatedOpacity = 0.0f;
        accumulatedOpacity += g_OsmTex[0].Sample(g_SamLinear, shadowUV).r;
        accumulatedOpacity += g_OsmTex[1].Sample(g_SamLinear, shadowUV).r;
        accumulatedOpacity += g_OsmTex[2].Sample(g_SamLinear, shadowUV).r;
        accumulatedOpacity += g_OsmTex[3].Sample(g_SamLinear, shadowUV).r;
        
        // Deep shadow using Beer's Law approximation
        shadowFactor = exp(-accumulatedOpacity * 5.0f);
    }
    
    float3 lighting = ambient + (diffuseLight + specularLight) * shadowFactor;
    
    // Darken roots for pseudo-AO using an exponential curve for subsurface feel
    float ao = pow(input.NormalizedHeight, 0.4f); // steep curve for dark roots, bright tips
    lighting *= lerp(0.1f, 1.0f, ao);
    
    // Fresnel Rim Lighting
    float f0 = 0.04f;
    float cosTheta = saturate(dot(input.NormalWS, V));
    float rim = f0 + (1.0f - f0) * pow(1.0f - cosTheta, 5.0f);
    lighting += g_Fur.FurColor * rim * 0.5f;
    
    return float4(lighting, 1.0f);
}