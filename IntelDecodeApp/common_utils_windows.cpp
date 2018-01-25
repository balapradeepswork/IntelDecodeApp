/*****************************************************************************

INTEL CORPORATION PROPRIETARY INFORMATION
This software is supplied under the terms of a license agreement or
nondisclosure agreement with Intel Corporation and may not be copied
or disclosed except in accordance with the terms of that agreement.
Copyright(c) 2005-2014 Intel Corporation. All Rights Reserved.

*****************************************************************************/

#include "common_utils.h"

// ATTENTION: If D3D surfaces are used, DX9_D3D or DX11_D3D must be set in project settings or hardcoded here
#ifndef DX11_D3D
#define DX11_D3D 1
#endif

#ifdef DX9_D3D
#include "common_directx.h"
#elif DX11_D3D
#include "common_directx11.h"
#endif

/* =======================================================
 * Windows implementation of OS-specific utility functions
 */

mfxStatus Initialize(mfxIMPL impl, mfxVersion ver, MFXVideoSession* pSession, mfxFrameAllocator* pmfxAllocator, bool bCreateSharedHandles)
{
    mfxStatus sts = MFX_ERR_NONE;

	
    // Initialize Intel Media SDK Session
    sts = pSession->Init(impl, &ver);
	if (sts != MFX_ERR_NONE)
	{
		printf("The Intel Media SDK is not supported.");
		MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
	}

	mfxIMPL impl_type;
	sts = pSession->QueryIMPL(&impl_type);   

    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
	if (impl_type != MFX_IMPL_SOFTWARE)
	{
#ifdef DX11_D3D
		impl |= MFX_IMPL_VIA_D3D11;
#endif
	}
	pSession->Close();


	// Initialize Intel Media SDK Session
	sts = pSession->Init(impl, &ver);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
	sts = pSession->QueryIMPL(&impl_type);

	

#if defined(DX9_D3D) || defined(DX11_D3D)
    // If mfxFrameAllocator is provided it means we need to setup DirectX device and memory allocator
    if (pmfxAllocator) {
        // Create DirectX device context
        mfxHDL deviceHandle;
        sts = CreateHWDevice(*pSession, &deviceHandle, NULL, bCreateSharedHandles);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

        // Provide device manager to Media SDK
        sts = pSession->SetHandle(DEVICE_MGR_TYPE, deviceHandle);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

        pmfxAllocator->pthis  = *pSession; // We use Media SDK session ID as the allocation identifier
        pmfxAllocator->Alloc  = simple_alloc;
        pmfxAllocator->Free   = simple_free;
        pmfxAllocator->Lock   = simple_lock;
        pmfxAllocator->Unlock = simple_unlock;
        pmfxAllocator->GetHDL = simple_gethdl;

        // Since we are using video memory we must provide Media SDK with an external allocator
        sts = pSession->SetFrameAllocator(pmfxAllocator);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    }
#endif

    return sts;
}

mfxStatus InitializeRender(int width, int height, HWND hWND)
{
	mfxStatus sts = MFX_ERR_NONE;

	RECT WindRect;
	GetWindowRect(hWND, &WindRect);

	sts = CreateSwapChain(hWND, WindRect.right - WindRect.left, WindRect.bottom - WindRect.top);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	sts = CreateSharedSurf(width, height);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	sts = MakeRTV();
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	SetViewPort(WindRect.right - WindRect.left, WindRect.bottom - WindRect.top);

	sts = CreateSamplerState();
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	sts = InitShaders();
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
	
	return sts;
}

void Release()
{
#if defined(DX9_D3D) || defined(DX11_D3D)
    CleanupHWDevice();
#endif
}

void mfxGetTime(mfxTime* timestamp)
{
    QueryPerformanceCounter(timestamp);
}

double TimeDiffMsec(mfxTime tfinish, mfxTime tstart)
{
    static LARGE_INTEGER tFreq = { 0 };

    if (!tFreq.QuadPart) QueryPerformanceFrequency(&tFreq);

    double freq = (double)tFreq.QuadPart;
    return 1000.0 * ((double)tfinish.QuadPart - (double)tstart.QuadPart) / freq;
}

void ClearYUVSurfaceVMem(mfxMemId memId)
{
#if defined(DX9_D3D) || defined(DX11_D3D)
    ClearYUVSurfaceD3D(memId);
#endif
}

void ClearRGBSurfaceVMem(mfxMemId memId)
{
#if defined(DX9_D3D) || defined(DX11_D3D)
    ClearRGBSurfaceD3D(memId);
#endif
}

mfxStatus RenderFrame(mfxFrameSurface1* pSurface , mfxIMPL impl_type)
{
	return Render(pSurface ,impl_type);
}