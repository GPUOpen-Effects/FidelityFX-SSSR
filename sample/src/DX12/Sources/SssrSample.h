/**********************************************************************
Copyright (c) 2020 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
********************************************************************/
#pragma once
#include "SampleRenderer.h"

//
// This is the main class, it manages the state of the sample and does all the high level work without touching the GPU directly.
// This class uses the GPU via the the SampleRenderer class. We would have a SampleRenderer instance for each GPU.
//
// This class takes care of:
//
//    - loading a scene (just the CPU data)
//    - updating the camera
//    - keeping track of time
//    - handling the keyboard
//    - updating the animation
//    - building the UI (but do not renders it)
//    - uses the SampleRenderer to update all the state to the GPU and do the rendering
//

class SssrSample : public FrameworkWindows
{
public:
	SssrSample(LPCSTR name);
	void OnCreate(HWND hWnd) override;
	void OnDestroy() override;
	void OnRender() override;
	bool OnEvent(MSG msg) override;
	void OnResize(uint32_t Width, uint32_t Height) override;
	virtual void OnParseCommandLine(LPSTR lpCmdLine, uint32_t* pWidth, uint32_t* pHeight, bool* pbFullScreen) override;

	void SetFullScreen(bool fullscreen);

private:
	void BuildUI();
	void HandleInput();
	void LoadScene(int sceneIndex);

	Device                      m_Device;
	SwapChain                   m_Swapchain;

	GLTFCommon* m_pGltfLoader = NULL;
	bool                        m_bLoadingScene = false;

	SampleRenderer* m_Node = NULL;
	SampleRenderer::State       m_State;

	float                       m_Distance;
	float                       m_Yaw;
	float                       m_Pitch;

	float                       m_Time;             // WallClock in seconds.
	double                      m_DeltaTime;        // The elapsed time in milliseconds since the previous frame.
	double                      m_LastFrameTime;

	// json config file
	json                        m_JsonConfigFile;
	std::vector<std::string>    m_SceneNames;

	bool                        m_bPlay;
	bool                        m_bShowUI;

	int                         m_CameraControlSelected;
	int 						m_selectedScene;
};
