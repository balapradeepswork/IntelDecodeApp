

#include "IntelDecoder.h"
#include <d3d11.h>
#include "common_directx11.h"





IntelDecoder::IntelDecoder()
{

}

IntelDecoder::~IntelDecoder()
{

}

mfxStatus IntelDecoder::InitializeX(HWND hWnd)
{
	if (SetDecodeOptions() == MFX_ERR_NULL_PTR)
	{
		fprintf_s(stdout, "Source file couldn't be found.");
		return MFX_ERR_NULL_PTR;
	}
	// Open input H.264 elementary stream (ES) file
	MSDK_FOPEN(fSource, options.SourceName, "rb");
	MSDK_CHECK_POINTER(fSource, MFX_ERR_NULL_PTR);

	mfxIMPL impl = options.impl;

	//Version 1.3 is selected for Video Conference Mode compatibility.
	mfxVersion ver = { { 3, 1 } };
	pSession = new MFXVideoSession();

	

	pMfxAllocator = (mfxFrameAllocator*)malloc(sizeof(mfxFrameAllocator));
	memset(pMfxAllocator, 0, sizeof(mfxFrameAllocator));
	mfxStatus sts = Initialize(impl, ver, pSession, pMfxAllocator);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
	sts = pSession->QueryIMPL(&impl_type);
	if (impl_type == MFX_IMPL_SOFTWARE)
	{
		printf("Implementation type is : SOFTWARE\n");
	}
	else
	{
		printf("Implementation type is : HARDWARE\n");
	}
	//impl_type = 2;
	// Create Media SDK decoder
	mfxDEC = new MFXVideoDECODE(*pSession);
	
	SetDecParameters();

	// Prepare Media SDK bit stream buffer
	memset(&mfxBS, 0, sizeof(mfxBS));
	mfxBS.DataFlag = MFX_BITSTREAM_COMPLETE_FRAME;

	mfxBS.MaxLength = 1024 * 1024;
	mfxBS.Data = new mfxU8[mfxBS.MaxLength];
	MSDK_CHECK_POINTER(mfxBS.Data, MFX_ERR_MEMORY_ALLOC);

	// Read a chunk of data from stream file into bit stream buffer
	// - Parse bit stream, searching for header and fill video parameters structure
	// - Abort if bit stream header is not found in the first bit stream buffer chunk
	sts = ReadBitStreamData(&mfxBS, fSource);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	sts = mfxDEC->DecodeHeader(&mfxBS, &mfxVideoParams);
	MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	mfxVideoParams.AsyncDepth = 1;

	sts = InitializeRender(mfxVideoParams.mfx.FrameInfo.CropW, mfxVideoParams.mfx.FrameInfo.CropH, hWnd);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);


	// Query selected implementation and version

	
	if (impl_type == MFX_IMPL_SOFTWARE)
	{
		sts = QueryAndAllocRequiredSurfacesForSW();
		MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
	}
	else
	{
		sts = QueryAndAllocRequiredSurfacesForHW();
		MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
	}

	// Initialize the Media SDK decoder
	sts = mfxDEC->Init(&mfxVideoParams);
	MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

}

mfxStatus IntelDecoder::RunDecodeAndRender()
{
	mfxStatus sts = MFX_ERR_NONE;

	// ===============================================================
	// Start decoding the frames from the stream
	//
	
	mfxGetTime(&tStart);
	pmfxOutSurface = NULL;
	pmfxOutSurface_sw = NULL;
	nIndex = 0;
	nIndex2 = 0;
	nFrame = 0;

	//
	// Stage 1: Main decoding loop
	//
	while (MFX_ERR_NONE <= sts || MFX_ERR_MORE_DATA == sts || MFX_ERR_MORE_SURFACE == sts) {
		if (MFX_WRN_DEVICE_BUSY == sts)
			MSDK_SLEEP(1);  // Wait if device is busy, then repeat the same call to DecodeFrameAsync

		if (MFX_ERR_MORE_DATA == sts) {
			sts = ReadBitStreamData(&mfxBS, fSource);       // Read more data into input bit stream
			MSDK_BREAK_ON_ERROR(sts);
		}

		if (MFX_ERR_MORE_SURFACE == sts || MFX_ERR_NONE == sts) {
			nIndex = GetFreeSurfaceIndex(pmfxSurfaces, numSurfaces);        // Find free frame surface
			MSDK_CHECK_ERROR(MFX_ERR_NOT_FOUND, nIndex, MFX_ERR_MEMORY_ALLOC);
		}
		// Decode a frame asychronously (returns immediately)
		//  - If input bitstream contains multiple frames DecodeFrameAsync will start decoding multiple frames, and remove them from bitstream
		sts = mfxDEC->DecodeFrameAsync(&mfxBS, pmfxSurfaces[nIndex], &pmfxOutSurface, &syncp);

		// Ignore warnings if output is available,
		// if no output and no action required just repeat the DecodeFrameAsync call
		if (MFX_ERR_NONE < sts && syncp)
			sts = MFX_ERR_NONE;

		if (MFX_ERR_NONE == sts)
			sts = pSession->SyncOperation(syncp, 60000);      // Synchronize. Wait until decoded frame is ready

		if (MFX_ERR_NONE == sts) {
			++nFrame;

			if (impl_type == MFX_IMPL_SOFTWARE)
			{
				sts = RenderFrame(pmfxOutSurface , impl_type);
				MSDK_BREAK_ON_ERROR(sts);
			}
			else
			{
				
				 // Surface locking required when read/write video surfaces
				sts = pMfxAllocator->Lock(pMfxAllocator->pthis, pmfxOutSurface->Data.MemId, &(pmfxOutSurface->Data));
				MSDK_BREAK_ON_ERROR(sts);

				sts = RenderFrame(pmfxOutSurface , impl_type);
				MSDK_BREAK_ON_ERROR(sts);

				sts = pMfxAllocator->Unlock(pMfxAllocator->pthis, pmfxOutSurface->Data.MemId, &(pmfxOutSurface->Data));
				MSDK_BREAK_ON_ERROR(sts);
			}
			
			printf("Frame number: %d\r", nFrame);
			fflush(stdout);
		}
	}

	// MFX_ERR_MORE_DATA means that file has ended, need to go to buffering loop, exit in case of other errors
	MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	mfxGetTime(&tEnd);
	elapsed = TimeDiffMsec(tEnd, tStart) / 1000;
	double fps = ((double)nFrame / elapsed);
	printf("\nExecution time: %3.2f s (%3.2f fps)\n", elapsed, fps);

	return sts;
}

mfxStatus IntelDecoder::FlushDecoderAndRender()
{
	mfxStatus sts = MFX_ERR_NONE;
	mfxGetTime(&tStart);

	//
	// Stage 2: Retrieve the buffered decoded frames
	//
	while (MFX_ERR_NONE <= sts || MFX_ERR_MORE_SURFACE == sts) {
		if (MFX_WRN_DEVICE_BUSY == sts)
			MSDK_SLEEP(1);  // Wait if device is busy, then repeat the same call to DecodeFrameAsync

		nIndex = GetFreeSurfaceIndex(pmfxSurfaces, numSurfaces);        // Find free frame surface
		MSDK_CHECK_ERROR(MFX_ERR_NOT_FOUND, nIndex, MFX_ERR_MEMORY_ALLOC);

		// Decode a frame asychronously (returns immediately)
		sts = mfxDEC->DecodeFrameAsync(NULL, pmfxSurfaces[nIndex], &pmfxOutSurface, &syncp);

		// Ignore warnings if output is available,
		// if no output and no action required just repeat the DecodeFrameAsync call
		if (MFX_ERR_NONE < sts && syncp)
			sts = MFX_ERR_NONE;

		if (MFX_ERR_NONE == sts)
			sts = pSession->SyncOperation(syncp, 60000);      // Synchronize. Waits until decoded frame is ready

		if (MFX_ERR_NONE == sts) {
			++nFrame;
			if (impl_type == MFX_IMPL_SOFTWARE)
			{
				sts = RenderFrame(pmfxOutSurface, impl_type);
				MSDK_BREAK_ON_ERROR(sts);
			}
			else
			{
				// Surface locking required when read/write D3D surfaces
				sts = pMfxAllocator->Lock(pMfxAllocator->pthis, pmfxOutSurface->Data.MemId, &(pmfxOutSurface->Data));
				MSDK_BREAK_ON_ERROR(sts);

				sts = RenderFrame(pmfxOutSurface, impl_type);
				MSDK_BREAK_ON_ERROR(sts);

				sts = pMfxAllocator->Unlock(pMfxAllocator->pthis, pmfxOutSurface->Data.MemId, &(pmfxOutSurface->Data));
			}
			
			

			printf("Frame number: %d\r", nFrame);
			fflush(stdout);
		}
	}

	// MFX_ERR_MORE_DATA indicates that all buffers has been fetched, exit in case of other errors
	MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	mfxGetTime(&tEnd);
	elapsed += TimeDiffMsec(tEnd, tStart) / 1000;
	double fps = ((double)nFrame / elapsed);
	printf("\nExecution time: %3.2f s (%3.2f fps)\n", elapsed, fps);
	return sts;
}

void IntelDecoder::CloseResources()
{
	// ===================================================================
	// Clean up resources
	//  - It is recommended to close Media SDK components first, before releasing allocated surfaces, since
	//    some surfaces may still be locked by internal Media SDK resources.
	if(mfxDEC)
		mfxDEC->Close();
	// session closed automatically on destruction

	/*for (int i = 0; i < numSurfaces; i++)
		delete pmfxSurfaces[i];*/
	MSDK_SAFE_DELETE_ARRAY(pmfxSurfaces);
	MSDK_SAFE_DELETE_ARRAY(mfxBS.Data);

	/*if(pMfxAllocator)
		pMfxAllocator->Free(pMfxAllocator->pthis, &mfxResponse);*/

	delete pSession;
	if(fSource)
		fclose(fSource);

	Release();
}

mfxStatus IntelDecoder::SetDecodeOptions()
{
	options.impl = MFX_IMPL_AUTO_ANY;
	
	GetWindowRect(GetDesktopWindow(), &DeskBounds);
	//options.Width = DeskBounds.right - DeskBounds.left;
	//options.Height = DeskBounds.bottom - DeskBounds.top;
	/*options.Bitrate = 4000;
	options.FrameRateN = 30;
	options.FrameRateD = 1;*/
	options.MeasureLatency = true;
	strcpy_s(options.SourceName, "output.h264");
	fopen_s(&fSource, options.SourceName, "rb");
	MSDK_CHECK_POINTER(fSource, MFX_ERR_NULL_PTR);
}

void IntelDecoder::SetDecParameters()
{
	// Initialize decoder parameters
	memset(&mfxVideoParams, 0, sizeof(mfxVideoParams));
	mfxVideoParams.mfx.CodecId = MFX_CODEC_AVC;
	if (impl_type == MFX_IMPL_SOFTWARE)
	{
		mfxVideoParams.IOPattern = MFX_IOPATTERN_OUT_SYSTEM_MEMORY;
	}
	else
	{
		mfxVideoParams.IOPattern = MFX_IOPATTERN_OUT_VIDEO_MEMORY;
	}
	
}

mfxStatus IntelDecoder::QueryAndAllocRequiredSurfacesForHW()
{
	mfxStatus sts = MFX_ERR_NONE;

	// Query number of required surfaces for decoder
	mfxFrameAllocRequest Request;
	memset(&Request, 0, sizeof(Request));
	sts = mfxDEC->QueryIOSurf(&mfxVideoParams, &Request);
	MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	numSurfaces = Request.NumFrameSuggested;

	Request.Type |= WILL_READ; // This line is only required for Windows DirectX11 to ensure that surfaces can be retrieved by the application

	 // Allocate surfaces for decoder
	//mfxFrameAllocResponse mfxResponse;
	sts = pMfxAllocator->Alloc(pMfxAllocator->pthis, &Request, &mfxResponse);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	// Allocate surface headers (mfxFrameSurface1) for decoder
	pmfxSurfaces = new mfxFrameSurface1 *[numSurfaces];
	MSDK_CHECK_POINTER(pmfxSurfaces, MFX_ERR_MEMORY_ALLOC);
	for (int i = 0; i < numSurfaces; i++) {
		pmfxSurfaces[i] = new mfxFrameSurface1;
		memset(pmfxSurfaces[i], 0, sizeof(mfxFrameSurface1));
		memcpy(&(pmfxSurfaces[i]->Info), &(mfxVideoParams.mfx.FrameInfo), sizeof(mfxFrameInfo));
		pmfxSurfaces[i]->Data.MemId = mfxResponse.mids[i];      // MID (memory id) represents one video NV12 surface
	}
	return sts;
}

mfxStatus IntelDecoder::QueryAndAllocRequiredSurfacesForSW()
{
	mfxStatus sts = MFX_ERR_NONE;

	// Query number of required surfaces for decoder
	mfxFrameAllocRequest DecRequest;
	memset(&DecRequest, 0, sizeof(DecRequest));
	sts = mfxDEC->QueryIOSurf(&mfxVideoParams, &DecRequest);
	MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	numSurfaces = DecRequest.NumFrameSuggested;

	//VPPRequest[0].Type |= WILL_WRITE; // This line is only required for Windows DirectX11 to ensure that surfaces can be written to by the application
	//DecRequest.Type |= WILL_READ; // This line is only required for Windows DirectX11 to ensure that surfaces can be retrieved by the application

	// Allocate surfaces for decoder
	// - Width and height of buffer must be aligned, a multiple of 32
	// - Frame surface array keeps pointers all surface planes and general frame info

	mfxU16 width = (mfxU16)MSDK_ALIGN(DecRequest.Info.Width);
	mfxU16 height = (mfxU16)MSDK_ALIGN16(DecRequest.Info.Height);
	mfxU8 bitsPerPixel = 12;        // NV12 format is a 12 bits per pixel format
	mfxU32 surfaceSize = width * height * bitsPerPixel / 8;
	mfxU8* surfaceBuffers = (mfxU8*) new mfxU8[surfaceSize * numSurfaces];

	// Allocate surface headers (mfxFrameSurface1) for decoder
	pmfxSurfaces = new mfxFrameSurface1 *[numSurfaces];
	MSDK_CHECK_POINTER(pmfxSurfaces, MFX_ERR_MEMORY_ALLOC);
	for (int i = 0; i < numSurfaces; i++) {
		pmfxSurfaces[i] = new mfxFrameSurface1;
		memset(pmfxSurfaces[i], 0, sizeof(mfxFrameSurface1));
		memcpy(&(pmfxSurfaces[i]->Info), &(mfxVideoParams.mfx.FrameInfo), sizeof(mfxFrameInfo));
		pmfxSurfaces[i]->Data.Y = &surfaceBuffers[surfaceSize * i];
		pmfxSurfaces[i]->Data.U = pmfxSurfaces[i]->Data.Y + width * height;
		pmfxSurfaces[i]->Data.V = pmfxSurfaces[i]->Data.U + 1;
		pmfxSurfaces[i]->Data.Pitch = width;
	}
	
	return sts;
}
