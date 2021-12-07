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

#include "PostProc/MagnifierPS.h"
#include <string>

struct UIState
{
    //
    // WINDOW MANAGEMENT
    //
    bool bShowControlsWindow;
    bool bShowProfilerWindow;
    bool bShowReflectionsWindow;

    //
    // POST PROCESS CONTROLS
    //
    int   SelectedTonemapperIndex;
    float Exposure;

    bool  bUseTAA;
    bool  bUseBloom;

    bool  bUseMagnifier;
    bool  bLockMagnifierPosition;
    bool  bLockMagnifierPositionHistory;
    int   LockedMagnifiedScreenPositionX;
    int   LockedMagnifiedScreenPositionY;
    CAULDRON_DX12::MagnifierPS::PassParameters MagnifierParams;


    //
    // APP/SCENE CONTROLS
    //
    float IBLFactor;
    float EmissiveFactor;

    int   SelectedSkydomeTypeIndex;
    bool  bDrawBoundingBoxes;
    bool  bDrawLightFrustum;

    enum class WireframeMode : int
    {
        WIREFRAME_MODE_OFF = 0,
        WIREFRAME_MODE_SHADED = 1,
        WIREFRAME_MODE_SOLID_COLOR = 2,
    };

    WireframeMode WireframeMode;
    float         WireframeColor[3];

    //
    // PROFILER CONTROLS
    //
    bool  bShowMilliseconds;

    //
    // REFLECTION CONTROLS
    //
    bool    bApplyScreenSpaceReflections;
    bool    bShowIntersectionResults;
    bool    bEnableTemporalVarianceGuidedTracing;
    bool    bShowReflectionTarget;
    float   targetFrameTime;
    int     maxTraversalIterations;
    int     mostDetailedDepthHierarchyMipLevel;
    int     minTraversalOccupancy;
    float   depthBufferThickness;
    float   roughnessThreshold;
    float   temporalStability;
    float   temporalVarianceThreshold;
    int     samplesPerQuad;

    // -----------------------------------------------

    void Initialize();

    void ToggleMagnifierLock();
    void AdjustMagnifierSize(float increment = 0.05f);
    void AdjustMagnifierMagnification(float increment = 1.00f);
};