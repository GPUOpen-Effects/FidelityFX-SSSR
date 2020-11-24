// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//
#pragma once

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files:
#include <windows.h>
#include <windowsx.h>

// C RunTime Header Files
#include <malloc.h>
#include <map>
#include <vector>
#include <mutex>
#include <fstream>

#include "vulkan/vulkan.h"

// we are using DirectXMath
#include <DirectXMath.h>
using namespace DirectX;

// TODO: reference additional headers your program requires here
#include "Base/Imgui.h"
#include "Base/ImguiHelper.h"
#include "Base/Device.h"
#include "Base/Helper.h"
#include "Base/Texture.h"
#include "Base/SwapChain.h"
#include "Base/UploadHeap.h"
#include "Base/GPUTimeStamps.h"
#include "Base/ExtDebugMarkers.h"
#include "Base/CommandListRing.h"
#include "Base/StaticBufferPool.h"
#include "Base/DynamicBufferRing.h"
#include "Base/ResourceViewHeaps.h"
#include "Base/ShaderCompilerHelper.h"

#include "Misc/Misc.h"
#include "Misc/Camera.h"
#include "Misc/FrameworkWindows.h"

#include "PostProc/Bloom.h"
#include "PostProc/BlurPS.h"
#include "PostProc/SkyDome.h"
#include "PostProc/ToneMapping.h"
#include "PostProc/SkyDomeProc.h"
#include "PostProc/DownSamplePS.h"
#include "PostProc/PostProcCS.h"

#include "GLTF/GltfPbrPass.h"
#include "GLTF/GltfBBoxPass.h"
#include "GLTF/GltfDepthPass.h"
#include "GLTF/GltfMotionVectorsPass.h"

#include "Widgets/Axis.h"
#include "Widgets/CheckerBoardFloor.h"
#include "Widgets/WireframeBox.h"
#include "Widgets/WireframeSphere.h"

using namespace CAULDRON_VK;