#include "GeometryGen.h"
#include <cmath>
#include <algorithm>

using namespace DirectX;

#ifndef XM_PI
#define XM_PI 3.141592654f
#endif

MeshData GeometryGen::CreateSphere(float radius, uint32_t sliceCount, uint32_t stackCount) {
    MeshData mesh;

    // Poles
    Vertex topVertex = { XMFLOAT3(0.0f, radius, 0.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) };
    Vertex bottomVertex = { XMFLOAT3(0.0f, -radius, 0.0f), XMFLOAT3(0.0f, -1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) };

    mesh.Vertices.push_back(topVertex);

    float phiStep = XM_PI / stackCount;
    float thetaStep = 2.0f * XM_PI / sliceCount;

    for (uint32_t i = 1; i <= stackCount - 1; ++i) {
        float phi = i * phiStep;
        for (uint32_t j = 0; j <= sliceCount; ++j) {
            float theta = j * thetaStep;

            Vertex v;
            v.Pos.x = radius * sinf(phi) * cosf(theta);
            v.Pos.y = radius * cosf(phi);
            v.Pos.z = radius * sinf(phi) * sinf(theta);

            v.Normal = v.Pos;
            XMVECTOR n = XMVector3Normalize(XMLoadFloat3(&v.Normal));
            XMStoreFloat3(&v.Normal, n);

            v.UV.x = theta / (2.0f * XM_PI);
            v.UV.y = phi / XM_PI;

            mesh.Vertices.push_back(v);
        }
    }
    mesh.Vertices.push_back(bottomVertex);

    // Indices
    for (uint32_t i = 1; i <= sliceCount; ++i) {
        mesh.Indices.push_back(0);
        mesh.Indices.push_back(i + 1);
        mesh.Indices.push_back(i);
    }

    uint32_t baseIndex = 1;
    uint32_t ringVertexCount = sliceCount + 1;
    for (uint32_t i = 0; i < stackCount - 2; ++i) {
        for (uint32_t j = 0; j < sliceCount; ++j) {
            mesh.Indices.push_back(baseIndex + i * ringVertexCount + j);
            mesh.Indices.push_back(baseIndex + i * ringVertexCount + j + 1);
            mesh.Indices.push_back(baseIndex + (i + 1) * ringVertexCount + j);

            mesh.Indices.push_back(baseIndex + (i + 1) * ringVertexCount + j);
            mesh.Indices.push_back(baseIndex + i * ringVertexCount + j + 1);
            mesh.Indices.push_back(baseIndex + (i + 1) * ringVertexCount + j + 1);
        }
    }

    uint32_t southPoleIndex = (uint32_t)mesh.Vertices.size() - 1;
    baseIndex = southPoleIndex - ringVertexCount;
    for (uint32_t i = 0; i < sliceCount; ++i) {
        mesh.Indices.push_back(southPoleIndex);
        mesh.Indices.push_back(baseIndex + i);
        mesh.Indices.push_back(baseIndex + i + 1);
    }

    GenerateAdjacency(mesh);
    return mesh;
}

struct Edge {
    uint32_t v1, v2;
    bool operator<(const Edge& other) const {
        if (v1 != other.v1) return v1 < other.v1;
        return v2 < other.v2;
    }
};

void GeometryGen::GenerateAdjacency(MeshData& mesh) {
    // Edge to triangles
    std::map<Edge, std::vector<uint32_t>> edgeToTri;
    
    uint32_t numTris = (uint32_t)mesh.Indices.size() / 3;
    for (uint32_t i = 0; i < numTris; i++) {
        uint32_t i0 = mesh.Indices[i*3 + 0];
        uint32_t i1 = mesh.Indices[i*3 + 1];
        uint32_t i2 = mesh.Indices[i*3 + 2];
        
        Edge e0 = { std::min(i0,i1), std::max(i0,i1) };
        Edge e1 = { std::min(i1,i2), std::max(i1,i2) };
        Edge e2 = { std::min(i2,i0), std::max(i2,i0) };
        
        edgeToTri[e0].push_back(i);
        edgeToTri[e1].push_back(i);
        edgeToTri[e2].push_back(i);
    }

    mesh.IndicesAdj.resize(numTris * 6);
    
    for (uint32_t i = 0; i < numTris; i++) {
        uint32_t i0 = mesh.Indices[i*3 + 0];
        uint32_t i1 = mesh.Indices[i*3 + 1];
        uint32_t i2 = mesh.Indices[i*3 + 2];
        
        uint32_t adj[3] = { i0, i1, i2 }; // defaults for borders (no neighbor)
        
        Edge edges[3] = {
            { std::min(i0,i1), std::max(i0,i1) },
            { std::min(i1,i2), std::max(i1,i2) },
            { std::min(i2,i0), std::max(i2,i0) }
        };
        
        // Find opposite vertex in neighbor triangles for each edge
        for (int e = 0; e < 3; e++) {
            const auto& tris = edgeToTri[edges[e]];
            for (uint32_t t : tris) {
                if (t != i) {
                    // This is a neighbor triangle! Find its opposite vertex.
                    uint32_t n0 = mesh.Indices[t*3+0];
                    uint32_t n1 = mesh.Indices[t*3+1];
                    uint32_t n2 = mesh.Indices[t*3+2];
                    if (n0 != edges[e].v1 && n0 != edges[e].v2) adj[e] = n0;
                    if (n1 != edges[e].v1 && n1 != edges[e].v2) adj[e] = n1;
                    if (n2 != edges[e].v1 && n2 != edges[e].v2) adj[e] = n2;
                    break; // Only expect one neighbor for a manifold
                }
            }
        }
        
        // Output format: v0, adj0, v1, adj1, v2, adj2
        // Where adj0 is opposite of edge v0-v1
        mesh.IndicesAdj[i*6 + 0] = i0;
        mesh.IndicesAdj[i*6 + 1] = adj[0];
        mesh.IndicesAdj[i*6 + 2] = i1;
        mesh.IndicesAdj[i*6 + 3] = adj[1];
        mesh.IndicesAdj[i*6 + 4] = i2;
        mesh.IndicesAdj[i*6 + 5] = adj[2];
    }
}
