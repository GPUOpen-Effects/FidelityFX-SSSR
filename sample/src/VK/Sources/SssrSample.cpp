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

#include "stdafx.h"

#include "SssrSample.h"
#include "base/ShaderCompilerCache.h"
#include "base/Instance.h"

namespace SSSR_SAMPLE_VK
{
	SssrSample::SssrSample(LPCSTR name) : FrameworkWindows(name)
	{
		m_DeltaTime = 0;
		m_Distance = 0;
		m_Pitch = 0;
		m_Yaw = 0;
		m_selectedScene = 0;
		m_LastFrameTime = MillisecondsNow();
		m_Time = 0;
		m_bPlay = true;
		m_bShowUI = true;

		m_CameraControlSelected = 0; // select WASD on start up

		m_pGltfLoader = NULL;
	}

	//--------------------------------------------------------------------------------------
	//
	// OnCreate
	//
	//--------------------------------------------------------------------------------------
	void SssrSample::OnCreate(HWND hWnd)
	{
		// get the list of scenes
		for (const auto& scene : m_JsonConfigFile["scenes"])
			m_SceneNames.push_back(scene["name"]);

		DWORD dwAttrib = GetFileAttributes("..\\media\\");
		if ((dwAttrib == INVALID_FILE_ATTRIBUTES) || ((dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) == 0)
		{
			MessageBox(NULL, "Media files not found!\n\nPlease check the readme on how to get the media files.", "Cauldron Panic!", MB_ICONERROR);
			exit(0);
		}

		// Create Device
		//
#ifdef _DEBUG
		bool cpuValidationLayerEnabled = true;
		bool gpuValidationLayerEnabled = false;
#else
		bool cpuValidationLayerEnabled = false;
		bool gpuValidationLayerEnabled = false;
#endif

		// Create the device
		InstanceProperties ip;
		ip.Init();
		m_Device.SetEssentialInstanceExtensions(cpuValidationLayerEnabled, gpuValidationLayerEnabled, &ip);

		// Create instance
		VkInstance vulkanInstance;
		VkPhysicalDevice physicalDevice;
		CreateInstance("SssrSample", "Cauldron", &vulkanInstance, &physicalDevice, &ip);

		DeviceProperties dp;
		dp.Init(physicalDevice);
		m_Device.SetEssentialDeviceExtensions(&dp);
		dp.AddDeviceExtensionName(VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME);

		bool addedSubgroupSizeControl = dp.AddDeviceExtensionName(VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME);

		VkPhysicalDeviceSubgroupSizeControlFeaturesEXT subgroupSizeControlFeatures = {};
		subgroupSizeControlFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT;
		subgroupSizeControlFeatures.pNext = nullptr;
		subgroupSizeControlFeatures.subgroupSizeControl = true;
		subgroupSizeControlFeatures.computeFullSubgroups = false;
		if (addedSubgroupSizeControl)
		{
			dp.SetNewNext(&subgroupSizeControlFeatures);
		}

		// Create device
		m_Device.OnCreateEx(vulkanInstance, physicalDevice, hWnd, &dp);

		m_Device.CreatePipelineCache();

		// Init the shader compiler
		InitDirectXCompiler();
		CreateShaderCache();

		// Create Swapchain
		//
		uint32_t dwNumberOfBackBuffers = 2;
		m_SwapChain.OnCreate(&m_Device, dwNumberOfBackBuffers, hWnd);

		// Create a instance of the renderer and initialize it, we need to do that for each GPU
		//
		m_Node = new SampleRenderer();
		m_Node->OnCreate(&m_Device, &m_SwapChain);

		// init GUI (non gfx stuff)
		//
		ImGUI_Init((void*)hWnd);

		// Init Camera, looking at the origin
		//
		m_Yaw = 0.0f;
		m_Pitch = 0.0f;
		m_Distance = 3.5f;

		// init GUI state   
		m_State.toneMapper = 2;
		m_State.skyDomeType = 1;
		m_State.exposure = 1.0f;
		m_State.emmisiveFactor = 1.0f;
		m_State.iblFactor = 1.0f;
		m_State.bDrawBoundingBoxes = false;
		m_State.bDrawLightFrustum = false;
		m_State.bDrawBloom = false;
		m_State.camera.LookAt(m_Yaw, m_Pitch, m_Distance, XMVectorSet(0, 0, 0, 0));
		m_State.lightIntensity = 10.f;
		m_State.lightCamera.SetFov(XM_PI / 6.0f, 1024, 1024, 0.1f, 20.0f);
		m_State.lightCamera.LookAt(XM_PI / 2.0f, 0.58f, 3.5f, XMVectorSet(0, 0, 0, 0));
		m_State.lightColor = XMFLOAT3(1, 1, 1);
		m_State.targetFrametime = 0;
		m_State.temporalStability = 0.99f;
		m_State.temporalVarianceThreshold = 0.002f;
		m_State.maxTraversalIterations = 128;
		m_State.mostDetailedDepthHierarchyMipLevel = 1;
		m_State.depthBufferThickness = 0.015f;
		m_State.minTraversalOccupancy = 4;
		m_State.samplesPerQuad = 1;
		m_State.bEnableVarianceGuidedTracing = true;
		m_State.bShowIntersectionResults = false;
		m_State.roughnessThreshold = 0.2f;
		m_State.showReflectionTarget = false;
		m_State.bDrawScreenSpaceReflections = true;

		LoadScene(m_selectedScene);
	}

	//--------------------------------------------------------------------------------------
	//
	// OnDestroy
	//
	//--------------------------------------------------------------------------------------
	void SssrSample::OnDestroy()
	{
		ImGUI_Shutdown();

		m_Device.GPUFlush();

		// Fullscreen state should always be false before exiting the app.
		m_SwapChain.SetFullScreen(false);

		m_Node->UnloadScene();
		m_Node->OnDestroyWindowSizeDependentResources();
		m_Node->OnDestroy();

		delete m_Node;

		m_SwapChain.OnDestroyWindowSizeDependentResources();
		m_SwapChain.OnDestroy();

		//shut down the shader compiler 
		DestroyShaderCache(&m_Device);

		m_Device.DestroyPipelineCache();

		if (m_pGltfLoader)
		{
			delete m_pGltfLoader;
			m_pGltfLoader = NULL;
		}

		m_Device.OnDestroy();
	}

	//--------------------------------------------------------------------------------------
	//
	// OnEvent, forward Win32 events to ImGUI
	//
	//--------------------------------------------------------------------------------------
	bool SssrSample::OnEvent(MSG msg)
	{
		if (ImGUI_WndProcHandler(msg.hwnd, msg.message, msg.wParam, msg.lParam))
			return true;

		return true;
	}

	//--------------------------------------------------------------------------------------
	//
	// SetFullScreen
	//
	//--------------------------------------------------------------------------------------
	void SssrSample::SetFullScreen(bool fullscreen)
	{
		m_Device.GPUFlush();

		m_SwapChain.SetFullScreen(fullscreen);
	}

	void SssrSample::OnParseCommandLine(LPSTR lpCmdLine, uint32_t* pWidth, uint32_t* pHeight, bool* pbFullScreen)
	{
		// First load configuration
		std::ifstream f("config.json");
		if (!f)
		{
			MessageBox(NULL, "Config file not found!\n", "Cauldron Panic!", MB_ICONERROR);
			exit(-1);
		}
		f >> m_JsonConfigFile;

		// Parse command line and override the config file
		try
		{
			if (strlen(lpCmdLine) > 0)
			{
				auto j3 = json::parse(lpCmdLine);
				m_JsonConfigFile.merge_patch(j3);
			}
		}
		catch (json::parse_error)
		{
			Trace("Error parsing commandline\n");
			exit(0);
		}

		// Set values
		*pWidth = m_JsonConfigFile.value("width", 1920);
		*pHeight = m_JsonConfigFile.value("height", 1080);
		*pbFullScreen = m_JsonConfigFile.value("fullScreen", false);
		m_State.isBenchmarking = m_JsonConfigFile.value("benchmark", false);
	}

	void SssrSample::BuildUI()
	{
		ImGuiStyle& style = ImGui::GetStyle();
		style.FrameBorderSize = 1.0f;

		bool opened = true;
		ImGui::Begin("FidelityFX SSSR", &opened);

		if (ImGui::CollapsingHeader("Info", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("Resolution       : %ix%i", m_Width, m_Height);
		}

		if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen))
		{
			char* cameraControl[] = { "WASD", "Orbit", "cam #0", "cam #1", "cam #2", "cam #3" , "cam #4", "cam #5" };
			if (m_CameraControlSelected >= m_pGltfLoader->m_cameras.size() + 2)
				m_CameraControlSelected = 0;
			ImGui::Combo("Camera", &m_CameraControlSelected, cameraControl, (int)(m_pGltfLoader->m_cameras.size() + 2));
		}

		if (ImGui::CollapsingHeader("Reflections", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Draw Screen Space Reflections", &m_State.bDrawScreenSpaceReflections);
			ImGui::Checkbox("Show Reflection Target", &m_State.showReflectionTarget);
			ImGui::Checkbox("Show Intersection Results", &m_State.bShowIntersectionResults);
			ImGui::SliderFloat("Target Frametime in ms", &m_State.targetFrametime, 0.0f, 50.0f);
			ImGui::SliderInt("Max Traversal Iterations", &m_State.maxTraversalIterations, 0, 256);
			ImGui::SliderInt("Min Traversal Occupancy", &m_State.minTraversalOccupancy, 0, 32);
			ImGui::SliderInt("Most Detailed Level", &m_State.mostDetailedDepthHierarchyMipLevel, 0, 5);
			ImGui::SliderFloat("Depth Buffer Thickness", &m_State.depthBufferThickness, 0.0f, 0.03f);
			ImGui::SliderFloat("Roughness Threshold", &m_State.roughnessThreshold, 0.0f, 1.f);
			ImGui::SliderFloat("Temporal Stability", &m_State.temporalStability, 0.0f, 1.0f);
			ImGui::SliderFloat("Temporal Variance Threshold", &m_State.temporalVarianceThreshold, 0.0f, 0.01f);
			ImGui::Checkbox("Enable Variance Guided Tracing", &m_State.bEnableVarianceGuidedTracing);

			ImGui::Text("Samples Per Quad"); ImGui::SameLine();
			ImGui::RadioButton("1", &m_State.samplesPerQuad, 1); ImGui::SameLine();
			ImGui::RadioButton("2", &m_State.samplesPerQuad, 2); ImGui::SameLine();
			ImGui::RadioButton("4", &m_State.samplesPerQuad, 4);

			ImGui::Value("Tile Classification Elapsed Time", 1000 * m_State.tileClassificationTime, "%.1f us");
			ImGui::Value("Intersection Elapsed Time", 1000 * m_State.intersectionTime, "%.1f us");
			ImGui::Value("Denoising Elapsed Time", 1000 * m_State.denoisingTime, "%.1f us");
		}

		if (ImGui::CollapsingHeader("Profiler"))
		{
			const std::vector<TimeStamp>& timeStamps = m_Node->GetTimingValues();
			if (timeStamps.size() > 0)
			{
				for (uint32_t i = 0; i < timeStamps.size(); i++)
				{
					ImGui::Text("%-22s: %7.1f", timeStamps[i].m_label.c_str(), timeStamps[i].m_microseconds);
				}

				//scrolling data and average computing
				static float values[128];
				values[127] = timeStamps.back().m_microseconds;
				for (uint32_t i = 0; i < 128 - 1; i++) { values[i] = values[i + 1]; }
				ImGui::PlotLines("", values, 128, 0, "GPU frame time (us)", 0.0f, 30000.0f, ImVec2(0, 80));
			}
		}

		ImGui::Text("'X' to show/hide GUI");
		ImGui::End();
	}

	void SssrSample::HandleInput()
	{
		// If the mouse was not used by the GUI then it's for the camera
		//
		ImGuiIO& io = ImGui::GetIO();

		static std::chrono::system_clock::time_point last = std::chrono::system_clock::now();
		std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
		std::chrono::duration<double> diff = now - last;
		last = now;

		io.DeltaTime = static_cast<float>(diff.count());

		if (ImGui::IsKeyPressed('X'))
		{
			m_bShowUI = !m_bShowUI;
			ShowCursor(m_bShowUI);
		}

		if (io.WantCaptureMouse == false || !m_bShowUI)
		{
			if ((io.KeyCtrl == false) && (io.MouseDown[0] == true))
			{
				m_Yaw -= io.MouseDelta.x / 100.f;
				m_Pitch += io.MouseDelta.y / 100.f;
			}

			// Choose camera movement depending on setting
			//
			if (m_CameraControlSelected == 0)
			{
				//  WASD
				//
				m_State.camera.UpdateCameraWASD(m_Yaw, m_Pitch, io.KeysDown, io.DeltaTime);
			}
			else if (m_CameraControlSelected == 1)
			{
				//  Orbiting
				//
				m_Distance -= (float)io.MouseWheel / 3.0f;
				m_Distance = std::max<float>(m_Distance, 0.1f);

				bool panning = (io.KeyCtrl == true) && (io.MouseDown[0] == true);

				m_State.camera.UpdateCameraPolar(m_Yaw, m_Pitch, panning ? -io.MouseDelta.x / 100.0f : 0.0f, panning ? io.MouseDelta.y / 100.0f : 0.0f, m_Distance);
			}
			else
			{
				// Use a camera from the GLTF
				// 
				m_pGltfLoader->GetCamera(m_CameraControlSelected - 2, &m_State.camera);
				m_Yaw = m_State.camera.GetYaw();
				m_Pitch = m_State.camera.GetPitch();
			}
		}
	}

	void SssrSample::LoadScene(int sceneIndex)
	{
		json scene = m_JsonConfigFile["scenes"][sceneIndex];
		if (m_pGltfLoader != NULL)
		{
			//free resources, unload the current scene, and load new scene...
			m_Device.GPUFlush();

			m_Node->UnloadScene();
			m_Node->OnDestroyWindowSizeDependentResources();
			m_Node->OnDestroy();
			m_pGltfLoader->Unload();
			m_Node->OnCreate(&m_Device, &m_SwapChain);
			m_Node->OnCreateWindowSizeDependentResources(&m_SwapChain, m_Width, m_Height);
		}

		delete(m_pGltfLoader);
		m_pGltfLoader = new GLTFCommon();

		if (m_pGltfLoader->Load(scene["directory"], scene["filename"]) == false)
		{
			MessageBox(NULL, "The selected model couldn't be found, please check the documentation", "Cauldron Panic!", MB_ICONERROR);
			exit(0);
		}

		// Load the UI settings, and also some defaults cameras and lights, in case the GLTF has none
		{
#define LOAD(j, key, val) val = j.value(key, val)

			// global settings
			LOAD(scene, "toneMapper", m_State.toneMapper);
			LOAD(scene, "skyDomeType", m_State.skyDomeType);
			LOAD(scene, "exposure", m_State.exposure);
			LOAD(scene, "iblFactor", m_State.iblFactor);
			LOAD(scene, "emmisiveFactor", m_State.emmisiveFactor);
			LOAD(scene, "skyDomeType", m_State.skyDomeType);

			// default light
			m_State.lightIntensity = scene.value("intensity", 1.0f);

			// default camera (in case the gltf has none)
			json camera = scene["camera"];
			LOAD(camera, "yaw", m_Yaw);
			LOAD(camera, "pitch", m_Pitch);
			LOAD(camera, "distance", m_Distance);
			XMVECTOR lookAt = GetVector(GetElementJsonArray(camera, "lookAt", { 0.0, 0.0, 0.0 }));
			m_State.camera.LookAt(m_Yaw, m_Pitch, m_Distance, lookAt);

			// set benchmarking state if enabled 
			if (m_State.isBenchmarking)
			{
				BenchmarkConfig(scene["BenchmarkSettings"], -1, m_pGltfLoader);
			}

			// indicate the mainloop we started loading a GLTF and it needs to load the rest (textures and geometry)
			m_bLoadingScene = true;
		}
	}

	//--------------------------------------------------------------------------------------
	//
	// OnResize
	//
	//--------------------------------------------------------------------------------------
	void SssrSample::OnResize(uint32_t width, uint32_t height)
	{
		if (m_Width != width || m_Height != height)
		{
			// Flush GPU
			//
			m_Device.GPUFlush();

			// If resizing but no minimizing
			//
			if (m_Width > 0 && m_Height > 0)
			{
				if (m_Node != NULL)
				{
					m_Node->OnDestroyWindowSizeDependentResources();
				}
				m_SwapChain.OnDestroyWindowSizeDependentResources();
			}

			m_Width = width;
			m_Height = height;

			// if resizing but not minimizing the recreate it with the new size
			//
			if (m_Width > 0 && m_Height > 0)
			{
				m_SwapChain.OnCreateWindowSizeDependentResources(m_Width, m_Height, false, DISPLAYMODE_SDR);
				if (m_Node != NULL)
				{
					m_Node->OnCreateWindowSizeDependentResources(&m_SwapChain, m_Width, m_Height);
				}
			}
		}
		m_State.camera.SetFov(XM_PI / 4, m_Width, m_Height, 0.1f, 1000.0f);
	}

	//--------------------------------------------------------------------------------------
	//
	// OnRender, updates the state from the UI, animates, transforms and renders the scene
	//
	//--------------------------------------------------------------------------------------
	void SssrSample::OnRender()
	{
		// Get timings
		//
		double timeNow = MillisecondsNow();
		m_DeltaTime = timeNow - m_LastFrameTime;
		m_LastFrameTime = timeNow;

		// Build UI and set the scene state. Note that the rendering of the UI happens later.
		//    
		ImGUI_UpdateIO();
		ImGui::NewFrame();

		if (m_bLoadingScene)
		{
			static int loadingStage = 0;
			// LoadScene needs to be called a number of times, the scene is not fully loaded until it returns 0
			// This is done so we can display a progress bar when the scene is loading
			loadingStage = m_Node->LoadScene(m_pGltfLoader, loadingStage);
			if (loadingStage == 0)
			{
				m_Time = 0;
				m_bLoadingScene = false;
			}
		}
		else if (m_pGltfLoader && m_State.isBenchmarking)
		{
			const std::vector<TimeStamp>& timeStamps = m_Node->GetTimingValues();
			const std::string* screenshotName;
			m_Time = BenchmarkLoop(timeStamps, &m_State.camera, &screenshotName);
		}
		else
		{
			if (m_bShowUI)
			{
				BuildUI();
			}

			if (!m_bLoadingScene)
			{
				HandleInput();
			}
		}

		// Set animation time
		//
		if (m_bPlay)
		{
			m_Time += (float)m_DeltaTime / 1000.0f;
		}

		// Animate and transform the scene
		//
		if (m_pGltfLoader)
		{
			m_pGltfLoader->SetAnimationTime(0, m_Time);
			m_pGltfLoader->TransformScene(0, XMMatrixIdentity());
		}

		m_State.time = m_Time;

		// Do Render frame using AFR 
		//
		m_Node->OnRender(&m_State, &m_SwapChain);

		m_SwapChain.Present();
	}

}
//--------------------------------------------------------------------------------------
	//
	// WinMain
	//
	//--------------------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR lpCmdLine,
	int nCmdShow)
{
	LPCSTR Name = "FidelityFX Stochastic Screen Space Reflection Sample VK v1.2";

	// create new sample
	return RunFramework(hInstance, lpCmdLine, nCmdShow, new SSSR_SAMPLE_VK::SssrSample(Name));
}