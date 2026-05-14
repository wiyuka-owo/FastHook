#include "FastHook.hpp"
#include <windows.h>
#include <iostream>
#include <atomic>
#include <functional>
#include <memory>

#include <d3d9.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include "kiero.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_dx12.h"

#include "HookManager.hpp"

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace SharedState {
    inline HWND g_Window = nullptr;
    inline WNDPROC OriginalWndProc = nullptr;
    inline std::function<void()> g_UserCallback = nullptr;
    inline bool g_ImGuiInitialized = false;

    inline LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (g_ImGuiInitialized) {
            ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
            
            ImGuiIO& io = ImGui::GetIO();
            if (io.WantCaptureMouse && (uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONUP || uMsg == WM_RBUTTONDOWN || uMsg == WM_RBUTTONUP || uMsg == WM_MOUSEWHEEL || uMsg == WM_MOUSEMOVE))
                return true;
            if (io.WantCaptureKeyboard && (uMsg == WM_KEYDOWN || uMsg == WM_KEYUP || uMsg == WM_SYSKEYDOWN || uMsg == WM_SYSKEYUP || uMsg == WM_CHAR))
                return true;
        }
        return CallWindowProc(OriginalWndProc, hWnd, uMsg, wParam, lParam);
    }

    inline void SetupWindowHook(HWND window) {
        if (g_Window == window) return;
        if (OriginalWndProc) {
            SetWindowLongPtr(g_Window, GWLP_WNDPROC, (LONG_PTR)OriginalWndProc);
        }
        g_Window = window;
        OriginalWndProc = (WNDPROC)SetWindowLongPtr(g_Window, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
        
        if (!ImGui::GetCurrentContext()) ImGui::CreateContext();
        ImGui_ImplWin32_Init(g_Window);
    }
}

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;
    virtual bool InitHook() = 0;
    virtual void Shutdown() = 0;
};

class DX9Backend : public IRenderBackend {
public:
    bool InitHook() override {
        if (kiero::init(kiero::RenderType::D3D9) == kiero::Status::Success) {
            kiero::bind(42, (void**)&OriginalEndScene9, HookedEndScene9);
            kiero::bind(16, (void**)&OriginalReset9, HookedReset9);
            return true;
        }
        return false;
    }

    void Shutdown() override {
        if (SharedState::g_ImGuiInitialized) {
            ImGui_ImplDX9_Shutdown();
        }
    }

private:
    using EndScene9_t = long(__stdcall*)(IDirect3DDevice9*);
    using Reset9_t = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
    
    inline static EndScene9_t OriginalEndScene9 = nullptr;
    inline static Reset9_t OriginalReset9 = nullptr;
    inline static std::atomic<IDirect3DDevice9*> g_TargetDevice9{nullptr};

    static HRESULT __stdcall HookedReset9(IDirect3DDevice9* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters) {
        if (SharedState::g_ImGuiInitialized) {
            ImGui_ImplDX9_InvalidateDeviceObjects();
        }
        HRESULT hr = OriginalReset9(pDevice, pPresentationParameters);
        if (hr >= 0 && SharedState::g_ImGuiInitialized) {
            ImGui_ImplDX9_CreateDeviceObjects();
        }
        return hr;
    }

    static long __stdcall HookedEndScene9(IDirect3DDevice9* pDevice) {
        IDirect3DDevice9* expected = nullptr;
        g_TargetDevice9.compare_exchange_strong(expected, pDevice);
        if (pDevice != g_TargetDevice9.load()) {
            return OriginalEndScene9(pDevice);
        }

        if (!SharedState::g_ImGuiInitialized) {
            D3DDEVICE_CREATION_PARAMETERS params;
            pDevice->GetCreationParameters(&params);
            SharedState::SetupWindowHook(params.hFocusWindow);
            ImGui_ImplDX9_Init(pDevice);
            SharedState::g_ImGuiInitialized = true;
        }

        if (SharedState::g_ImGuiInitialized) {
            ImGui_ImplDX9_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            
            if (SharedState::g_UserCallback) SharedState::g_UserCallback();
            
            ImGui::EndFrame();
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        }
        return OriginalEndScene9(pDevice);
    }
};

class DX11Backend : public IRenderBackend {
public:
    bool InitHook() override {
        if (kiero::init(kiero::RenderType::D3D11) == kiero::Status::Success) {
            kiero::bind(8, (void**)&OriginalPresent11, HookedPresent11);
            kiero::bind(13, (void**)&OriginalResizeBuffers11, HookedResizeBuffers11);
            return true;
        }
        return false;
    }

    void Shutdown() override {
        CleanupRenderTarget11();
        if (SharedState::g_ImGuiInitialized) {
            ImGui_ImplDX11_Shutdown();
        }
    }

private:
    using Present11_t = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
    using ResizeBuffers11_t = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
    
    inline static Present11_t OriginalPresent11 = nullptr;
    inline static ResizeBuffers11_t OriginalResizeBuffers11 = nullptr;
    
    inline static ID3D11Device* g_pDevice11 = nullptr;
    inline static ID3D11DeviceContext* g_pContext11 = nullptr;
    inline static ID3D11RenderTargetView* g_pMainRenderTargetView11 = nullptr;
    inline static std::atomic<IDXGISwapChain*> g_TargetSwapChain11{nullptr};

    static void CleanupRenderTarget11() {
        if (g_pMainRenderTargetView11) {
            g_pMainRenderTargetView11->Release();
            g_pMainRenderTargetView11 = nullptr;
        }
    }

    static HRESULT __stdcall HookedResizeBuffers11(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
        CleanupRenderTarget11();
        return OriginalResizeBuffers11(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    }

    static HRESULT __stdcall HookedPresent11(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
        if (Flags & DXGI_PRESENT_TEST) return OriginalPresent11(pSwapChain, SyncInterval, Flags);

        IDXGISwapChain* expected = nullptr;
        g_TargetSwapChain11.compare_exchange_strong(expected, pSwapChain);
        if (pSwapChain != g_TargetSwapChain11.load()) {
            return OriginalPresent11(pSwapChain, SyncInterval, Flags);
        }

        if (!SharedState::g_ImGuiInitialized) {
            if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_pDevice11))) {
                g_pDevice11->GetImmediateContext(&g_pContext11);
                DXGI_SWAP_CHAIN_DESC sd;
                pSwapChain->GetDesc(&sd);
                SharedState::SetupWindowHook(sd.OutputWindow);
                ImGui_ImplDX11_Init(g_pDevice11, g_pContext11);
                SharedState::g_ImGuiInitialized = true;
            }
        }

        if (SharedState::g_ImGuiInitialized) {
            if (!g_pMainRenderTargetView11) {
                ID3D11Texture2D* pBackBuffer = nullptr;
                if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer))) {
                    g_pDevice11->CreateRenderTargetView(pBackBuffer, NULL, &g_pMainRenderTargetView11);
                    pBackBuffer->Release();
                }
            }

            if (g_pMainRenderTargetView11) {
                ID3D11RenderTargetView* oldRTV = nullptr;
                ID3D11DepthStencilView* oldDSV = nullptr;
                g_pContext11->OMGetRenderTargets(1, &oldRTV, &oldDSV);
                g_pContext11->OMSetRenderTargets(1, &g_pMainRenderTargetView11, NULL);

                ImGui_ImplDX11_NewFrame();
                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();
                
                if (SharedState::g_UserCallback) SharedState::g_UserCallback();
                
                ImGui::Render();
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

                g_pContext11->OMSetRenderTargets(1, &oldRTV, oldDSV);
                if (oldRTV) oldRTV->Release();
                if (oldDSV) oldDSV->Release();
            }
        }
        return OriginalPresent11(pSwapChain, SyncInterval, Flags);
    }
};

class DX12Backend : public IRenderBackend {
public:
    bool InitHook() override {
        if (kiero::init(kiero::RenderType::D3D12) == kiero::Status::Success) {
            kiero::bind(54, (void**)&OriginalExecuteCommandLists, HookedExecuteCommandLists);
            kiero::bind(140, (void**)&OriginalPresent12, HookedPresent12);
            kiero::bind(145, (void**)&OriginalResizeBuffers12, HookedResizeBuffers12);
            return true;
        }
        return false;
    }

    void Shutdown() override {
        if (SharedState::g_ImGuiInitialized) {
            ImGui_ImplDX12_Shutdown();
        }
        CleanupDX12();
    }

private:
    using Present12_t = HRESULT(__stdcall*)(IDXGISwapChain3*, UINT, UINT);
    using ExecuteCommandLists_t = void(__stdcall*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
    using ResizeBuffers12_t = HRESULT(__stdcall*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

    inline static Present12_t OriginalPresent12 = nullptr;
    inline static ExecuteCommandLists_t OriginalExecuteCommandLists = nullptr;
    inline static ResizeBuffers12_t OriginalResizeBuffers12 = nullptr;
    inline static std::atomic<IDXGISwapChain3*> g_TargetSwapChain12{nullptr};

    struct FrameContext {
        ID3D12CommandAllocator* CommandAllocator;
        ID3D12Resource* MainRenderTargetResource;
        D3D12_CPU_DESCRIPTOR_HANDLE MainRenderTargetDescriptor;
    };

    inline static uint32_t g_BuffersCounts = 0;
    inline static FrameContext* g_FrameContexts = nullptr;
    inline static ID3D12Device* g_pd3dDevice12 = nullptr;
    inline static ID3D12DescriptorHeap* g_pd3dRtvDescHeap = nullptr;
    inline static ID3D12DescriptorHeap* g_pd3dSrvDescHeap = nullptr;
    inline static ID3D12CommandQueue* g_pd3dCommandQueue = nullptr;
    inline static ID3D12GraphicsCommandList* g_pd3dCommandList = nullptr;

    static void CleanupDX12() {
        if (g_FrameContexts) {
            for (UINT i = 0; i < g_BuffersCounts; i++) {
                if (g_FrameContexts[i].MainRenderTargetResource) { g_FrameContexts[i].MainRenderTargetResource->Release(); g_FrameContexts[i].MainRenderTargetResource = nullptr; }
                if (g_FrameContexts[i].CommandAllocator) { g_FrameContexts[i].CommandAllocator->Release(); g_FrameContexts[i].CommandAllocator = nullptr; }
            }
            delete[] g_FrameContexts;
            g_FrameContexts = nullptr;
        }
        if (g_pd3dCommandList) { g_pd3dCommandList->Release(); g_pd3dCommandList = nullptr; }
        if (g_pd3dRtvDescHeap) { g_pd3dRtvDescHeap->Release(); g_pd3dRtvDescHeap = nullptr; }
        if (g_pd3dSrvDescHeap) { g_pd3dSrvDescHeap->Release(); g_pd3dSrvDescHeap = nullptr; }
    }

    static HRESULT __stdcall HookedResizeBuffers12(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
        if (SharedState::g_ImGuiInitialized) {
            ImGui_ImplDX12_Shutdown();
            CleanupDX12();
            SharedState::g_ImGuiInitialized = false;
        }
        return OriginalResizeBuffers12(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    }

    static void __stdcall HookedExecuteCommandLists(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists) {
        if (!g_pd3dCommandQueue && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
            g_pd3dCommandQueue = queue;
        }
        OriginalExecuteCommandLists(queue, NumCommandLists, ppCommandLists);
    }

    static HRESULT __stdcall HookedPresent12(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags) {
        if (Flags & DXGI_PRESENT_TEST) return OriginalPresent12(pSwapChain, SyncInterval, Flags);

        IDXGISwapChain3* expected = nullptr;
        g_TargetSwapChain12.compare_exchange_strong(expected, pSwapChain);
        if (pSwapChain != g_TargetSwapChain12.load()) {
            return OriginalPresent12(pSwapChain, SyncInterval, Flags);
        }

        if (g_pd3dCommandQueue && !SharedState::g_ImGuiInitialized) {
            if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D12Device), (void**)&g_pd3dDevice12))) {
                DXGI_SWAP_CHAIN_DESC sd;
                if (SUCCEEDED(pSwapChain->GetDesc(&sd))) {
                    g_BuffersCounts = sd.BufferCount;
                    g_FrameContexts = new FrameContext[g_BuffersCounts];

                    D3D12_DESCRIPTOR_HEAP_DESC rtvdesc = {};
                    rtvdesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
                    rtvdesc.NumDescriptors = g_BuffersCounts;
                    rtvdesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
                    rtvdesc.NodeMask = 1;
                    g_pd3dDevice12->CreateDescriptorHeap(&rtvdesc, IID_PPV_ARGS(&g_pd3dRtvDescHeap));

                    D3D12_DESCRIPTOR_HEAP_DESC srvdesc = {};
                    srvdesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                    srvdesc.NumDescriptors = 1;
                    srvdesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                    g_pd3dDevice12->CreateDescriptorHeap(&srvdesc, IID_PPV_ARGS(&g_pd3dSrvDescHeap));

                    SIZE_T rtvDescriptorSize = g_pd3dDevice12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
                    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_pd3dRtvDescHeap->GetCPUDescriptorHandleForHeapStart();
                    for (UINT i = 0; i < g_BuffersCounts; i++) {
                        g_FrameContexts[i].MainRenderTargetDescriptor = rtvHandle;
                        pSwapChain->GetBuffer(i, IID_PPV_ARGS(&g_FrameContexts[i].MainRenderTargetResource));
                        g_pd3dDevice12->CreateRenderTargetView(g_FrameContexts[i].MainRenderTargetResource, nullptr, rtvHandle);
                        rtvHandle.ptr += rtvDescriptorSize;
                        g_pd3dDevice12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_FrameContexts[i].CommandAllocator));
                    }

                    g_pd3dDevice12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_FrameContexts[0].CommandAllocator, nullptr, IID_PPV_ARGS(&g_pd3dCommandList));
                    g_pd3dCommandList->Close();
                    
                    SharedState::SetupWindowHook(sd.OutputWindow);
                    
                    ImGui_ImplDX12_InitInfo init_info = {};
                    init_info.Device = g_pd3dDevice12;
                    init_info.CommandQueue = g_pd3dCommandQueue;
                    init_info.NumFramesInFlight = g_BuffersCounts;
                    init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
                    init_info.SrvDescriptorHeap = g_pd3dSrvDescHeap;
                    init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu) {
                        *out_cpu = info->SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
                        *out_gpu = info->SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
                    };
                    init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu) {};

                    ImGui_ImplDX12_Init(&init_info);
                    ImGui::GetIO().Fonts->Build();
                    ImGui_ImplDX12_CreateDeviceObjects();

                    SharedState::g_ImGuiInitialized = true;
                }
            }
        }

        if (SharedState::g_ImGuiInitialized) {
            UINT backBufferIdx = pSwapChain->GetCurrentBackBufferIndex();
            FrameContext& currentFrameContext = g_FrameContexts[backBufferIdx];

            currentFrameContext.CommandAllocator->Reset();
            g_pd3dCommandList->Reset(currentFrameContext.CommandAllocator, nullptr);

            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = currentFrameContext.MainRenderTargetResource;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            g_pd3dCommandList->ResourceBarrier(1, &barrier);

            g_pd3dCommandList->OMSetRenderTargets(1, &currentFrameContext.MainRenderTargetDescriptor, FALSE, nullptr);
            g_pd3dCommandList->SetDescriptorHeaps(1, &g_pd3dSrvDescHeap);

            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            
            if (SharedState::g_UserCallback) SharedState::g_UserCallback();
            
            ImGui::Render();
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_pd3dCommandList);

            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
            g_pd3dCommandList->ResourceBarrier(1, &barrier);

            g_pd3dCommandList->Close();
            ID3D12CommandList* ppCommandLists[] = { g_pd3dCommandList };
            g_pd3dCommandQueue->ExecuteCommandLists(1, ppCommandLists);
        }

        return OriginalPresent12(pSwapChain, SyncInterval, Flags);
    }
};

static std::unique_ptr<IRenderBackend> g_CurrentBackend = nullptr;

void FastHook::Shutdown() {
    FastHook::HookManager::DisableAll();

    kiero::shutdown();

    if (g_CurrentBackend) {
        g_CurrentBackend->Shutdown();
        g_CurrentBackend.reset();
    }

    if (SharedState::OriginalWndProc && SharedState::g_Window) {
        SetWindowLongPtr(SharedState::g_Window, GWLP_WNDPROC, (LONG_PTR)SharedState::OriginalWndProc);
    }

    if (SharedState::g_ImGuiInitialized) {
        ImGui_ImplWin32_Shutdown();
        if (ImGui::GetCurrentContext()) {
            ImGui::DestroyContext();
        }
        SharedState::g_ImGuiInitialized = false;
    }
}

void FastHook::Init(RenderAPI api, std::function<void()> renderCallback) {
    SharedState::g_UserCallback = renderCallback;

    CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
        RenderAPI targetAPI = *(RenderAPI*)lpParam;
        delete (RenderAPI*)lpParam;

        switch (targetAPI) {
            case RenderAPI::D3D9:
                g_CurrentBackend = std::make_unique<DX9Backend>();
                break;
            case RenderAPI::D3D11:
                g_CurrentBackend = std::make_unique<DX11Backend>();
                break;
            case RenderAPI::D3D12:
                g_CurrentBackend = std::make_unique<DX12Backend>();
                break;
        }

        if (g_CurrentBackend) {
            bool init_hook = false;
            do {
                init_hook = g_CurrentBackend->InitHook();
                if (!init_hook) Sleep(100);
            } while (!init_hook);
            if (HookManager::EnableAll()) {
                std::cout << "[+] Game Hooks Enabled Successfully!" << std::endl;
            }
        }

        return 0;
    }, new RenderAPI(api), 0, nullptr);
}
