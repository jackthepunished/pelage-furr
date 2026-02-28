#include "FurRenderer.h"
#include <iostream>

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, PSTR, int) {
    // Register Window Class
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "FurRendererApp";
    RegisterClass(&wc);

    // Create Window
    uint32_t width = 1280;
    uint32_t height = 720;
    HWND hwnd = CreateWindow(wc.lpszClassName, "Pelage D3D12 Fur Renderer", WS_OVERLAPPEDWINDOW, 100, 100, width, height, nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) {
        MessageBox(nullptr, "Failed to create window", "Error", MB_OK);
        return 0;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // Initialize Renderer
    FurRenderer renderer(hwnd, width, height);
    renderer.Init();

    // Main Loop
    MSG msg = {};
    LARGE_INTEGER start, end, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            QueryPerformanceCounter(&end);
            float dt = static_cast<float>(end.QuadPart - start.QuadPart) / freq.QuadPart;
            start = end;

            renderer.Update(dt);
            renderer.Render();
        }
    }

    return 0;
}
