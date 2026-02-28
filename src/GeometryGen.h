#pragma once
#include <vector>
#include <map>
#include <string>
#include <DirectXMath.h>

using namespace DirectX;

struct Vertex {
    XMFLOAT3 Pos;
    XMFLOAT3 Normal;
    XMFLOAT2 UV;
};

struct MeshData {
    std::vector<Vertex> Vertices;
    std::vector<uint32_t> Indices;
    std::vector<uint32_t> IndicesAdj; // With adjacency
};

class GeometryGen {
public:
    static MeshData CreateSphere(float radius, uint32_t sliceCount, uint32_t stackCount);
    static MeshData LoadGLTF(const std::string& path);
    static void GenerateAdjacency(MeshData& mesh);
};
