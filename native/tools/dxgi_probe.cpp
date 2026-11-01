// Creates a throwaway swapchain in its own process and prints where DXGI's swapchain vtable is
// inside dxgi.dll. TrackmaniaVR.dll runs this instead of creating a swapchain inside the game,
// where Openplanet would see (and track) it.
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstdio>

using Microsoft::WRL::ComPtr;

int main()
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"dxgi_probe";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 16, 16, nullptr,
        nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        printf("error=CreateWindow failed (%lu)\n", GetLastError());
        return 1;
    }

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 1;
    desc.BufferDesc.Width = 16;
    desc.BufferDesc.Height = 16;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = hwnd;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    ComPtr<IDXGISwapChain> swapChain;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    const HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &desc, &swapChain, &device, nullptr, &context);
    if (FAILED(hr)) {
        printf("error=D3D11CreateDeviceAndSwapChain failed (0x%08lX)\n", hr);
        return 1;
    }

    const auto dxgi = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"dxgi.dll"));
    const auto vtable = *reinterpret_cast<uintptr_t*>(swapChain.Get());
    printf("dxgi_base=0x%llx\n", static_cast<unsigned long long>(dxgi));
    printf("vtable_rva=0x%llx\n", static_cast<unsigned long long>(vtable - dxgi));
    return 0;
}
