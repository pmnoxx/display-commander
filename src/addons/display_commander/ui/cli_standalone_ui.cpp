// UITest: run the installer UI inside the addon DLL (no separate .exe).
// Uses a second ImGui build in namespace ImGuiStandalone (via compile define ImGui=ImGuiStandalone)
// to avoid symbol clash with ReShade's ImGui used in-game.

#define ImGui ImGuiStandalone
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <string>
#include <vector>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#ifndef IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
#define IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
#endif

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

static bool CreateDeviceD3D(HWND hWnd);
static void CleanupDeviceD3D();
static void CreateRenderTarget();
static void CleanupRenderTarget();
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static const wchar_t* s_reshadeDllNames[] = {
    L"dxgi.dll", L"d3d9.dll", L"d3d11.dll", L"d3d12.dll", L"opengl32.dll",
    L"ReShade64.dll", L"ReShade32.dll"
};

static void CollectReShadeDllsInDir(const std::wstring& dir, std::vector<std::wstring>& outPresent) {
    outPresent.clear();
    for (const wchar_t* name : s_reshadeDllNames) {
        std::wstring path = dir + L"\\" + name;
        DWORD att = GetFileAttributesW(path.c_str());
        if (att != INVALID_FILE_ATTRIBUTES && !(att & FILE_ATTRIBUTE_DIRECTORY))
            outPresent.push_back(name);
    }
}

void RunStandaloneUI(HINSTANCE hInst) {
    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = (HINSTANCE)hInst;
    wc.lpszClassName = L"DisplayCommanderUI";
    if (!RegisterClassExW(&wc))
        return;

    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Display Commander - Installer",
                              WS_OVERLAPPEDWINDOW, 100, 100, 800, 500, nullptr, nullptr, (HINSTANCE)hInst, nullptr);
    if (!hwnd) {
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return;
    }

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    wchar_t modulePath[2048];
    DWORD modLen = GetModuleFileNameW((HMODULE)hInst, modulePath, (DWORD)std::size(modulePath));
    std::wstring addonDir;
    if (modLen > 0 && modLen < (DWORD)std::size(modulePath)) {
        addonDir.assign(modulePath);
        size_t last = addonDir.find_last_of(L"\\/");
        if (last != std::wstring::npos)
            addonDir.resize(last);
    }
    std::vector<std::wstring> reshadeDllsPresent;
    if (!addonDir.empty())
        CollectReShadeDllsInDir(addonDir, reshadeDllsPresent);

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Display Commander - Installer", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Display Commander Installer UI");
            ImGui::Separator();
            ImGui::Text("Run via: rundll32.exe zzz_DisplayCommander.addon64,CommandLine UITest");
            ImGui::Spacing();
            ImGui::Text("ReShade DLLs in this folder:");
            if (addonDir.empty()) {
                ImGui::TextUnformatted("(unknown path)");
            } else if (reshadeDllsPresent.empty()) {
                ImGui::TextUnformatted("(none of dxgi/d3d9/d3d11/d3d12/opengl32/ReShade64/ReShade32)");
            } else {
                for (const auto& n : reshadeDllsPresent) {
                    int need = WideCharToMultiByte(CP_UTF8, 0, n.c_str(), (int)n.size(), nullptr, 0, nullptr, nullptr);
                    if (need > 0) {
                        std::string utf8(static_cast<size_t>(need), 0);
                        WideCharToMultiByte(CP_UTF8, 0, n.c_str(), (int)n.size(), &utf8[0], need, nullptr, nullptr);
                        ImGui::BulletText("%s", utf8.c_str());
                    }
                }
            }
            ImGui::Spacing();
            if (ImGui::Button("Close"))
                done = true;
            ImGui::End();
        }

        ImGui::Render();
        const float clear[4] = {0.15f, 0.15f, 0.18f, 1.0f};
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}

static bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                               D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel,
                                               &g_pd3dDeviceContext);
    if (hr == DXGI_ERROR_UNSUPPORTED)
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2,
                                          D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel,
                                          &g_pd3dDeviceContext);
    if (hr != S_OK)
        return false;
    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

static void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

static void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return 1;
    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            g_ResizeWidth = (UINT)LOWORD(lParam);
            g_ResizeHeight = (UINT)HIWORD(lParam);
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
