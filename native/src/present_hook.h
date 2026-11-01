#pragma once

struct IDXGISwapChain;

namespace tmvr {

// Patches IDXGISwapChain::Present and IDXGISwapChain1::Present1 in DXGI's shared swapchain
// vtable, so every swapchain in the process (including the game's) calls OnPresentCallback
// right before presenting. Coexists with other overlays (Openplanet) that patch the same slots.
// The patch and the DLL stay in place until the process exits; this only (re)activates it.
bool InstallPresentHook();
// Makes the hooks pass-through and waits for an in-progress OnPresentCallback to return.
void UninstallPresentHook();

// True when frames are captured after Openplanet's overlay (and plugin scripts) ran for the
// frame, false when captured before them. Decides which frame a plugin camera write lands in.
// Follows SharedBlock::captureAfterOverlay when Openplanet's stub could be patched.
bool CapturesAfterOverlay();

void OnPresentCallback(IDXGISwapChain* swapChain);

} // namespace tmvr
