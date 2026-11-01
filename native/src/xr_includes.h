#pragma once

#include <windows.h>
#include <unknwn.h>
#include <d3d11.h>
#include <dxgi1_2.h>

// The loader is loaded at runtime, so we only want the PFN_ typedefs.
#define XR_NO_PROTOTYPES
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
