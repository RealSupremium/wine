/*
 * Copyright 2023 Alistair Leslie-Hughes
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */
#define COBJMACROS

#include "d3d10_1.h"
#include "d3dx10.h"
#undef MAKE_DDHRESULT
#include "d3dx11.h"
#include "wincodec.h"

#include "wined3dx.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3dx);

#include "wined3dx.h"

HRESULT WINAPI WICCreateImagingFactory_Proxy(UINT sdk_version, IWICImagingFactory **imaging_factory);

static const struct
{
    const GUID *wic_container_guid;
    D3DX10_IMAGE_FILE_FORMAT d3dx_file_format;
}
file_formats[] =
{
    { &GUID_ContainerFormatBmp,  D3DX10_IFF_BMP },
    { &GUID_ContainerFormatJpeg, D3DX10_IFF_JPG },
    { &GUID_ContainerFormatPng,  D3DX10_IFF_PNG },
    { &GUID_ContainerFormatDds,  D3DX10_IFF_DDS },
    { &GUID_ContainerFormatTiff, D3DX10_IFF_TIFF },
    { &GUID_ContainerFormatGif,  D3DX10_IFF_GIF },
    { &GUID_ContainerFormatWmp,  D3DX10_IFF_WMP },
};

static D3DX10_IMAGE_FILE_FORMAT wic_container_guid_to_file_format(GUID *container_format)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(file_formats); ++i)
    {
        if (IsEqualGUID(file_formats[i].wic_container_guid, container_format))
            return file_formats[i].d3dx_file_format;
    }
    return D3DX10_IFF_FORCE_DWORD;
}

static DXGI_FORMAT get_d3dx10_dds_format(DXGI_FORMAT format)
{
    static const struct
    {
        DXGI_FORMAT src;
        DXGI_FORMAT dst;
    }
    format_map[] =
    {
        {DXGI_FORMAT_UNKNOWN,           DXGI_FORMAT_R8G8B8A8_UNORM},
        {DXGI_FORMAT_R8_UNORM,          DXGI_FORMAT_R8G8B8A8_UNORM},
        {DXGI_FORMAT_R8G8_UNORM,        DXGI_FORMAT_R8G8B8A8_UNORM},
        {DXGI_FORMAT_B5G6R5_UNORM,      DXGI_FORMAT_R8G8B8A8_UNORM},
        {DXGI_FORMAT_B4G4R4A4_UNORM,    DXGI_FORMAT_R8G8B8A8_UNORM},
        {DXGI_FORMAT_B5G5R5A1_UNORM,    DXGI_FORMAT_R8G8B8A8_UNORM},
        {DXGI_FORMAT_B8G8R8X8_UNORM,    DXGI_FORMAT_R8G8B8A8_UNORM},
        {DXGI_FORMAT_B8G8R8A8_UNORM,    DXGI_FORMAT_R8G8B8A8_UNORM},
        {DXGI_FORMAT_R16_UNORM,         DXGI_FORMAT_R16G16B16A16_UNORM},
    };

    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(format_map); ++i)
    {
        if (format == format_map[i].src)
            return format_map[i].dst;
    }
    return format;
}

static D3D10_RESOURCE_DIMENSION wic_dimension_to_d3dx10_dimension(WICDdsDimension wic_dimension)
{
    switch (wic_dimension)
    {
        case WICDdsTexture1D:
            return D3D10_RESOURCE_DIMENSION_TEXTURE1D;
        case WICDdsTexture2D:
        case WICDdsTextureCube:
            return D3D10_RESOURCE_DIMENSION_TEXTURE2D;
        case WICDdsTexture3D:
            return D3D10_RESOURCE_DIMENSION_TEXTURE3D;
        default:
            return D3D10_RESOURCE_DIMENSION_UNKNOWN;
    }
}

HRESULT wined3dx_get_image_info(const void *data, SIZE_T size, void *imginfo)
{
    D3DX10_IMAGE_INFO *img_info = imginfo;
    IWICBitmapFrameDecode *frame = NULL;
    IWICImagingFactory *factory = NULL;
    IWICDdsDecoder *dds_decoder = NULL;
    IWICBitmapDecoder *decoder = NULL;
    WICDdsParameters dds_params;
    IWICStream *stream = NULL;
    unsigned int frame_count;
    GUID container_format;
    HRESULT hr;

    WICCreateImagingFactory_Proxy(WINCODEC_SDK_VERSION, &factory);
    IWICImagingFactory_CreateStream(factory, &stream);
    hr = IWICStream_InitializeFromMemory(stream, (BYTE *)data, size);
    if (FAILED(hr))
    {
        WARN("Failed to initialize stream.\n");
        goto end;
    }
    hr = IWICImagingFactory_CreateDecoderFromStream(factory, (IStream *)stream, NULL, 0, &decoder);
    if (FAILED(hr))
        goto end;

    hr = IWICBitmapDecoder_GetContainerFormat(decoder, &container_format);
    if (FAILED(hr))
        goto end;
    img_info->ImageFileFormat = wic_container_guid_to_file_format(&container_format);
    if (img_info->ImageFileFormat == D3DX10_IFF_FORCE_DWORD)
    {
        hr = E_FAIL;
        WARN("Unsupported image file format %s.\n", debugstr_guid(&container_format));
        goto end;
    }

    hr = IWICBitmapDecoder_GetFrameCount(decoder, &frame_count);
    if (FAILED(hr) || !frame_count)
        goto end;
    hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    if (FAILED(hr))
        goto end;
    hr = IWICBitmapFrameDecode_GetSize(frame, &img_info->Width, &img_info->Height);
    if (FAILED(hr))
        goto end;

    if (img_info->ImageFileFormat == D3DX10_IFF_DDS)
    {
        hr = IWICBitmapDecoder_QueryInterface(decoder, &IID_IWICDdsDecoder, (void **)&dds_decoder);
        if (FAILED(hr))
            goto end;
        hr = IWICDdsDecoder_GetParameters(dds_decoder, &dds_params);
        if (FAILED(hr))
            goto end;
        img_info->ArraySize = dds_params.ArraySize;
        img_info->Depth = dds_params.Depth;
        img_info->MipLevels = dds_params.MipLevels;
        img_info->ResourceDimension = wic_dimension_to_d3dx10_dimension(dds_params.Dimension);
        img_info->Format = get_d3dx10_dds_format(dds_params.DxgiFormat);
        img_info->MiscFlags = 0;
        if (dds_params.Dimension == WICDdsTextureCube)
        {
            img_info->MiscFlags = D3D10_RESOURCE_MISC_TEXTURECUBE;
            img_info->ArraySize *= 6;
        }
    }
    else
    {
        img_info->ArraySize = 1;
        img_info->Depth = 1;
        img_info->MipLevels = 1;
        img_info->ResourceDimension = D3D10_RESOURCE_DIMENSION_TEXTURE2D;
        img_info->Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        img_info->MiscFlags = 0;
    }

end:
    if (dds_decoder)
        IWICDdsDecoder_Release(dds_decoder);
    if (frame)
        IWICBitmapFrameDecode_Release(frame);
    if (decoder)
        IWICBitmapDecoder_Release(decoder);
    if (stream)
        IWICStream_Release(stream);
    if (factory)
        IWICImagingFactory_Release(factory);

    if (hr != S_OK)
    {
        WARN("Invalid or unsupported image file.\n");
        return E_FAIL;
    }
    return S_OK;
}
