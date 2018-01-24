#ifndef _INTELDECODER_H_
#define _INTELDECODER_H_

#include "common_utils.h"
//#include "OutputManager.h"

#define MSDK_MAX_PATH 280

struct DecodeOptions {
	mfxIMPL impl; // OPTION_IMPL

	char SourceName[MSDK_MAX_PATH]; // OPTION_FSOURCE

	mfxU16 Width; // OPTION_GEOMETRY
	mfxU16 Height;

	mfxU16 Bitrate; // OPTION_BITRATE

	mfxU16 FrameRateN; // OPTION_FRAMERATE
	mfxU16 FrameRateD;

	bool MeasureLatency; // OPTION_MEASURE_LATENCY
};

class IntelDecoder
{
private:
	//Vars
	//OUTPUTMANAGER OutMgr;
	RECT DeskBounds;
	RECT WindRect;
	DecodeOptions options;
	MFXVideoDECODE *mfxDEC;
	MFXVideoSession *pSession;
	mfxFrameAllocator *pMfxAllocator;
	mfxVideoParam mfxVideoParams;
	mfxFrameSurface1** pmfxSurfaces;
	mfxFrameSurface1* pmfxOutSurface;
	mfxFrameSurface1* pmfxOutSurface_sw;
	BYTE* pmfxOutCPUSurface_sw;
	mfxU16 numSurfaces;
	mfxFrameAllocResponse mfxResponse;
	int nIndex;
	int nIndex2;
	mfxBitstream mfxBS;
	mfxTime tStart, tEnd;
	double elapsed;
	mfxSyncPoint syncp;
	mfxU32 nFrame;
	mfxFrameAllocRequest VPPRequest[2];     // [0] - in, [1] - out
	mfxExtVPPDoNotUse extDoNotUse;
	mfxExtBuffer* extBuffers[1];

	//Methods
	mfxStatus SetDecodeOptions();
	void SetDecParameters();
	mfxStatus QueryAndAllocRequiredSurfacesForSW();
	mfxStatus QueryAndAllocRequiredSurfacesForHW();

public:
	//Vars
	FILE* fSource;
	mfxIMPL impl_type;
	/*MFXVideoDECODE *mfxDEC;
	MFXVideoVPP *mfxVPP;*/

	//Methods
	IntelDecoder();
	~IntelDecoder();
	mfxStatus InitializeX(HWND hWnd);
	mfxStatus RunDecodeAndRender();
	mfxStatus FlushDecoderAndRender();
	void CloseResources();
};

#endif