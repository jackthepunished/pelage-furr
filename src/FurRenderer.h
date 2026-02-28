#ifndef FUR_RENDERER_H
#define FUR_RENDERER_H

#include <windows.h>
#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <string>
#include <vector>

#include "d3dx12.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

class FurRenderer {
public:
    FurRenderer(HWND hwnd, uint32_t width, uint32_t height);
    ~FurRenderer();

    void Init();
    void Update(float deltaTime);
    void Render();
    void Resize(uint32_t width, uint32_t height);

private:
    void InitD3D12();
    void CreateCommandObjects();
    void CreateSwapChain();
    void CreateRtvAndDsvDescriptorHeaps();
    void CreateRootSignature();
    void CreateConstantBuffers();
    void CreateShadersAndPSOs();
    void BuildRenderItems();
    void FlushCommandQueue();
    
    // Core parameters from original setup
    struct FrameCB {
        XMMATRIX ViewProj;
        XMMATRIX World;
        XMFLOAT3 CameraPos;
        float Time;
        XMFLOAT3 Gravity;
        float WindStrength;
        XMFLOAT3 WindDirection;
        float Padding;
    };

    struct FurCB {
        float FurLength;
        uint32_t ShellCount;
        float Density;
        float Thickness;
        XMFLOAT3 FurColor;
        float Padding;
    };

    // D3D12 Context
    HWND m_hwnd;
    uint32_t m_width;
    uint32_t m_height;

    ComPtr<IDXGIFactory4> m_dxgiFactory;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12Fence> m_fence;
    UINT64 m_currentFence = 0;

    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;

    static const int SwapChainBufferCount = 2;
    int m_currentBackBuffer = 0;
    ComPtr<ID3D12Resource> m_swapChainBuffer[SwapChainBufferCount];
    ComPtr<ID3D12Resource> m_depthStencilBuffer;

    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    ComPtr<ID3D12DescriptorHeap> m_cbvSrvUavHeap;
    
    UINT m_rtvDescriptorSize = 0;
    UINT m_dsvDescriptorSize = 0;
    UINT m_cbvSrvUavDescriptorSize = 0;

    D3D12_VIEWPORT m_viewport;
    D3D12_RECT m_scissorRect;

    // Pipeline Objects
    ComPtr<ID3D12RootSignature> m_commonRootSignature;
    ComPtr<ID3D12PipelineState> m_shellPSO;
    ComPtr<ID3D12PipelineState> m_finPSO;
    ComPtr<ID3D12PipelineState> m_osmPSO;
    ComPtr<ID3D12PipelineState> m_opaquePSO;

    // Buffers and Textures
    ComPtr<ID3D12Resource> m_noiseTex;
    ComPtr<ID3D12Resource> m_frameCB;
    ComPtr<ID3D12Resource> m_furCB;
    UINT8* m_frameCBMapped = nullptr;
    UINT8* m_furCBMapped = nullptr;

    ComPtr<ID3D12Resource> m_vertexBuffer;
    ComPtr<ID3D12Resource> m_indexBuffer;
    ComPtr<ID3D12Resource> m_indexBufferAdj;
    
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
    D3D12_INDEX_BUFFER_VIEW m_indexBufferAdjView;
    
    uint32_t m_indexCount = 0;
    uint32_t m_indexCountAdj = 0;
};

#endif
