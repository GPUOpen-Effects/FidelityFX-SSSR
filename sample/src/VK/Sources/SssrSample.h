/**********************************************************************
Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.

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

#include "base/FrameworkWindows.h"
#include "Renderer.h"
#include "UI.h"

// This class encapsulates the 'application' and is responsible for handling window events and scene updates (simulation)
// Rendering and rendering resource management is done by the Renderer class

class SssrSample : public FrameworkWindows
{
public:
    SssrSample(LPCSTR name);
    void OnParseCommandLine(LPSTR lpCmdLine, uint32_t* pWidth, uint32_t* pHeight) override;
    void OnCreate() override;
    void OnDestroy() override;
    void OnRender() override;
    bool OnEvent(MSG msg) override;
    void OnResize(bool resizeRender) override;
    void OnUpdateDisplay() override;

    void BuildUI();
    void LoadScene(int sceneIndex);

    void OnUpdate();

    void HandleInput(const ImGuiIO& io);
    void UpdateCamera(Camera& cam, const ImGuiIO& io);

private:

    bool                        m_bIsBenchmarking;

    GLTFCommon* m_pGltfLoader = NULL;
    bool                        m_loadingScene = false;

    Renderer* m_pRenderer = NULL;
    UIState                     m_UIState;
    float                       m_fontSize;
    Camera                      m_camera;

    float                       m_time; // Time accumulator in seconds, used for animation.

    // json config file
    json                        m_jsonConfigFile;
    std::vector<std::string>    m_sceneNames;
    int                         m_activeScene;
    int                         m_activeCamera;

    bool                        m_bPlay;
};
