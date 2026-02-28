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

struct GS_IN {
    float4 PosCS : SV_POSITION;
    float3 PosWS : POSITION;
    float3 NormalWS : NORMAL;
    float2 UV : TEXCOORD;
    float NormalizedHeight : HEIGHT;
};

struct GS_OUT {
    float4 PosCS : SV_POSITION;
    float3 PosWS : POSITION;
    float3 NormalWS : NORMAL;
    float2 UV : TEXCOORD;
    float NormalizedHeight : HEIGHT;
};

// Exactly matches Shell VS extrusion
float4 ExtrudeTip(GS_IN v, float3 normalWS, float h) {
    float3 posWS = v.PosWS;
    
    float3 extrusion = normalWS * h * g_Fur.FurLength;
    
    float stiffness = h * h; 
    float3 droop = g_Frame.Gravity * stiffness;
    
    float phaseOffset = dot(posWS, float3(12.9898f, 78.233f, 37.719f)); 
    float time = g_Frame.Time;
    float windWave1 = sin(time * 2.0f + phaseOffset);
    float windWave2 = sin(time * 3.7f + phaseOffset * 1.5f) * 0.5f; 
    float windIntensity = (windWave1 + windWave2) * g_Frame.WindStrength;
    
    float3 windOffset = g_Frame.WindDirection * windIntensity;
    
    float3 combinedDisplacement = extrusion + droop + (windOffset * stiffness);
    
    float currentLen = length(combinedDisplacement);
    float3 strandDir = combinedDisplacement / currentLen;
    float3 finalPosWS = posWS + strandDir * (h * g_Fur.FurLength);
    
    return mul(float4(finalPosWS, 1.0f), g_Frame.ViewProj);
}

[maxvertexcount(12)] // Max 3 quads (12 vertices) per triangle
void main(
    triangleadj GS_IN input[6], 
    inout TriangleStream<GS_OUT> stream
) {
    // Vertices 0, 2, 4 are the main triangle.
    // Vertices 1, 3, 5 are adjacent vertices.
    
    // Calculate face normal of the main triangle
    float3 edge1 = input[2].PosWS - input[0].PosWS;
    float3 edge2 = input[4].PosWS - input[0].PosWS;
    float3 mainNormal = normalize(cross(edge1, edge2));
    
    float3 viewDir = normalize(g_Frame.CameraPos - input[0].PosWS);
    
    // Edges
    uint edges[3][4] = {
        {0, 1, 2, 4}, // Edge 0-2, Adj 1, Opposite 4
        {2, 3, 4, 0}, // Edge 2-4, Adj 3, Opposite 0
        {4, 5, 0, 2}  // Edge 4-0, Adj 5, Opposite 2
    };
    
    [unroll]
    for(int i = 0; i < 3; i++) {
        uint vStart = edges[i][0];
        uint vAdj   = edges[i][1];
        uint vEnd   = edges[i][2];
        
        float3 adjEdge1 = input[vAdj].PosWS - input[vStart].PosWS;
        float3 adjEdge2 = input[vEnd].PosWS - input[vStart].PosWS;
        float3 adjNormal = normalize(cross(adjEdge2, adjEdge1)); 
        
        // Silhouette condition: One faces viewer, adjacent faces away
        // Also draw fin if it's a boundary edge (vAdj and vEnd could be degenerate if we don't have good adj data, but we generated it properly)
        bool isSilhouette = (dot(mainNormal, viewDir) > 0.0f) && (dot(adjNormal, viewDir) <= 0.0f);
        
        if (isSilhouette) {
            GS_OUT v[4];
            
            // Base vertices (h = 0)
            v[0].PosCS = input[vStart].PosCS;
            v[0].PosWS = input[vStart].PosWS;
            v[0].NormalWS = input[vStart].NormalWS;
            v[0].UV = input[vStart].UV;
            v[0].NormalizedHeight = -0.001f; // Sink slightly to prevent Z-fighting with shells at roots
            
            v[1].PosCS = input[vEnd].PosCS;
            v[1].PosWS = input[vEnd].PosWS;
            v[1].NormalWS = input[vEnd].NormalWS;
            v[1].UV = input[vEnd].UV;
            v[1].NormalizedHeight = -0.001f;
            
            // Tip vertices (h = 1)
            v[2].PosCS = ExtrudeTip(input[vStart], input[vStart].NormalWS, 1.0f); 
            v[2].PosWS = input[vStart].PosWS; // Not used for lighting
            v[2].NormalWS = input[vStart].NormalWS;
            v[2].UV = input[vStart].UV;
            v[2].NormalizedHeight = 1.0f;
            
            v[3].PosCS = ExtrudeTip(input[vEnd], input[vEnd].NormalWS, 1.0f);
            v[3].PosWS = input[vEnd].PosWS; 
            v[3].NormalWS = input[vEnd].NormalWS;
            v[3].UV = input[vEnd].UV;
            v[3].NormalizedHeight = 1.0f;
            
            stream.Append(v[0]);
            stream.Append(v[2]); // Triangle strip order (0, 2, 1) then (2, 3, 1) -> wait, standard is 0,1,2,3 for strip. Let's do 0, 1, 2, 3 where 0,1 are base, 2,3 are tip. 
            // Wait: triangle strip is v0, v1, v2 -> v0(baseStart), v1(tipStart), v2(baseEnd).
            // Let's order it: BaseStart, TipStart, BaseEnd, TipEnd.
            
            GS_OUT strip[4];
            strip[0] = v[0]; // Base Start
            strip[1] = v[2]; // Tip Start
            strip[2] = v[1]; // Base End
            strip[3] = v[3]; // Tip End

            stream.Append(strip[0]);
            stream.Append(strip[1]);
            stream.Append(strip[2]);
            stream.Append(strip[3]);
            stream.RestartStrip();
        }
    }
}