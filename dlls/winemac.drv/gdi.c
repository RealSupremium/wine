/*
 * Mac graphics driver initialisation functions
 *
 * Copyright 1996 Alexandre Julliard
 * Copyright 2011, 2012, 2013 Ken Thomases for CodeWeavers, Inc.
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

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include "macdrv.h"
#include "winreg.h"

WINE_DEFAULT_DEBUG_CHANNEL(macdrv);


typedef struct
{
    struct gdi_physdev  dev;
} MACDRV_PDEVICE;

static inline MACDRV_PDEVICE *get_macdrv_dev(PHYSDEV dev)
{
    return (MACDRV_PDEVICE*)dev;
}


/* a few dynamic device caps */
static CGRect desktop_rect;     /* virtual desktop rectangle */
static int horz_size;           /* horz. size of screen in millimeters */
static int vert_size;           /* vert. size of screen in millimeters */
static bool device_data_valid;  /* do the above variables have up-to-date values? */

bool retina_on = false;

static pthread_mutex_t device_data_mutex = PTHREAD_MUTEX_INITIALIZER;

static const struct user_driver_funcs macdrv_funcs;

/***********************************************************************
 *              compute_desktop_rect
 */
static void compute_desktop_rect(void)
{
    CGDirectDisplayID displayIDs[32];
    uint32_t count, i;

    desktop_rect = CGRectNull;
    if (CGGetOnlineDisplayList(ARRAY_SIZE(displayIDs), displayIDs, &count) != kCGErrorSuccess ||
        !count)
    {
        displayIDs[0] = CGMainDisplayID();
        count = 1;
    }

    for (i = 0; i < count; i++)
        desktop_rect = CGRectUnion(desktop_rect, CGDisplayBounds(displayIDs[i]));
    desktop_rect = cgrect_win_from_mac(desktop_rect);
}


/***********************************************************************
 *              macdrv_get_desktop_rect
 *
 * Returns the rectangle encompassing all the screens.
 */
CGRect macdrv_get_desktop_rect(void)
{
    CGRect ret;

    pthread_mutex_lock(&device_data_mutex);

    if (!device_data_valid)
    {
        check_retina_status();
        compute_desktop_rect();
    }
    ret = desktop_rect;

    pthread_mutex_unlock(&device_data_mutex);

    TRACE("%s\n", wine_dbgstr_cgrect(ret));

    return ret;
}


/**********************************************************************
 *              device_init
 *
 * Perform initializations needed upon creation of the first device.
 */
static void device_init(void)
{
    CGDirectDisplayID mainDisplay = CGMainDisplayID();
    CGSize size_mm = CGDisplayScreenSize(mainDisplay);

    check_retina_status();

    /* Initialize device caps */
    horz_size = size_mm.width;
    vert_size = size_mm.height;

    compute_desktop_rect();

    device_data_valid = true;
}


void macdrv_reset_device_metrics(void)
{
    pthread_mutex_lock(&device_data_mutex);
    device_data_valid = false;
    pthread_mutex_unlock(&device_data_mutex);
}


static MACDRV_PDEVICE *create_mac_physdev(void)
{
    MACDRV_PDEVICE *physDev;

    pthread_mutex_lock(&device_data_mutex);
    if (!device_data_valid) device_init();
    pthread_mutex_unlock(&device_data_mutex);

    if (!(physDev = calloc(1, sizeof(*physDev)))) return NULL;

    return physDev;
}


/**********************************************************************
 *              CreateDC (MACDRV.@)
 */
static BOOL macdrv_CreateDC(PHYSDEV *pdev, LPCWSTR device, LPCWSTR output, const DEVMODEW* initData)
{
    MACDRV_PDEVICE *physDev = create_mac_physdev();

    TRACE("pdev %p hdc %p device %s output %s initData %p\n", pdev,
          (*pdev)->hdc, debugstr_w(device), debugstr_w(output), initData);

    if (!physDev) return FALSE;

    push_dc_driver(pdev, &physDev->dev, &macdrv_funcs.dc_funcs);
    return TRUE;
}


/**********************************************************************
 *              CreateCompatibleDC (MACDRV.@)
 */
static BOOL macdrv_CreateCompatibleDC(PHYSDEV orig, PHYSDEV *pdev)
{
    MACDRV_PDEVICE *physDev = create_mac_physdev();

    TRACE("orig %p orig->hdc %p pdev %p pdev->hdc %p\n", orig, (orig ? orig->hdc : NULL), pdev,
          ((pdev && *pdev) ? (*pdev)->hdc : NULL));

    if (!physDev) return FALSE;

    push_dc_driver(pdev, &physDev->dev, &macdrv_funcs.dc_funcs);
    return TRUE;
}


/**********************************************************************
 *              DeleteDC (MACDRV.@)
 */
static BOOL macdrv_DeleteDC(PHYSDEV dev)
{
    MACDRV_PDEVICE *physDev = get_macdrv_dev(dev);

    TRACE("hdc %p\n", dev->hdc);

    free(physDev);
    return TRUE;
}

static void CDECL free_heap_bits( struct gdi_image_bits *bits )
{
    free( bits->ptr );
}

typedef unsigned long VisualID;
typedef struct {
  void *visual;
  VisualID visualid;
  int screen;
  int depth;
#if defined(__cplusplus) || defined(c_plusplus)
  int c_class;					/* C++ */
#else
  int class;
#endif
  unsigned long red_mask;
  unsigned long green_mask;
  unsigned long blue_mask;
  int colormap_size;
  int bits_per_rgb;
} XVisualInfo;

/* Maps pixel to the entry in the system palette */
int *X11DRV_PALETTE_XPixelToPalette = NULL;

/* store the palette or color mask data in the bitmap info structure */
static void set_color_info( const XVisualInfo *vis, BITMAPINFO *info, BOOL has_alpha )
{
    DWORD *colors = (DWORD *)((char *)info + info->bmiHeader.biSize);

    info->bmiHeader.biCompression = BI_RGB;
    info->bmiHeader.biClrUsed = 0;

    switch (info->bmiHeader.biBitCount)
    {
    case 4:
    case 8:
    {
        RGBQUAD *rgb = (RGBQUAD *)colors;
        PALETTEENTRY palette[256];
        UINT i, count;

        info->bmiHeader.biClrUsed = 1 << info->bmiHeader.biBitCount;
        //count = X11DRV_GetSystemPaletteEntries( NULL, 0, info->bmiHeader.biClrUsed, palette );
        count = 0;
        for (i = 0; i < count; i++)
        {
            rgb[i].rgbRed   = palette[i].peRed;
            rgb[i].rgbGreen = palette[i].peGreen;
            rgb[i].rgbBlue  = palette[i].peBlue;
            rgb[i].rgbReserved = 0;
        }
        memset( &rgb[count], 0, (info->bmiHeader.biClrUsed - count) * sizeof(*rgb) );
        break;
    }
    case 16:
        colors[0] = vis->red_mask;
        colors[1] = vis->green_mask;
        colors[2] = vis->blue_mask;
        info->bmiHeader.biCompression = BI_BITFIELDS;
        break;
    case 32:
        colors[0] = vis->red_mask;
        colors[1] = vis->green_mask;
        colors[2] = vis->blue_mask;
        if (colors[0] != 0xff0000 || colors[1] != 0x00ff00 || colors[2] != 0x0000ff || !has_alpha)
            info->bmiHeader.biCompression = BI_BITFIELDS;
        break;
    }
}

extern macdrv_view macdrv_get_cocoa_view(HWND hwnd);

void macdrv_get_image_from_screen(const struct wxRect *subrect, double contentScaleFactor, void* *pbits, int* pbytes_per_line);
void macdrv_get_image(macdrv_view v, const struct wxRect *subrect, double contentScaleFactor, void* *pbits, int* pbytes_per_line);

/***********************************************************************
 *           macdrv_GetImage
 */
DWORD CDECL macdrv_GetImage( PHYSDEV dev, BITMAPINFO *info,
                             struct gdi_image_bits *bits, struct bitblt_coords *src )
{
    MACDRV_PDEVICE *physdev = get_macdrv_dev(dev);
    DWORD ret = ERROR_SUCCESS;
    XVisualInfo vis = {0};
    UINT align, x, y, width, height;
    const int *mapping = NULL;

    vis.depth = bits_per_pixel;
    
    vis.red_mask   = 0xff0000;
    vis.green_mask = 0x00ff00;
    vis.blue_mask  = 0x0000ff;

    /* align start and width to 32-bit boundary */
    switch (bits_per_pixel)
    {
    case 1:  align = 32; break;
    case 4:  align = 8;  mapping = X11DRV_PALETTE_XPixelToPalette; break;
    case 8:  align = 4;  mapping = X11DRV_PALETTE_XPixelToPalette; break;
    case 16: align = 2;  break;
    case 24: align = 4;  break;
    case 32: align = 1;  break;
    default:
        FIXME( "depth %u bpp %u not supported yet\n", vis.depth, bits_per_pixel );
        return ERROR_BAD_FORMAT;
    }

    info->bmiHeader.biSize          = sizeof(info->bmiHeader);
    info->bmiHeader.biPlanes        = 1;
    info->bmiHeader.biBitCount      = bits_per_pixel;
    info->bmiHeader.biXPelsPerMeter = 0;
    info->bmiHeader.biYPelsPerMeter = 0;
    info->bmiHeader.biClrImportant  = 0;
    set_color_info( &vis, info, FALSE );

    if (!bits) return ERROR_SUCCESS;  /* just querying the color information */

    x = src->visrect.left & ~(align - 1);
    y = src->visrect.top;
    width = src->visrect.right - x;
    height = src->visrect.bottom - src->visrect.top;
    //if (format->scanline_pad != 32) width = (width + (align - 1)) & ~(align - 1);
    /* make the source rectangle relative to the returned bits */
    src->x -= x;
    src->y -= y;
    OffsetRect( &src->visrect, -x, -y );

    //Get image from the platform device
    int bytes_per_line = 0;
    HWND hwnd = NtUserWindowFromDC(dev->hdc);
    // We will only create 32 bit bitmaps
    if (hwnd == NULL || hwnd == NtUserGetDesktopWindow())
    {
        struct wxRect subrect = {src->log_x, src->log_y, src->log_width, src->log_height};
        macdrv_get_image_from_screen(&subrect, 1.0, &bits->ptr, &bytes_per_line);
    }
    else
    {
        macdrv_view view = macdrv_get_cocoa_view(hwnd);
        if (view != NULL)
        {
            struct wxRect subrect = {src->log_x, src->log_y, src->log_width, src->log_height};
            macdrv_get_image(view, &subrect, 1.0, &bits->ptr, &bytes_per_line);
        }
        else
        {
            // Window in other process are not currently supported
            FIXME( "Window in other process is not supported yet\n");
        }
    }
    if (bits->ptr) {
        bits->is_copy = TRUE;
        bits->free = free_heap_bits;
    }

    info->bmiHeader.biWidth     = width;
    info->bmiHeader.biHeight    = -height;
    info->bmiHeader.biSizeImage = height * bytes_per_line;
    return ret;
}

/***********************************************************************
 *              GetDeviceCaps (MACDRV.@)
 */
static INT macdrv_GetDeviceCaps(PHYSDEV dev, INT cap)
{
    INT ret;

    pthread_mutex_lock(&device_data_mutex);

    if (!device_data_valid) device_init();

    switch(cap)
    {
    case HORZSIZE:
        ret = horz_size;
        break;
    case VERTSIZE:
        ret = vert_size;
        break;
    case HORZRES:
    case VERTRES:
    default:
        pthread_mutex_unlock(&device_data_mutex);
        dev = GET_NEXT_PHYSDEV( dev, pGetDeviceCaps );
        ret = dev->funcs->pGetDeviceCaps( dev, cap );
        if ((cap == HORZRES || cap == VERTRES) && retina_on)
            ret *= 2;
        return ret;
    }

    TRACE("cap %d -> %d\n", cap, ret);

    pthread_mutex_unlock(&device_data_mutex);
    return ret;
}


static const struct user_driver_funcs macdrv_funcs =
{
    .dc_funcs.pCreateCompatibleDC = macdrv_CreateCompatibleDC,
    .dc_funcs.pCreateDC = macdrv_CreateDC,
    .dc_funcs.pDeleteDC = macdrv_DeleteDC,
    .dc_funcs.pGetDeviceCaps = macdrv_GetDeviceCaps,
    .dc_funcs.pGetDeviceGammaRamp = macdrv_GetDeviceGammaRamp,
    .dc_funcs.pGetImage = macdrv_GetImage,
    .dc_funcs.pSetDeviceGammaRamp = macdrv_SetDeviceGammaRamp,
    .dc_funcs.priority = GDI_PRIORITY_GRAPHICS_DRV,

    .pActivateKeyboardLayout = macdrv_ActivateKeyboardLayout,
    .pBeep = macdrv_Beep,
    .pChangeDisplaySettings = macdrv_ChangeDisplaySettings,
    .pClipCursor = macdrv_ClipCursor,
    .pNotifyIcon = macdrv_NotifyIcon,
    .pCleanupIcons = macdrv_CleanupIcons,
    .pClipboardWindowProc = macdrv_ClipboardWindowProc,
    .pDesktopWindowProc = macdrv_DesktopWindowProc,
    .pDestroyCursorIcon = macdrv_DestroyCursorIcon,
    .pDestroyWindow = macdrv_DestroyWindow,
    .pUpdateDisplayDevices = macdrv_UpdateDisplayDevices,
    .pGetCursorPos = macdrv_GetCursorPos,
    .pGetKeyboardLayoutList = macdrv_GetKeyboardLayoutList,
    .pGetKeyNameText = macdrv_GetKeyNameText,
    .pMapVirtualKeyEx = macdrv_MapVirtualKeyEx,
    .pProcessEvents = macdrv_ProcessEvents,
    .pRegisterHotKey = macdrv_RegisterHotKey,
    .pSetCapture = macdrv_SetCapture,
    .pSetCursor = macdrv_SetCursor,
    .pSetCursorPos = macdrv_SetCursorPos,
    .pSetDesktopWindow = macdrv_SetDesktopWindow,
    .pActivateWindow = macdrv_ActivateWindow,
    .pSetLayeredWindowAttributes = macdrv_SetLayeredWindowAttributes,
    .pSetParent = macdrv_SetParent,
    .pSetWindowRgn = macdrv_SetWindowRgn,
    .pSetWindowStyle = macdrv_SetWindowStyle,
    .pSetWindowText = macdrv_SetWindowText,
    .pShowWindow = macdrv_ShowWindow,
    .pSysCommand =macdrv_SysCommand,
    .pSystemParametersInfo = macdrv_SystemParametersInfo,
    .pThreadDetach = macdrv_ThreadDetach,
    .pToUnicodeEx = macdrv_ToUnicodeEx,
    .pUnregisterHotKey = macdrv_UnregisterHotKey,
    .pUpdateClipboard = macdrv_UpdateClipboard,
    .pUpdateLayeredWindow = macdrv_UpdateLayeredWindow,
    .pVkKeyScanEx = macdrv_VkKeyScanEx,
    .pImeProcessKey = macdrv_ImeProcessKey,
    .pNotifyIMEStatus = macdrv_NotifyIMEStatus,
    .pSetIMECompositionRect = macdrv_SetIMECompositionRect,
    .pWindowMessage = macdrv_WindowMessage,
    .pWindowPosChanged = macdrv_WindowPosChanged,
    .pWindowPosChanging = macdrv_WindowPosChanging,
    .pGetWindowStyleMasks = macdrv_GetWindowStyleMasks,
    .pCreateWindowSurface = macdrv_CreateWindowSurface,
    .pVulkanInit = macdrv_VulkanInit,
    .pOpenGLInit = macdrv_OpenGLInit,
};


void init_user_driver(void)
{
    __wine_set_user_driver( &macdrv_funcs, WINE_GDI_DRIVER_VERSION );
}
