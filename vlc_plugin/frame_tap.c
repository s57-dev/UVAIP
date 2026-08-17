/*****************************************************************************
 * frame_tap.c : VLC video filter that exports decoded frames to the camera_app
 *               pipeline via POSIX shared memory.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

/* MODULE_NAME must be an unquoted token so VLC pastes vlc_entry__frame_tap. */
#ifndef MODULE_NAME
# define MODULE_NAME frame_tap
#endif
#ifndef MODULE_STRING
# define MODULE_STRING "frame_tap"
#endif

#include <vlc_common.h>
#include <vlc_configuration.h>
#include <vlc_plugin.h>
#include <vlc_filter.h>
#include <vlc_picture.h>

#include "shared_frame_channel.h"

#include <stdlib.h>
#include <string.h>

#define CFG_PREFIX "frame-tap-"

static int Create(vlc_object_t*);
static void Destroy(vlc_object_t*);
static picture_t* Filter(filter_t*, picture_t*);

#define SHM_TEXT "Shared memory name"
#define SHM_LONGTEXT "POSIX shared memory object name used to publish BGR frames"

vlc_module_begin()
    set_description("Camera pipeline frame tap")
    set_shortname("Frame tap")
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VFILTER)
    set_capability("video filter", 0)
    add_shortcut("frame_tap")
    add_string(CFG_PREFIX "shm", SFC_DEFAULT_SHM_NAME, SHM_TEXT, SHM_LONGTEXT, false)
    set_callbacks(Create, Destroy)
vlc_module_end()

struct filter_sys_t
{
    sfc_producer_t producer;
    uint8_t* bgr_buffer;
    size_t bgr_capacity;
};

static uint8_t clamp_u8(int value)
{
    if (value < 0)
    {
        return 0;
    }
    if (value > 255)
    {
        return 255;
    }
    return (uint8_t)value;
}

static void i420_to_bgr24(const picture_t* pic, uint8_t* bgr_out, uint32_t width, uint32_t height)
{
    const uint8_t* y_plane = pic->p[Y_PLANE].p_pixels;
    const uint8_t* u_plane = pic->p[U_PLANE].p_pixels;
    const uint8_t* v_plane = pic->p[V_PLANE].p_pixels;
    const int y_pitch = pic->p[Y_PLANE].i_pitch;
    const int u_pitch = pic->p[U_PLANE].i_pitch;
    const int v_pitch = pic->p[V_PLANE].i_pitch;
    const uint32_t stride = width * SFC_BYTES_PER_PIXEL;

    for (uint32_t row = 0; row < height; ++row)
    {
        for (uint32_t col = 0; col < width; ++col)
        {
            const int y = y_plane[row * y_pitch + col];
            const int u = u_plane[(row / 2) * u_pitch + (col / 2)] - 128;
            const int v = v_plane[(row / 2) * v_pitch + (col / 2)] - 128;

            const int r = y + ((1436 * v) / 1024);
            const int g = y - ((352 * u + 731 * v) / 1024);
            const int b = y + ((1814 * u) / 1024);

            const size_t index = row * stride + col * SFC_BYTES_PER_PIXEL;
            bgr_out[index + 0] = clamp_u8(b);
            bgr_out[index + 1] = clamp_u8(g);
            bgr_out[index + 2] = clamp_u8(r);
        }
    }
}

static int export_picture(filter_t* p_filter, picture_t* pic)
{
    filter_sys_t* p_sys = p_filter->p_sys;
    const video_format_t* fmt = &pic->format;
    const uint32_t width = fmt->i_visible_width;
    const uint32_t height = fmt->i_visible_height;
    const size_t required = (size_t)width * height * SFC_BYTES_PER_PIXEL;

    if (required > p_sys->bgr_capacity)
    {
        uint8_t* resized = realloc(p_sys->bgr_buffer, required);
        if (!resized)
        {
            return VLC_ENOMEM;
        }
        p_sys->bgr_buffer = resized;
        p_sys->bgr_capacity = required;
    }

    switch (fmt->i_chroma)
    {
    case VLC_CODEC_RGB24:
    {
        const uint8_t* src = pic->p[0].p_pixels;
        const int src_pitch = pic->p[0].i_pitch;
        const uint32_t stride = width * SFC_BYTES_PER_PIXEL;
        for (uint32_t row = 0; row < height; ++row)
        {
            memcpy(p_sys->bgr_buffer + row * stride, src + row * src_pitch, stride);
        }
        break;
    }
    case VLC_CODEC_I420:
    case VLC_CODEC_J420:
    case VLC_CODEC_YV12:
        i420_to_bgr24(pic, p_sys->bgr_buffer, width, height);
        break;
    default:
        msg_Warn(p_filter, "Unsupported chroma for frame tap: %4.4s",
                 (const char*)&fmt->i_chroma);
        return VLC_EGENERIC;
    }

    if (sfc_producer_publish_bgr24(&p_sys->producer, p_sys->bgr_buffer, width, height,
                                   width * SFC_BYTES_PER_PIXEL) != 0)
    {
        msg_Warn(p_filter, "Failed to publish frame to shared memory");
        return VLC_EGENERIC;
    }

    return VLC_SUCCESS;
}

static int Create(vlc_object_t* p_this)
{
    filter_t* p_filter = (filter_t*)p_this;
    filter_sys_t* p_sys = calloc(1, sizeof(*p_sys));
    if (!p_sys)
    {
        return VLC_ENOMEM;
    }

    char* shm_name = var_CreateGetString(p_filter, CFG_PREFIX "shm");
    if (!shm_name)
    {
        free(p_sys);
        return VLC_EGENERIC;
    }

    if (sfc_producer_open(&p_sys->producer, shm_name, 1) != 0)
    {
        msg_Err(p_filter, "Unable to open shared memory '%s'", shm_name);
        free(shm_name);
        free(p_sys);
        return VLC_EGENERIC;
    }

    free(shm_name);
    p_filter->p_sys = p_sys;
    p_filter->pf_video_filter = Filter;
  msg_Info(p_filter, "Frame tap publishing to shared memory '%s'", p_sys->producer.shm_name);
    return VLC_SUCCESS;
}

static void Destroy(vlc_object_t* p_this)
{
    filter_t* p_filter = (filter_t*)p_this;
    filter_sys_t* p_sys = p_filter->p_sys;
    if (!p_sys)
    {
        return;
    }

    free(p_sys->bgr_buffer);
    sfc_producer_close(&p_sys->producer);
    free(p_sys);
}

static picture_t* Filter(filter_t* p_filter, picture_t* p_pic)
{
    if (p_pic)
    {
        export_picture(p_filter, p_pic);
    }
    return p_pic;
}
