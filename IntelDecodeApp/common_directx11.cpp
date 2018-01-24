/*****************************************************************************

INTEL CORPORATION PROPRIETARY INFORMATION
This software is supplied under the terms of a license agreement or
nondisclosure agreement with Intel Corporation and may not be copied
or disclosed except in accordance with the terms of that agreement.
Copyright(c) 2005-2014 Intel Corporation. All Rights Reserved.

*****************************************************************************/

#include "common_directx11.h"
#include "../Debug/PixelShader.h"
#include "../Debug/VertexShader.h"
#include "DirectXMath.h"
#include<map>
#include<array>

CComPtr<ID3D11Device>                   g_pD3D11Device;
CComPtr<ID3D11DeviceContext>            g_pD3D11Ctx;
CComPtr<IDXGIFactory2>                  g_pDXGIFactory;
CComPtr<IDXGISwapChain1>                g_pDXGISwapChain;
CComPtr<ID3D11Texture2D>				g_texture;
CComPtr<ID3D11Texture2D>				m_AccessibleSurf;
CComPtr<ID3D11ShaderResourceView>		g_luminanceView;
CComPtr<ID3D11ShaderResourceView>		g_chrominanceView;
ID3D11RenderTargetView*					g_RTV;
ID3D11SamplerState*						g_SamplerLinear;

CComPtr<ID3D11VertexShader>				g_VertexShader;
CComPtr<ID3D11PixelShader>				g_PixelShader;

UINT OutputCount;

//
// A vertex with a position and texture coordinate
//
typedef struct _VERTEX
{
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT2 TexCoord;
} VERTEX;

float x_coord = 1.0f;
float y_coord = 1.0f;

CComPtr<ID3D11InputLayout>				g_InputLayout;
IDXGIAdapter*                           g_pAdapter;

std::map<mfxMemId*, mfxHDL>             allocResponses;
std::map<mfxHDL, mfxFrameAllocResponse> allocDecodeResponses;
std::map<mfxHDL, int>                   allocDecodeRefCount;

typedef struct {
    mfxMemId    memId;
    mfxMemId    memIdStage;
    mfxU16      rw;
} CustomMemId;

const struct {
    mfxIMPL impl;       // actual implementation
    mfxU32  adapterID;  // device adapter number
} implTypes[] = {
    {MFX_IMPL_HARDWARE, 0},
    {MFX_IMPL_HARDWARE2, 1},
    {MFX_IMPL_HARDWARE3, 2},
    {MFX_IMPL_HARDWARE4, 3}
};

// =================================================================
// DirectX functionality required to manage DX11 device and surfaces
//

IDXGIAdapter* GetIntelDeviceAdapterHandle(mfxSession session)
{
    mfxU32  adapterNum = 0;
    mfxIMPL impl;

    MFXQueryIMPL(session, &impl);

    mfxIMPL baseImpl = MFX_IMPL_BASETYPE(impl); // Extract Media SDK base implementation type

    // get corresponding adapter number
    for (mfxU8 i = 0; i < sizeof(implTypes)/sizeof(implTypes[0]); i++) {
        if (implTypes[i].impl == baseImpl) {
            adapterNum = implTypes[i].adapterID;
            break;
        }
    }

    HRESULT hres = CreateDXGIFactory(__uuidof(IDXGIFactory2), (void**)(&g_pDXGIFactory) );
    if (FAILED(hres)) return NULL;

    IDXGIAdapter* adapter;
    hres = g_pDXGIFactory->EnumAdapters(adapterNum, &adapter);
    if (FAILED(hres)) return NULL;

    return adapter;
}

// Create HW device context
mfxStatus CreateHWDevice(mfxSession session, mfxHDL* deviceHandle, HWND hWnd, bool bCreateSharedHandles)
{
    //Note: not using bCreateSharedHandles for DX11 -- for API consistency only
    hWnd; // Window handle not required by DX11 since we do not showcase rendering.

    HRESULT hres = S_OK;

    static D3D_FEATURE_LEVEL FeatureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
		D3D_FEATURE_LEVEL_9_1
    };
    D3D_FEATURE_LEVEL pFeatureLevelsOut;

    g_pAdapter = GetIntelDeviceAdapterHandle(session);
    if (NULL == g_pAdapter)
        return MFX_ERR_DEVICE_FAILED;

    UINT dxFlags = 0;
    //UINT dxFlags = D3D11_CREATE_DEVICE_DEBUG;


    hres =  D3D11CreateDevice(  g_pAdapter,
                                D3D_DRIVER_TYPE_UNKNOWN,
                                NULL,
                                dxFlags,
                                FeatureLevels,
                                (sizeof(FeatureLevels) / sizeof(FeatureLevels[0])),
                                D3D11_SDK_VERSION,
                                &g_pD3D11Device,
                                &pFeatureLevelsOut,
                                &g_pD3D11Ctx);
    if (FAILED(hres))
        return MFX_ERR_DEVICE_FAILED;

    // turn on multithreading for the DX11 context
    CComQIPtr<ID3D10Multithread> p_mt(g_pD3D11Ctx);
    if (p_mt)
        p_mt->SetMultithreadProtected(true);
    else
        return MFX_ERR_DEVICE_FAILED;

    *deviceHandle = (mfxHDL)g_pD3D11Device;

    return MFX_ERR_NONE;
}


void SetHWDeviceContext(CComPtr<ID3D11DeviceContext> devCtx)
{
    g_pD3D11Ctx = devCtx;
    devCtx->GetDevice(&g_pD3D11Device);
}

// Free HW device context
void CleanupHWDevice()
{
	if (g_pAdapter)
    g_pAdapter->Release();
	if(g_RTV)
		g_RTV->Release();
	if (g_SamplerLinear)
		g_SamplerLinear->Release();
}

CComPtr<ID3D11DeviceContext> GetHWDeviceContext()
{
    return g_pD3D11Ctx;
}

void ClearYUVSurfaceD3D(mfxMemId memId)
{
    // TBD
}

void ClearRGBSurfaceD3D(mfxMemId memId)
{
    // TBD
}

mfxStatus CreateSwapChain(HWND hWnd, int Width, int Height)
{
	mfxStatus sts = MFX_ERR_NONE;

	// Create swapchain for window
	DXGI_SWAP_CHAIN_DESC1 SwapChainDesc;
	RtlZeroMemory(&SwapChainDesc, sizeof(SwapChainDesc));

	SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	SwapChainDesc.BufferCount = 2;
	SwapChainDesc.Width = Width;
	SwapChainDesc.Height = Height;
	SwapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	SwapChainDesc.SampleDesc.Count = 1;
	SwapChainDesc.SampleDesc.Quality = 0;
	HRESULT hr = g_pDXGIFactory->CreateSwapChainForHwnd(g_pD3D11Device, hWnd, &SwapChainDesc, nullptr, nullptr, &g_pDXGISwapChain);
	if (FAILED(hr))
	{
		return MFX_ERR_DEVICE_FAILED;
	}

	// Disable the ALT-ENTER shortcut for entering full-screen mode
	hr = g_pDXGIFactory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);
	if (FAILED(hr))
	{
		return MFX_ERR_DEVICE_FAILED;
	}

	return sts;
}

//Create Accessible Surface

mfxStatus CreateAccessibleSurf(int Width, int Height)
{
	mfxStatus sts = MFX_ERR_NONE;
	HRESULT hr;

	D3D11_TEXTURE2D_DESC const texDesc = CD3D11_TEXTURE2D_DESC(
		DXGI_FORMAT_NV12,           // HoloLens PV camera format, common for video sources
		MSDK_ALIGN(Width),						// Width of the video frames
		MSDK_ALIGN16(Height),						// Height of the video frames
		1,                          // Number of textures in the array
		1,                          // Number of miplevels in each texture
		0/*D3D11_BIND_SHADER_RESOURCE*/, // We read from this texture in the shader
		D3D11_USAGE_DEFAULT,        // Because we'll be copying from CPU memory
		D3D11_CPU_ACCESS_WRITE /*D3D11_CPU_ACCESS_READ */   // We only need to write into the texture
	);


	hr = g_pD3D11Device->CreateTexture2D(&texDesc, nullptr, &m_AccessibleSurf);
	if (FAILED(hr))
	{
		return MFX_ERR_DEVICE_FAILED;
	}
	return sts;
}

//
// Recreate shared texture
//
mfxStatus CreateSharedSurf(int Width, int Height)
{
	mfxStatus sts = MFX_ERR_NONE;
	HRESULT hr;

	IDXGIOutput* DxgiOutput = nullptr;

	// Figure out right dimensions for full size desktop texture and # of outputs to duplicate

	hr = S_OK;
	for (OutputCount = 0; SUCCEEDED(hr); ++OutputCount)
	{
		if (DxgiOutput)
		{
			DxgiOutput->Release();
			DxgiOutput = nullptr;
		}
		hr = g_pAdapter->EnumOutputs(OutputCount, &DxgiOutput);
		if (DxgiOutput && (hr != DXGI_ERROR_NOT_FOUND))
		{
			/*DXGI_OUTPUT_DESC DesktopDesc;
			DxgiOutput->GetDesc(&DesktopDesc);

			DeskBounds->left = min(DesktopDesc.DesktopCoordinates.left, DeskBounds->left);
			DeskBounds->top = min(DesktopDesc.DesktopCoordinates.top, DeskBounds->top);
			DeskBounds->right = max(DesktopDesc.DesktopCoordinates.right, DeskBounds->right);
			DeskBounds->bottom = max(DesktopDesc.DesktopCoordinates.bottom, DeskBounds->bottom);*/
		}
	}

	--OutputCount;

	D3D11_TEXTURE2D_DESC const texDesc = CD3D11_TEXTURE2D_DESC(
		DXGI_FORMAT_NV12,           // HoloLens PV camera format, common for video sources
		MSDK_ALIGN(Width),						// Width of the video frames
		MSDK_ALIGN16(Height),						// Height of the video frames
		1,                          // Number of textures in the array
		1,                          // Number of miplevels in each texture
		D3D11_BIND_SHADER_RESOURCE, // We read from this texture in the shader
		D3D11_USAGE_DYNAMIC,        // Because we'll be copying from CPU memory
		D3D11_CPU_ACCESS_WRITE/*D3D11_CPU_ACCESS_READ */   // We only need to write into the texture
	);


	hr = g_pD3D11Device->CreateTexture2D(&texDesc, nullptr, &g_texture);
	if (FAILED(hr))
	{
		return MFX_ERR_DEVICE_FAILED;
	}

	// https://msdn.microsoft.com/en-us/library/windows/desktop/bb173059(v=vs.85).aspx
	// To access DXGI_FORMAT_NV12 in the shader, we need to map the luminance channel and the chrominance channels
	// into a format that shaders can understand.
	// In the case of NV12, DirectX understands how the texture is laid out, so we can create these
	// shader resource views which represent the two channels of the NV12 texture.
	// Then inside the shader we convert YUV into RGB so we can render.

	// DirectX specifies the view format to be DXGI_FORMAT_R8_UNORM for NV12 luminance channel.
	// Luminance is 8 bits per pixel. DirectX will handle converting 8-bit integers into normalized
	// floats for use in the shader.
	D3D11_SHADER_RESOURCE_VIEW_DESC const luminancePlaneDesc = CD3D11_SHADER_RESOURCE_VIEW_DESC(
		g_texture,
		D3D11_SRV_DIMENSION_TEXTURE2D,
		DXGI_FORMAT_R8_UNORM
	);

	hr = g_pD3D11Device->CreateShaderResourceView(
		g_texture,
		&luminancePlaneDesc,
		&g_luminanceView
	);
	if (FAILED(hr))
	{
		return MFX_ERR_DEVICE_FAILED;
	}

	// DirectX specifies the view format to be DXGI_FORMAT_R8G8_UNORM for NV12 chrominance channel.
	// Chrominance has 4 bits for U and 4 bits for V per pixel. DirectX will handle converting 4-bit
	// integers into normalized floats for use in the shader.
	D3D11_SHADER_RESOURCE_VIEW_DESC const chrominancePlaneDesc = CD3D11_SHADER_RESOURCE_VIEW_DESC(
		g_texture,
		D3D11_SRV_DIMENSION_TEXTURE2D,
		DXGI_FORMAT_R8G8_UNORM
	);

	hr = g_pD3D11Device->CreateShaderResourceView(
		g_texture,
		&chrominancePlaneDesc,
		&g_chrominanceView
	);
	if (FAILED(hr))
	{
		return MFX_ERR_DEVICE_FAILED;
	}

	return sts;
}

//
// Reset render target view
//
mfxStatus MakeRTV()
{
	mfxStatus sts = MFX_ERR_NONE;
	// Get backbuffer
	ID3D11Texture2D* BackBuffer = nullptr;
	HRESULT hr = g_pDXGISwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&BackBuffer));
	if (FAILED(hr))
	{
		return MFX_ERR_DEVICE_FAILED;
	}

	// Create a render target view
	hr = g_pD3D11Device->CreateRenderTargetView(BackBuffer, nullptr, &g_RTV);
	BackBuffer->Release();
	if (FAILED(hr))
	{
		return MFX_ERR_DEVICE_FAILED;
	}

	// Set new render target
	g_pD3D11Ctx->OMSetRenderTargets(OutputCount, &g_RTV, nullptr);

	return sts;
}

//
// Set new viewport
//
void SetViewPort(UINT Width, UINT Height)
{
	D3D11_VIEWPORT VP;
	VP.Width = static_cast<FLOAT>(Width);
	VP.Height = static_cast<FLOAT>(Height);
	VP.MinDepth = 0.0f;
	VP.MaxDepth = 1.0f;
	VP.TopLeftX = 0;
	VP.TopLeftY = 0;
	g_pD3D11Ctx->RSSetViewports(1, &VP);
}

mfxStatus CreateSamplerState()
{
	D3D11_SAMPLER_DESC desc = CD3D11_SAMPLER_DESC(CD3D11_DEFAULT());


	HRESULT hr = g_pD3D11Device->CreateSamplerState(&desc, &g_SamplerLinear);
	if (FAILED(hr))
	{
		return MFX_ERR_DEVICE_FAILED;
	}
	return MFX_ERR_NONE;
}

//
// Initialize shaders for drawing to screen
//
mfxStatus InitShaders()
{
	mfxStatus sts = MFX_ERR_NONE;
	HRESULT hr;

	UINT Size = ARRAYSIZE(g_VS);
	hr = g_pD3D11Device->CreateVertexShader(g_VS, Size, nullptr, &g_VertexShader);
	if (FAILED(hr))
	{
		return MFX_ERR_DEVICE_FAILED;
	}


	constexpr std::array<D3D11_INPUT_ELEMENT_DESC, 2> Layout =
	{ {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	} };

	hr = g_pD3D11Device->CreateInputLayout(Layout.data(), Layout.size(), g_VS, Size, &g_InputLayout);
	if (FAILED(hr))
	{
		return MFX_ERR_DEVICE_FAILED;
	}
	g_pD3D11Ctx->IASetInputLayout(g_InputLayout);

	Size = ARRAYSIZE(g_PS);
	hr = g_pD3D11Device->CreatePixelShader(g_PS, Size, nullptr, &g_PixelShader);
	if (FAILED(hr))
	{
		return MFX_ERR_DEVICE_FAILED;
	}

	return sts;
}

mfxStatus Render(mfxFrameSurface1* pSurface , mfxIMPL impl_type)
{
	mfxStatus sts = MFX_ERR_NONE;

	x_coord = pSurface->Info.CropW / (float)pSurface->Info.Width;
	y_coord = pSurface->Info.CropH / (float)pSurface->Info.Height;

	if (impl_type == MFX_IMPL_SOFTWARE)
	{
		mfxFrameInfo* pInfo = &pSurface->Info;
		mfxFrameData* pData = &pSurface->Data;
		mfxU32 h, w;

		int width = pInfo->CropW;
		int height = pInfo->CropH;

		// Copy from CPU access texture to bitmap buffer
		D3D11_MAPPED_SUBRESOURCE resource;
		UINT subresource = D3D11CalcSubresource(0, 0, 0);
		g_pD3D11Ctx->Map(g_texture, subresource, D3D11_MAP_WRITE_DISCARD, 0, &resource);

		BYTE* dptr = reinterpret_cast<BYTE*>(resource.pData);

		for (int i = 0; i < pInfo->CropH; i++)
		{
			//mfxU8* y = pData->Y + (pInfo->CropY * pData->Pitch + pInfo->CropX) + i * pData->Pitch;
			memcpy(dptr + resource.RowPitch * i, pData->Y + pData->Pitch * i, pData->Pitch);
		}

		for (int i = 0; i < pInfo->CropH / 2; i++)
		{
			memcpy(dptr + resource.RowPitch * pInfo->CropH + resource.RowPitch * i, pData->UV + pData->Pitch * i, resource.RowPitch);
		}

		x_coord = pSurface->Info.CropW / (float)resource.RowPitch;

		g_pD3D11Ctx->Unmap(g_texture, subresource);
	}

	else
	{
		CustomMemId* srcTexture = (CustomMemId*)(pSurface->Data.MemId);
		g_pD3D11Ctx->CopyResource(g_texture, (ID3D11Texture2D*)(srcTexture->memId));
	}
	//Sleep(20);
	sts = DrawFrame();
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
	return sts;
}

//
// Draw frame into backbuffer
//
mfxStatus DrawFrame()
{
	mfxStatus sts = MFX_ERR_NONE;
	HRESULT hr;

	// Vertices for drawing whole texture
	VERTEX Vertices[6] =
	{
		{ DirectX::XMFLOAT3(-1.0f, -1.0f, 0), DirectX::XMFLOAT2(0.0f, y_coord) },
		{ DirectX::XMFLOAT3(-1.0f, 1.0f, 0), DirectX::XMFLOAT2(0.0f, 0.0f) },
		{ DirectX::XMFLOAT3(1.0f, -1.0f, 0), DirectX::XMFLOAT2(x_coord, y_coord) },
		{ DirectX::XMFLOAT3(1.0f, -1.0f, 0), DirectX::XMFLOAT2(x_coord, y_coord) },
		{ DirectX::XMFLOAT3(-1.0f, 1.0f, 0), DirectX::XMFLOAT2(0.0f, 0.0f) },
		{ DirectX::XMFLOAT3(1.0f, 1.0f, 0), DirectX::XMFLOAT2(x_coord, 0.0f) },
	};




	// Rendering NV12 requires two resource views, which represent the luminance and chrominance channels of the YUV formatted texture.
	std::array<ID3D11ShaderResourceView*, 2> const textureViews = {
		g_luminanceView,
		g_chrominanceView
	};

	// Bind the NV12 channels to the shader.
	g_pD3D11Ctx->PSSetShaderResources(
		0,
		textureViews.size(),
		textureViews.data()
	);

	// Set resources
	UINT Stride = sizeof(VERTEX);
	UINT Offset = 0;
	FLOAT blendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
	g_pD3D11Ctx->OMSetBlendState(nullptr, blendFactor, 0xffffffff);
	g_pD3D11Ctx->OMSetRenderTargets(1, &g_RTV, nullptr);
	g_pD3D11Ctx->VSSetShader(g_VertexShader, nullptr, 0);
	g_pD3D11Ctx->PSSetShader(g_PixelShader, nullptr, 0);
	g_pD3D11Ctx->PSSetSamplers(0, 1, &g_SamplerLinear);
	g_pD3D11Ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	D3D11_BUFFER_DESC BufferDesc;
	RtlZeroMemory(&BufferDesc, sizeof(BufferDesc));
	BufferDesc.Usage = D3D11_USAGE_DEFAULT;
	BufferDesc.ByteWidth = sizeof(VERTEX) * 6;
	BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	BufferDesc.CPUAccessFlags = 0;
	D3D11_SUBRESOURCE_DATA InitData;
	RtlZeroMemory(&InitData, sizeof(InitData));
	InitData.pSysMem = Vertices;

	ID3D11Buffer* VertexBuffer = nullptr;

	// Create vertex buffer
	hr = g_pD3D11Device->CreateBuffer(&BufferDesc, &InitData, &VertexBuffer);
	if (FAILED(hr))
	{

		return MFX_ERR_DEVICE_FAILED;
	}

	g_pD3D11Ctx->IASetVertexBuffers(0, 1, &VertexBuffer, &Stride, &Offset);

	// Draw textured quad onto render target
	g_pD3D11Ctx->Draw(6, 0);

	VertexBuffer->Release();
	VertexBuffer = nullptr;

	hr = g_pDXGISwapChain->Present(1, 0);
	if (FAILED(hr))
	{
		return MFX_ERR_DEVICE_FAILED;
	}

	return sts;
}

//
// Intel Media SDK memory allocator entrypoints....
//
mfxStatus _simple_alloc(mfxFrameAllocRequest* request, mfxFrameAllocResponse* response)
{
    HRESULT hRes;

    // Determine surface format
    DXGI_FORMAT format;
    if (MFX_FOURCC_NV12 == request->Info.FourCC)
        format = DXGI_FORMAT_NV12;
    else if (MFX_FOURCC_RGB4 == request->Info.FourCC)
        format = DXGI_FORMAT_B8G8R8A8_UNORM;
    else if (MFX_FOURCC_YUY2== request->Info.FourCC)
        format = DXGI_FORMAT_YUY2;
    else if (MFX_FOURCC_P8 == request->Info.FourCC ) //|| MFX_FOURCC_P8_TEXTURE == request->Info.FourCC
        format = DXGI_FORMAT_P8;
    else
        format = DXGI_FORMAT_UNKNOWN;

    if (DXGI_FORMAT_UNKNOWN == format)
        return MFX_ERR_UNSUPPORTED;


    // Allocate custom container to keep texture and stage buffers for each surface
    // Container also stores the intended read and/or write operation.
    CustomMemId** mids = (CustomMemId**)calloc(request->NumFrameSuggested, sizeof(CustomMemId*));
    if (!mids) return MFX_ERR_MEMORY_ALLOC;

    for (int i=0; i<request->NumFrameSuggested; i++) {
        mids[i] = (CustomMemId*)calloc(1, sizeof(CustomMemId));
        if (!mids[i]) {
            return MFX_ERR_MEMORY_ALLOC;
        }
        mids[i]->rw = request->Type & 0xF000; // Set intended read/write operation
    }

    request->Type = request->Type & 0x0FFF;

    // because P8 data (bitstream) for h264 encoder should be allocated by CreateBuffer()
    // but P8 data (MBData) for MPEG2 encoder should be allocated by CreateTexture2D()
    if (request->Info.FourCC == MFX_FOURCC_P8) {
        D3D11_BUFFER_DESC desc = { 0 };

        if (!request->NumFrameSuggested) return MFX_ERR_MEMORY_ALLOC;

        desc.ByteWidth           = request->Info.Width * request->Info.Height;
        desc.Usage               = D3D11_USAGE_STAGING;
        desc.BindFlags           = 0;
        desc.CPUAccessFlags      = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags           = 0;
        desc.StructureByteStride = 0;

        ID3D11Buffer* buffer = 0;
        hRes = g_pD3D11Device->CreateBuffer(&desc, 0, &buffer);
        if (FAILED(hRes))
            return MFX_ERR_MEMORY_ALLOC;

        mids[0]->memId = reinterpret_cast<ID3D11Texture2D*>(buffer);
    } else {
        D3D11_TEXTURE2D_DESC desc = {0};

        desc.Width              = request->Info.Width;
        desc.Height             = request->Info.Height;
        desc.MipLevels          = 1;
        desc.ArraySize          = 1; // number of subresources is 1 in this case
        desc.Format             = format;
        desc.SampleDesc.Count   = 1;
        desc.Usage              = D3D11_USAGE_DEFAULT;
        desc.BindFlags          = D3D11_BIND_DECODER;
        desc.MiscFlags          = 0;
        //desc.MiscFlags            = D3D11_RESOURCE_MISC_SHARED;

        if ( (MFX_MEMTYPE_FROM_VPPIN & request->Type) &&
             (DXGI_FORMAT_B8G8R8A8_UNORM == desc.Format) ) {
            desc.BindFlags = D3D11_BIND_RENDER_TARGET;
            if (desc.ArraySize > 2)
                return MFX_ERR_MEMORY_ALLOC;
        }

        if ( (MFX_MEMTYPE_FROM_VPPOUT & request->Type) ||
             (MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET & request->Type)) {
            desc.BindFlags = D3D11_BIND_RENDER_TARGET;
            if (desc.ArraySize > 2)
                return MFX_ERR_MEMORY_ALLOC;
        }

        if ( DXGI_FORMAT_P8 == desc.Format )
            desc.BindFlags = 0;

        ID3D11Texture2D* pTexture2D;

        // Create surface textures
        for (size_t i = 0; i < request->NumFrameSuggested / desc.ArraySize; i++) {
            hRes = g_pD3D11Device->CreateTexture2D(&desc, NULL, &pTexture2D);

            if (FAILED(hRes))
                return MFX_ERR_MEMORY_ALLOC;

            mids[i]->memId = pTexture2D;
        }

        desc.ArraySize      = 1;
        desc.Usage          = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;// | D3D11_CPU_ACCESS_WRITE;
        desc.BindFlags      = 0;
        desc.MiscFlags      = 0;
        //desc.MiscFlags        = D3D11_RESOURCE_MISC_SHARED;

        // Create surface staging textures
        for (size_t i = 0; i < request->NumFrameSuggested; i++) {
            hRes = g_pD3D11Device->CreateTexture2D(&desc, NULL, &pTexture2D);

            if (FAILED(hRes))
                return MFX_ERR_MEMORY_ALLOC;

            mids[i]->memIdStage = pTexture2D;
        }
    }


    response->mids = (mfxMemId*)mids;
    response->NumFrameActual = request->NumFrameSuggested;

    return MFX_ERR_NONE;
}

mfxStatus simple_alloc(mfxHDL pthis, mfxFrameAllocRequest* request, mfxFrameAllocResponse* response)
{
    mfxStatus sts = MFX_ERR_NONE;

    if (request->Type & MFX_MEMTYPE_SYSTEM_MEMORY)
        return MFX_ERR_UNSUPPORTED;

    if (allocDecodeResponses.find(pthis) != allocDecodeResponses.end() &&
        MFX_MEMTYPE_EXTERNAL_FRAME & request->Type &&
        MFX_MEMTYPE_FROM_DECODE & request->Type) {
        // Memory for this request was already allocated during manual allocation stage. Return saved response
        //   When decode acceleration device (DXVA) is created it requires a list of d3d surfaces to be passed.
        //   Therefore Media SDK will ask for the surface info/mids again at Init() stage, thus requiring us to return the saved response
        //   (No such restriction applies to Encode or VPP)
        *response = allocDecodeResponses[pthis];
        allocDecodeRefCount[pthis]++;
    } else {
        sts = _simple_alloc(request, response);

        if (MFX_ERR_NONE == sts) {
            if ( MFX_MEMTYPE_EXTERNAL_FRAME & request->Type &&
                 MFX_MEMTYPE_FROM_DECODE & request->Type) {
                // Decode alloc response handling
                allocDecodeResponses[pthis] = *response;
                allocDecodeRefCount[pthis]++;
            } else {
                // Encode and VPP alloc response handling
                allocResponses[response->mids] = pthis;
            }
        }
    }

    return sts;
}

mfxStatus simple_lock(mfxHDL pthis, mfxMemId mid, mfxFrameData* ptr)
{
    pthis; // To suppress warning for this unused parameter

    HRESULT hRes = S_OK;

    D3D11_TEXTURE2D_DESC        desc = {0};
    D3D11_MAPPED_SUBRESOURCE    lockedRect = {0};

    CustomMemId*        memId       = (CustomMemId*)mid;
    ID3D11Texture2D*    pSurface    = (ID3D11Texture2D*)memId->memId;
    ID3D11Texture2D*    pStage      = (ID3D11Texture2D*)memId->memIdStage;

    D3D11_MAP   mapType  = D3D11_MAP_READ;
    UINT        mapFlags = D3D11_MAP_FLAG_DO_NOT_WAIT;

    if (NULL == pStage) {
        hRes = g_pD3D11Ctx->Map(pSurface, 0, mapType, mapFlags, &lockedRect);
        desc.Format = DXGI_FORMAT_P8;
    } else {
        pSurface->GetDesc(&desc);

        // copy data only in case of user wants o read from stored surface
        if (memId->rw & WILL_READ)
            g_pD3D11Ctx->CopySubresourceRegion(pStage, 0, 0, 0, 0, pSurface, 0, NULL);

        do {
            hRes = g_pD3D11Ctx->Map(pStage, 0, mapType, mapFlags, &lockedRect);
            if (S_OK != hRes && DXGI_ERROR_WAS_STILL_DRAWING != hRes)
                return MFX_ERR_LOCK_MEMORY;
        } while (DXGI_ERROR_WAS_STILL_DRAWING == hRes);
    }

    if (FAILED(hRes))
        return MFX_ERR_LOCK_MEMORY;

    switch (desc.Format) {
    case DXGI_FORMAT_NV12:
        ptr->Pitch = (mfxU16)lockedRect.RowPitch;
        ptr->Y = (mfxU8*)lockedRect.pData;
        ptr->U = (mfxU8*)lockedRect.pData + desc.Height * lockedRect.RowPitch;
        ptr->V = ptr->U + 1;
        break;
    case DXGI_FORMAT_B8G8R8A8_UNORM :
        ptr->Pitch = (mfxU16)lockedRect.RowPitch;
        ptr->B = (mfxU8*)lockedRect.pData;
        ptr->G = ptr->B + 1;
        ptr->R = ptr->B + 2;
        ptr->A = ptr->B + 3;
        break;
    case DXGI_FORMAT_YUY2:
        ptr->Pitch = (mfxU16)lockedRect.RowPitch;
        ptr->Y = (mfxU8*)lockedRect.pData;
        ptr->U = ptr->Y + 1;
        ptr->V = ptr->Y + 3;
        break;
    case DXGI_FORMAT_P8 :
        ptr->Pitch = (mfxU16)lockedRect.RowPitch;
        ptr->Y = (mfxU8*)lockedRect.pData;
        ptr->U = 0;
        ptr->V = 0;
        break;
    default:
        return MFX_ERR_LOCK_MEMORY;
    }

    return MFX_ERR_NONE;
}

mfxStatus simple_unlock(mfxHDL pthis, mfxMemId mid, mfxFrameData* ptr)
{
    pthis; // To suppress warning for this unused parameter

    CustomMemId*        memId       = (CustomMemId*)mid;
    ID3D11Texture2D*    pSurface    = (ID3D11Texture2D*)memId->memId;
    ID3D11Texture2D*    pStage      = (ID3D11Texture2D*)memId->memIdStage;

    if (NULL == pStage) {
        g_pD3D11Ctx->Unmap(pSurface, 0);
    } else {
        g_pD3D11Ctx->Unmap(pStage, 0);
        // copy data only in case of user wants to write to stored surface
        if (memId->rw & WILL_WRITE)
            g_pD3D11Ctx->CopySubresourceRegion(pSurface, 0, 0, 0, 0, pStage, 0, NULL);
    }

    if (ptr) {
        ptr->Pitch=0;
        ptr->U=ptr->V=ptr->Y=0;
        ptr->A=ptr->R=ptr->G=ptr->B=0;
    }
   
    return MFX_ERR_NONE;
}

mfxStatus simple_gethdl(mfxHDL pthis, mfxMemId mid, mfxHDL* handle)
{
    pthis; // To suppress warning for this unused parameter

    if (NULL == handle)
        return MFX_ERR_INVALID_HANDLE;

    mfxHDLPair*     pPair = (mfxHDLPair*)handle;
    CustomMemId*    memId = (CustomMemId*)mid;

    pPair->first  = memId->memId; // surface texture
    pPair->second = 0;

    return MFX_ERR_NONE;
}


mfxStatus _simple_free(mfxFrameAllocResponse* response)
{
    if (response->mids) {
        for (mfxU32 i = 0; i < response->NumFrameActual; i++) {
            if (response->mids[i]) {
                CustomMemId*        mid         = (CustomMemId*)response->mids[i];
                ID3D11Texture2D*    pSurface    = (ID3D11Texture2D*)mid->memId;
                ID3D11Texture2D*    pStage      = (ID3D11Texture2D*)mid->memIdStage;

                if (pSurface)
                    pSurface->Release();
                if (pStage)
                    pStage->Release();

                free(mid);
            }
        }
        free(response->mids);
        response->mids = NULL;
    }

    return MFX_ERR_NONE;
}

mfxStatus simple_free(mfxHDL pthis, mfxFrameAllocResponse* response)
{
    if (NULL == response)
        return MFX_ERR_NULL_PTR;

    if (allocResponses.find(response->mids) == allocResponses.end()) {
        // Decode free response handling
        if (--allocDecodeRefCount[pthis] == 0) {
            _simple_free(response);
            allocDecodeResponses.erase(pthis);
            allocDecodeRefCount.erase(pthis);
        }
    } else {
        // Encode and VPP free response handling
        allocResponses.erase(response->mids);
        _simple_free(response);
    }

    return MFX_ERR_NONE;
}
