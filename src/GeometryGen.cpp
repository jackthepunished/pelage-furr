#include "GeometryGen.h"
#include <cmath>
#include <algorithm>
#include <iostream>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "../third_party/tinygltf/tiny_gltf.h"

using namespace DirectX;

#ifndef XM_PI
#define XM_PI 3.141592654f
#endif

MeshData GeometryGen::LoadGLTF(const std::string& path) {
    MeshData mesh;
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, path);
    if (!warn.empty()) std::cout << "GLTF Warn: " << warn << std::endl;
    if (!err.empty()) std::cout << "GLTF Err: " << err << std::endl;
    if (!ret) return mesh;

    if (model.meshes.empty()) return mesh;

    for (const auto& gltfMesh : model.meshes) {
        if (gltfMesh.primitives.empty()) continue;
        
        for (const auto& primitive : gltfMesh.primitives) {
            uint32_t vertexOffset = (uint32_t)mesh.Vertices.size();

            // Get Accessors
            const tinygltf::Accessor* posAccessor = nullptr;
            const tinygltf::Accessor* normAccessor = nullptr;
            const tinygltf::Accessor* uvAccessor = nullptr;
            
            if (primitive.attributes.count("POSITION")) {
                posAccessor = &model.accessors[primitive.attributes.at("POSITION")];
            } else {
                continue; // Skip if no position
            }
            
            if (primitive.attributes.count("NORMAL")) {
                normAccessor = &model.accessors[primitive.attributes.at("NORMAL")];
            }
            if (primitive.attributes.count("TEXCOORD_0")) {
                uvAccessor = &model.accessors[primitive.attributes.at("TEXCOORD_0")];
            }
            
            if (primitive.indices < 0) continue; // Skip non-indexed geometry for simplicity
            const tinygltf::Accessor& indAccessor = model.accessors[primitive.indices];

            // Get Buffer Views
            const tinygltf::BufferView& posView = model.bufferViews[posAccessor->bufferView];
            const tinygltf::BufferView* normView = normAccessor ? &model.bufferViews[normAccessor->bufferView] : nullptr;
            const tinygltf::BufferView* uvView = uvAccessor ? &model.bufferViews[uvAccessor->bufferView] : nullptr;
            const tinygltf::BufferView& indView = model.bufferViews[indAccessor.bufferView];

            // Get Buffers
            const tinygltf::Buffer& posBuffer = model.buffers[posView.buffer];
            const tinygltf::Buffer* normBuffer = normView ? &model.buffers[normView->buffer] : nullptr;
            const tinygltf::Buffer* uvBuffer = uvView ? &model.buffers[uvView->buffer] : nullptr;
            const tinygltf::Buffer& indBuffer = model.buffers[indView.buffer];

            // Extract Vertices
            const float* positions = reinterpret_cast<const float*>(&posBuffer.data[posView.byteOffset + posAccessor->byteOffset]);
            const float* normals = normBuffer ? reinterpret_cast<const float*>(&normBuffer->data[normView->byteOffset + normAccessor->byteOffset]) : nullptr;
            const float* uvs = uvBuffer ? reinterpret_cast<const float*>(&uvBuffer->data[uvView->byteOffset + uvAccessor->byteOffset]) : nullptr;

            for (size_t i = 0; i < posAccessor->count; ++i) {
                Vertex v;
                v.Pos = XMFLOAT3(positions[i * 3 + 0], positions[i * 3 + 1], positions[i * 3 + 2]);
                
                if (normals) {
                    v.Normal = XMFLOAT3(normals[i * 3 + 0], normals[i * 3 + 1], normals[i * 3 + 2]);
                } else {
                    v.Normal = XMFLOAT3(0, 1, 0); // Default normal
                }
                
                if (uvs) {
                    v.UV = XMFLOAT2(uvs[i * 2 + 0], uvs[i * 2 + 1]);
                } else {
                    v.UV = XMFLOAT2(0, 0);
                }
                
                // Scale it down significantly since this particular fur_carpet Sketchfab model is absolutely massive
                v.Pos.x *= 0.005f;
                v.Pos.y *= 0.005f;
                v.Pos.z *= 0.005f;
                
                // Fix coordinate system (glTF is right-handed Y-up, DirectX is left-handed Y-up)
                v.Pos.z *= -1.0f;
                v.Normal.z *= -1.0f;

                mesh.Vertices.push_back(v);
            }

            // Extract Indices
            if (indAccessor.componentType == TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT) {
                const uint16_t* indices = reinterpret_cast<const uint16_t*>(&indBuffer.data[indView.byteOffset + indAccessor.byteOffset]);
                for (size_t i = 0; i < indAccessor.count; i += 3) {
                    mesh.Indices.push_back(vertexOffset + indices[i]);
                    mesh.Indices.push_back(vertexOffset + indices[i + 2]);
                    mesh.Indices.push_back(vertexOffset + indices[i + 1]);
                }
            } else if (indAccessor.componentType == TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT) {
                const uint32_t* indices = reinterpret_cast<const uint32_t*>(&indBuffer.data[indView.byteOffset + indAccessor.byteOffset]);
                for (size_t i = 0; i < indAccessor.count; i += 3) {
                    mesh.Indices.push_back(vertexOffset + indices[i]);
                    mesh.Indices.push_back(vertexOffset + indices[i + 2]);
                    mesh.Indices.push_back(vertexOffset + indices[i + 1]);
                }
            } else if (indAccessor.componentType == TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE) {
                const uint8_t* indices = reinterpret_cast<const uint8_t*>(&indBuffer.data[indView.byteOffset + indAccessor.byteOffset]);
                for (size_t i = 0; i < indAccessor.count; i += 3) {
                    mesh.Indices.push_back(vertexOffset + indices[i]);
                    mesh.Indices.push_back(vertexOffset + indices[i + 2]);
                    mesh.Indices.push_back(vertexOffset + indices[i + 1]);
                }
            }
        }
    }
    
    std::cout << "Loaded " << mesh.Vertices.size() << " vertices and " << mesh.Indices.size() / 3 << " triangles." << std::endl;
    
    // Check if the mesh is massive and might cause memory/timeout issues
    if (mesh.Vertices.size() > 500000) {
        std::cout << "Warning: Mesh is extremely large. Downsampling for prototype performance..." << std::endl;
        
        // Simple fast decimation (take only every Nth triangle)
        std::vector<uint32_t> downsampledIndices;
        int step = 200; // Take 1 in 200 triangles (drastic cut)
        for (size_t i = 0; i < mesh.Indices.size(); i += 3 * step) {
            if (i + 2 < mesh.Indices.size()) {
                downsampledIndices.push_back(mesh.Indices[i]);
                downsampledIndices.push_back(mesh.Indices[i+1]);
                downsampledIndices.push_back(mesh.Indices[i+2]);
            }
        }
        mesh.Indices = downsampledIndices;
        std::cout << "Downsampled to " << mesh.Indices.size() / 3 << " triangles." << std::endl;
    }

        // Scale it down significantly since this particular fur_carpet Sketchfab model is absolutely massive
        for(auto& v : mesh.Vertices) {
            v.Pos.x *= 0.05f;
            v.Pos.y *= 0.05f;
            v.Pos.z *= 0.05f;
        }

    std::cout << "Generating Adjacency..." << std::endl;
    GenerateAdjacency(mesh);
    std::cout << "Adjacency Generated." << std::endl;
    return mesh;
}

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
            DirectX::XMVECTOR n = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&v.Normal));
            DirectX::XMStoreFloat3(&v.Normal, n);

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
