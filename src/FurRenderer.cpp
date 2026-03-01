#include "FurRenderer.h"
#include "GeometryGen.h"
#include <stdexcept>

// Helper to check HRESULTs
inline void ThrowIfFailed(HRESULT hr) {
    if (FAILED(hr)) {
        throw std::runtime_error("D3D12 error");
    }
}

FurRenderer::FurRenderer(HWND hwnd, uint32_t width, uint32_t height)
    : m_hwnd(hwnd), m_width(width), m_height(height) {
    m_viewport = {0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    m_scissorRect = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
}

FurRenderer::~FurRenderer() {
    FlushCommandQueue();
    // Keep it simple for now; ComPtrs will clean up
}

void FurRenderer::Init() {
    InitD3D12();
    CreateCommandObjects();
    CreateSwapChain();
    CreateRtvAndDsvDescriptorHeaps();
    CreateRootSignature();
    CreateConstantBuffers();
    CreateShadersAndPSOs();
    BuildRenderItems();
}

void FurRenderer::Update(float deltaTime) {
    static float time = 0.0f;
    time += deltaTime;

    FrameCB frameData = {};
    
    // Rotating camera
    float camRadius = 15.0f; // Zoom out
    frameData.CameraPos = XMFLOAT3(camRadius * cosf(time * 0.5f), 5.0f, camRadius * sinf(time * 0.5f));
    
    XMVECTOR pos = XMLoadFloat3(&frameData.CameraPos);
    XMVECTOR target = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    
    XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, (float)m_width / m_height, 0.1f, 100.0f);
    
    frameData.ViewProj = XMMatrixTranspose(view * proj);
    // Update frameData.Time and others ...
    
    // Apply transformations
    // Scale is baked into the vertices during load now, so we just rotate/translate if needed
    XMMATRIX rot = XMMatrixRotationX(XM_PIDIV2); // Rotate 90 degrees on X (often needed for models)
    XMMATRIX trans = XMMatrixTranslation(0.0f, 0.0f, 0.0f);
    frameData.World = XMMatrixTranspose(rot * trans);

    frameData.Time = time;
    frameData.Gravity = XMFLOAT3(0.0f, -2.5f, 0.0f);
    frameData.WindStrength = 0.2f;
    frameData.WindDirection = XMFLOAT3(1.0f, 0.0f, 0.0f);
    
    // Light Frame
    float lightRadius = 15.0f; // Scale up light for larger scene
    XMFLOAT3 lightPosF3 = XMFLOAT3(lightRadius, lightRadius, -lightRadius);
    XMVECTOR lightPos = XMLoadFloat3(&lightPosF3);
    XMMATRIX lightView = XMMatrixLookAtLH(lightPos, target, up);
    XMMATRIX lightProj = XMMatrixOrthographicLH(25.0f, 25.0f, 0.1f, 50.0f); // Wider light ortho
    XMMATRIX lightVP = lightView * lightProj;
    frameData.LightViewProj = XMMatrixTranspose(lightVP);

    memcpy(m_frameCBMapped, &frameData, sizeof(FrameCB));

    FrameCB lightData = frameData;
    lightData.CameraPos = lightPosF3; 
    lightData.ViewProj = frameData.LightViewProj; // For OSM Pass
    
    memcpy(m_lightFrameCBMapped, &lightData, sizeof(FrameCB));
}

void FurRenderer::Render() {
    HRESULT hr = m_commandAllocator->Reset();
    hr = m_commandList->Reset(m_commandAllocator.Get(), nullptr);

    // ==========================================
    // Pass 1: OSM Shadows
    // ==========================================
    D3D12_RESOURCE_BARRIER osmBarriers[4];
    for(int i = 0; i < 4; i++) {
        osmBarriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(
            m_osmTextures[i].Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    m_commandList->ResourceBarrier(4, osmBarriers);

    CD3DX12_CPU_DESCRIPTOR_HANDLE osmRtvHandle(
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
        SwapChainBufferCount + 1, m_rtvDescriptorSize);

    D3D12_CPU_DESCRIPTOR_HANDLE osmRtvs[4];
    for(int i = 0; i < 4; i++) {
        osmRtvs[i] = osmRtvHandle;
        const float clearZero[] = { 0.0f, 0.0f, 0.0f, 0.0f };
        m_commandList->ClearRenderTargetView(osmRtvHandle, clearZero, 0, nullptr);
        osmRtvHandle.Offset(1, m_rtvDescriptorSize);
    }

    m_commandList->OMSetRenderTargets(4, osmRtvs, FALSE, nullptr);
    
    D3D12_VIEWPORT osmViewport = { 0.0f, 0.0f, 1024.0f, 1024.0f, 0.0f, 1.0f };
    D3D12_RECT osmScissor = { 0, 0, 1024, 1024 };
    m_commandList->RSSetViewports(1, &osmViewport);
    m_commandList->RSSetScissorRects(1, &osmScissor);

    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_commandList->IASetIndexBuffer(&m_indexBufferView);

    m_commandList->SetPipelineState(m_osmPSO.Get());
    m_commandList->SetGraphicsRootSignature(m_commonRootSignature.Get());

    ID3D12DescriptorHeap* descriptorHeaps[] = { m_cbvSrvUavHeap.Get() };
    m_commandList->SetDescriptorHeaps(1, descriptorHeaps);

    m_commandList->SetGraphicsRootConstantBufferView(0, m_lightFrameCB->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootConstantBufferView(1, m_furCB->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootDescriptorTable(2, m_cbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart());
    
    m_commandList->DrawIndexedInstanced(m_indexCount, 32, 0, 0, 0);

    for(int i = 0; i < 4; i++) {
        osmBarriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(
            m_osmTextures[i].Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    m_commandList->ResourceBarrier(4, osmBarriers);

    // ==========================================
    // Pass 2: Main Render (MSAA Target)
    // ==========================================
    CD3DX12_RESOURCE_BARRIER transitionToRT = CD3DX12_RESOURCE_BARRIER::Transition(
        m_msaaRenderTarget.Get(),
        D3D12_RESOURCE_STATE_RESOLVE_SOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_commandList->ResourceBarrier(1, &transitionToRT);

    CD3DX12_CPU_DESCRIPTOR_HANDLE msaaRtvHandle(
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
        SwapChainBufferCount, m_rtvDescriptorSize);

    m_commandList->OMSetRenderTargets(1, &msaaRtvHandle, FALSE, nullptr);

    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    m_commandList->ClearRenderTargetView(msaaRtvHandle, clearColor, 0, nullptr);

    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);
    
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_commandList->IASetIndexBuffer(&m_indexBufferView);

    m_commandList->SetGraphicsRootConstantBufferView(0, m_frameCB->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootConstantBufferView(1, m_furCB->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootDescriptorTable(2, m_cbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart());
    
    // Bind OSM SRVs at offset 1 from the heap start
    CD3DX12_GPU_DESCRIPTOR_HANDLE osmSrvHandle(m_cbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart());
    osmSrvHandle.Offset(1, m_cbvSrvUavDescriptorSize);
    m_commandList->SetGraphicsRootDescriptorTable(3, osmSrvHandle);

    // Fins
    m_commandList->SetPipelineState(m_finPSO.Get());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ);
    m_commandList->IASetIndexBuffer(&m_indexBufferAdjView);
    m_commandList->DrawIndexedInstanced(m_indexCountAdj, 1, 0, 0, 0);

    // Shells
    m_commandList->SetPipelineState(m_shellPSO.Get());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetIndexBuffer(&m_indexBufferView);
    m_commandList->DrawIndexedInstanced(m_indexCount, 32, 0, 0, 0);

    // ==========================================
    // Pass 3: Resolve & Present
    // ==========================================
    D3D12_RESOURCE_BARRIER resolveBarriers[2];
    resolveBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
        m_msaaRenderTarget.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    resolveBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
        m_swapChainBuffer[m_currentBackBuffer].Get(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RESOLVE_DEST);
    m_commandList->ResourceBarrier(2, resolveBarriers);

    m_commandList->ResolveSubresource(
        m_swapChainBuffer[m_currentBackBuffer].Get(), 0,
        m_msaaRenderTarget.Get(), 0,
        DXGI_FORMAT_R8G8B8A8_UNORM);

    CD3DX12_RESOURCE_BARRIER presentBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_swapChainBuffer[m_currentBackBuffer].Get(),
        D3D12_RESOURCE_STATE_RESOLVE_DEST,
        D3D12_RESOURCE_STATE_PRESENT);
    m_commandList->ResourceBarrier(1, &presentBarrier);

    hr = m_commandList->Close();
    ID3D12CommandList* cmdsLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, cmdsLists);

    ThrowIfFailed(m_swapChain->Present(1, 0));
    FlushCommandQueue();

    m_currentBackBuffer = (m_currentBackBuffer + 1) % SwapChainBufferCount;
}

void FurRenderer::Resize(uint32_t width, uint32_t height) {
    // Left unimplemented for now
}

void FurRenderer::InitD3D12() {
#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
    }
#endif

    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&m_dxgiFactory)));

    ComPtr<IDXGIAdapter1> hardwareAdapter;
    // For simplicity, just pick first adapter
    for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != m_dxgiFactory->EnumAdapters1(adapterIndex, &hardwareAdapter); ++adapterIndex) {
        DXGI_ADAPTER_DESC1 desc;
        hardwareAdapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (SUCCEEDED(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_12_0, _uuidof(ID3D12Device), nullptr))) break;
    }

    ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device)));
    ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    
    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    m_cbvSrvUavDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void FurRenderer::CreateCommandObjects() {
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

    ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));

    ThrowIfFailed(m_device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList)));

    // Close in case we don't use it right away
    m_commandList->Close();
}

void FurRenderer::CreateSwapChain() {
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = m_width;
    sd.Height = m_height;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.Stereo = FALSE;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = SwapChainBufferCount;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = 0;

    ComPtr<IDXGISwapChain1> swapChain1;
    ThrowIfFailed(m_dxgiFactory->CreateSwapChainForHwnd(
        m_commandQueue.Get(), m_hwnd, &sd, nullptr, nullptr, &swapChain1));

    ThrowIfFailed(swapChain1.As(&m_swapChain));
}

void FurRenderer::CreateRtvAndDsvDescriptorHeaps() {
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
    rtvHeapDesc.NumDescriptors = SwapChainBufferCount + 1 + 4; // +1 for MSAA Render Target, +4 for OSM
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvHeapDesc.NodeMask = 0;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < SwapChainBufferCount; i++) {
        ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_swapChainBuffer[i])));
        m_device->CreateRenderTargetView(m_swapChainBuffer[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, m_rtvDescriptorSize);
    }

    // Create MSAA Render Target
    D3D12_RESOURCE_DESC msaaRTDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R8G8B8A8_UNORM, m_width, m_height, 1, 1, 4, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    
    D3D12_CLEAR_VALUE msaaClear;
    msaaClear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    msaaClear.Color[0] = 0.0f;
    msaaClear.Color[1] = 0.2f;
    msaaClear.Color[2] = 0.4f;
    msaaClear.Color[3] = 1.0f;
    
    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &msaaRTDesc,
        D3D12_RESOURCE_STATE_RESOLVE_SOURCE, // Start in resolve source so first frame transition works
        &msaaClear,
        IID_PPV_ARGS(&m_msaaRenderTarget)));
        
    m_device->CreateRenderTargetView(m_msaaRenderTarget.Get(), nullptr, rtvHandle);
    rtvHandle.Offset(1, m_rtvDescriptorSize);

    // Create OSM Render Targets
    D3D12_RESOURCE_DESC osmRTDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R8_UNORM, 1024, 1024, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    
    D3D12_CLEAR_VALUE osmClear;
    osmClear.Format = DXGI_FORMAT_R8_UNORM;
    osmClear.Color[0] = 0.0f;
    osmClear.Color[1] = 0.0f;
    osmClear.Color[2] = 0.0f;
    osmClear.Color[3] = 0.0f;

    for (int i = 0; i < 4; i++) {
        ThrowIfFailed(m_device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &osmRTDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &osmClear,
            IID_PPV_ARGS(&m_osmTextures[i])));

        D3D12_RENDER_TARGET_VIEW_DESC osmRTVDesc = {};
        osmRTVDesc.Format = DXGI_FORMAT_R8_UNORM;
        osmRTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        m_device->CreateRenderTargetView(m_osmTextures[i].Get(), &osmRTVDesc, rtvHandle);
        rtvHandle.Offset(1, m_rtvDescriptorSize);
    }

    // SRV Heap
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 10;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_cbvSrvUavHeap)));
}

void FurRenderer::CreateRootSignature() {
//... (Already done, so I will add CreateConstantBuffers right after it)

    // Root Parameter 0: CBV (Frame/Camera data)
    // Root Parameter 1: CBV (Fur parameters)
    // Root Parameter 2: Descriptor Table (1 SRV: Voronoi Noise)
    // Root Parameter 3: Descriptor Table (4 SRVs: OSM Shadow Maps)
    // Static Sampler: Linear Wrap
    
    CD3DX12_ROOT_PARAMETER1 rootParameters[4];
    rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[1].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_DESCRIPTOR_RANGE1 rangeNoise;
    rangeNoise.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
    rootParameters[2].InitAsDescriptorTable(1, &rangeNoise, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_DESCRIPTOR_RANGE1 rangeOSM;
    rangeOSM.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
    rootParameters[3].InitAsDescriptorTable(1, &rangeOSM, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_STATIC_SAMPLER_DESC sampler(
        0, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP
    );

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init_1_1(4, rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &signature, &error);
    if (FAILED(hr)) {
        if (error) {
            OutputDebugStringA((char*)error->GetBufferPointer());
        }
        ThrowIfFailed(hr);
    }
    ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_commonRootSignature)));
}

ComPtr<ID3DBlob> CompileShader(const std::wstring& filename, const D3D_SHADER_MACRO* defines, const std::string& entrypoint, const std::string& target) {
    UINT compileFlags = 0;
#if defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> byteCode = nullptr;
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompileFromFile(filename.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entrypoint.c_str(), target.c_str(), compileFlags, 0, &byteCode, &errors);

    if (errors != nullptr) {
        OutputDebugStringA((char*)errors->GetBufferPointer());
    }

    ThrowIfFailed(hr);
    return byteCode;
}

void FurRenderer::CreateShadersAndPSOs() {
    ComPtr<ID3DBlob> shellVS = CompileShader(L"shaders/shell_vs.hlsl", nullptr, "main", "vs_5_1");
    ComPtr<ID3DBlob> shellPS = CompileShader(L"shaders/shell_ps.hlsl", nullptr, "main", "ps_5_1");
    
    ComPtr<ID3DBlob> finVS = CompileShader(L"shaders/fin_vs.hlsl", nullptr, "main", "vs_5_1");
    ComPtr<ID3DBlob> finGS = CompileShader(L"shaders/fin_gs.hlsl", nullptr, "main", "gs_5_1");
    // finPS uses shellPS
    
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.pRootSignature = m_commonRootSignature.Get();
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(shellVS.Get());
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(shellPS.Get());
    
    CD3DX12_RASTERIZER_DESC rsDesc(D3D12_DEFAULT);
    rsDesc.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState = rsDesc;
    
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 4;
    psoDesc.SampleDesc.Quality = 0;
    
    // Enable Alpha to Coverage for Shells
    psoDesc.BlendState.AlphaToCoverageEnable = TRUE;
    
    // DSV is missing because we haven't created a depth buffer yet, but let's assume we'll use DXGI_FORMAT_D32_FLOAT
    // psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT; 

    // For now, no depth buffer
    psoDesc.DepthStencilState.DepthEnable = FALSE;

    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_shellPSO)));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC finPsoDesc = psoDesc;
    finPsoDesc.BlendState.AlphaToCoverageEnable = FALSE; // Fins don't need A2C, they use solid geometry
    finPsoDesc.VS = CD3DX12_SHADER_BYTECODE(finVS.Get());
    finPsoDesc.GS = CD3DX12_SHADER_BYTECODE(finGS.Get());
    // Fin uses adjacency topology!
    finPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; // Still triangles, just adjacency for input assembler

    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&finPsoDesc, IID_PPV_ARGS(&m_finPSO)));

    ComPtr<ID3DBlob> osmPS = CompileShader(L"shaders/osm_ps.hlsl", nullptr, "main", "ps_5_1");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC osmPsoDesc = psoDesc;
    osmPsoDesc.SampleDesc.Count = 1; // Shadows don't need MSAA
    osmPsoDesc.BlendState.AlphaToCoverageEnable = FALSE;
    osmPsoDesc.PS = CD3DX12_SHADER_BYTECODE(osmPS.Get());
    osmPsoDesc.NumRenderTargets = 4;
    for(int i=0; i<4; i++) {
        osmPsoDesc.RTVFormats[i] = DXGI_FORMAT_R8G8B8A8_UNORM; // Should be R8_UNORM but we reuse texture formats for simplicity in scaffolding
    }

    D3D12_RENDER_TARGET_BLEND_DESC additiveBlend = {};
    additiveBlend.BlendEnable = TRUE;
    additiveBlend.SrcBlend = D3D12_BLEND_ONE;
    additiveBlend.DestBlend = D3D12_BLEND_ONE;
    additiveBlend.BlendOp = D3D12_BLEND_OP_ADD;
    additiveBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
    additiveBlend.DestBlendAlpha = D3D12_BLEND_ONE;
    additiveBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    additiveBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    for(int i=0; i<4; ++i) {
        osmPsoDesc.BlendState.RenderTarget[i] = additiveBlend;
    }
    osmPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // No Z-write!

    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&osmPsoDesc, IID_PPV_ARGS(&m_osmPSO)));
}

void FurRenderer::CreateConstantBuffers() {
    uint32_t frameCBSize = (sizeof(FrameCB) + 255) & ~255;
    uint32_t furCBSize = (sizeof(FurCB) + 255) & ~255;

    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    
    CD3DX12_RESOURCE_DESC frameCBDesc = CD3DX12_RESOURCE_DESC::Buffer(frameCBSize);
    ThrowIfFailed(m_device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &frameCBDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_frameCB)));

    ThrowIfFailed(m_device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &frameCBDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_lightFrameCB)));

    CD3DX12_RESOURCE_DESC furCBDesc = CD3DX12_RESOURCE_DESC::Buffer(furCBSize);
    ThrowIfFailed(m_device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &furCBDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_furCB)));

    // Map them
    CD3DX12_RANGE readRange(0, 0); // No reading on CPU
    ThrowIfFailed(m_frameCB->Map(0, &readRange, reinterpret_cast<void**>(&m_frameCBMapped)));
    ThrowIfFailed(m_lightFrameCB->Map(0, &readRange, reinterpret_cast<void**>(&m_lightFrameCBMapped)));
    ThrowIfFailed(m_furCB->Map(0, &readRange, reinterpret_cast<void**>(&m_furCBMapped)));

    // Initialize Fur Parameters with recommended defaults for a Carpet
    FurCB initialFurData = {};
    initialFurData.FurLength = 0.04f; // Short fibers
    initialFurData.ShellCount = 48;   // High shell count for soft appearance
    initialFurData.Density = 120.0f;  // Extremely dense
    initialFurData.Thickness = 0.85f; // Keep tips reasonably sharp
    initialFurData.FurColor = XMFLOAT3(0.85f, 0.82f, 0.78f); // Soft off-white/cream
    
    memcpy(m_furCBMapped, &initialFurData, sizeof(FurCB));
}

ComPtr<ID3D12Resource> CreateDefaultBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const void* initData, UINT64 byteSize, ComPtr<ID3D12Resource>& uploadBuffer) {
    ComPtr<ID3D12Resource> defaultBuffer;

    CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
    
    ThrowIfFailed(device->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(defaultBuffer.GetAddressOf())));

    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(uploadBuffer.GetAddressOf())));

    D3D12_SUBRESOURCE_DATA subResourceData = {};
    subResourceData.pData = initData;
    subResourceData.RowPitch = byteSize;
    subResourceData.SlicePitch = subResourceData.RowPitch;

    CD3DX12_RESOURCE_BARRIER transitionToCopyDest = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    cmdList->ResourceBarrier(1, &transitionToCopyDest);

    UpdateSubresources<1>(cmdList, defaultBuffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);

    CD3DX12_RESOURCE_BARRIER transitionToGenericRead = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
    cmdList->ResourceBarrier(1, &transitionToGenericRead);

    return defaultBuffer;
}

void FurRenderer::BuildRenderItems() {
    ThrowIfFailed(m_commandAllocator->Reset());
    ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), nullptr));

    MeshData sphere = GeometryGen::LoadGLTF("assets/fur_carpet/scene.gltf");
    
    // Fallback if glTF fails to load
    if (sphere.Vertices.empty()) {
        OutputDebugStringA("Failed to load fur_carpet. Falling back to sphere.\n");
        sphere = GeometryGen::CreateSphere(1.0f, 20, 20);
    }

    const UINT vbByteSize = (UINT)sphere.Vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)sphere.Indices.size() * sizeof(uint32_t);
    const UINT ibAdjByteSize = (UINT)sphere.IndicesAdj.size() * sizeof(uint32_t);

    ComPtr<ID3D12Resource> vUploadBuffer, iUploadBuffer, iAdjUploadBuffer;

    m_vertexBuffer = CreateDefaultBuffer(m_device.Get(), m_commandList.Get(), sphere.Vertices.data(), vbByteSize, vUploadBuffer);
    m_indexBuffer = CreateDefaultBuffer(m_device.Get(), m_commandList.Get(), sphere.Indices.data(), ibByteSize, iUploadBuffer);
    m_indexBufferAdj = CreateDefaultBuffer(m_device.Get(), m_commandList.Get(), sphere.IndicesAdj.data(), ibAdjByteSize, iAdjUploadBuffer);

    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.StrideInBytes = sizeof(Vertex);
    m_vertexBufferView.SizeInBytes = vbByteSize;

    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    m_indexBufferView.SizeInBytes = ibByteSize;

    m_indexBufferAdjView.BufferLocation = m_indexBufferAdj->GetGPUVirtualAddress();
    m_indexBufferAdjView.Format = DXGI_FORMAT_R32_UINT;
    m_indexBufferAdjView.SizeInBytes = ibAdjByteSize;

    m_indexCount = (UINT)sphere.Indices.size();
    m_indexCountAdj = (UINT)sphere.IndicesAdj.size();

    // Generate High-Resolution Cellular (Voronoi) Noise
    const UINT texWidth = 512, texHeight = 512;
    std::vector<float> noiseData(texWidth * texHeight);
    
    // Generate feature points
    const int cells = 32;
    struct Point2D { float x, y; };
    std::vector<Point2D> points;
    for(int i = 0; i < cells * cells; ++i) {
        float px = (rand() % 1000) / 1000.0f;
        float py = (rand() % 1000) / 1000.0f;
        points.push_back({px, py});
    }

    // Evaluate Voronoi distance for each pixel
    for (UINT y = 0; y < texHeight; ++y) {
        for (UINT x = 0; x < texWidth; ++x) {
            float u = (float)x / texWidth;
            float v = (float)y / texHeight;
            
            float minDist = 1.0f;
            
            // Optimization: Only check neighboring cells (simplified here to all points for basic 32x32 grids)
            // Need to wrap distance to make it seamlessly tileable
            for (const auto& pt : points) {
                float dx = std::abs(u - pt.x);
                float dy = std::abs(v - pt.y);
                if (dx > 0.5f) dx = 1.0f - dx; // Wrap around for tiling
                if (dy > 0.5f) dy = 1.0f - dy; // Wrap around for tiling
                
                float dist = sqrt(dx * dx + dy * dy);
                if (dist < minDist) {
                    minDist = dist;
                }
            }
            
            // Invert so strands are thickest at the point centers
            // Scale so maximum value is roughly 1.0
            float cellMaxDist = sqrt(0.5f * 0.5f + 0.5f * 0.5f) / cells * 2.0f;
            float val = 1.0f - (minDist / cellMaxDist);
            if (val < 0.0f) val = 0.0f;
            
            // Optional: apply a power function to make the strands thinner and taper sharper
            val = pow(val, 2.5f);
            
            noiseData[y * texWidth + x] = val;
        }
    }
    
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R32_FLOAT;
    texDesc.Width = texWidth;
    texDesc.Height = texHeight;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    texDesc.DepthOrArraySize = 1;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(m_device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_noiseTex)));

    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(m_noiseTex.Get(), 0, 1);
    ComPtr<ID3D12Resource> noiseUploadBuffer;
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
    ThrowIfFailed(m_device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&noiseUploadBuffer)));

    D3D12_SUBRESOURCE_DATA texResourceData = {};
    texResourceData.pData = noiseData.data();
    texResourceData.RowPitch = texWidth * sizeof(float);
    texResourceData.SlicePitch = texResourceData.RowPitch * texHeight;

    UpdateSubresources(m_commandList.Get(), m_noiseTex.Get(), noiseUploadBuffer.Get(), 0, 0, 1, &texResourceData);
    
    CD3DX12_RESOURCE_BARRIER transitionToSRV = CD3DX12_RESOURCE_BARRIER::Transition(m_noiseTex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_commandList->ResourceBarrier(1, &transitionToSRV);

    // Create SRV in heap for noise (1 slot) and OSM (4 slots)
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    
    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
    
    // Slot 0: Noise
    m_device->CreateShaderResourceView(m_noiseTex.Get(), &srvDesc, hDescriptor);
    hDescriptor.Offset(1, m_cbvSrvUavDescriptorSize);
    
    // Slot 1-4: OSM Textures
    D3D12_SHADER_RESOURCE_VIEW_DESC osmSrvDesc = srvDesc;
    osmSrvDesc.Format = DXGI_FORMAT_R8_UNORM;
    for(int i = 0; i < 4; ++i) {
        m_device->CreateShaderResourceView(m_osmTextures[i].Get(), &osmSrvDesc, hDescriptor);
        hDescriptor.Offset(1, m_cbvSrvUavDescriptorSize);
    }

    ThrowIfFailed(m_commandList->Close());
    ID3D12CommandList* cmdsLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, cmdsLists);

    FlushCommandQueue();
}

void FurRenderer::FlushCommandQueue() {
    m_currentFence++;
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_currentFence));
    if (m_fence->GetCompletedValue() < m_currentFence) {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(m_fence->SetEventOnCompletion(m_currentFence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }
}
