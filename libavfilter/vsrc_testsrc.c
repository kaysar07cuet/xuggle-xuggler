/*
 * Copyright (c) 2007 Nicolas George <nicolas.george@normalesup.org>
 * Copyright (c) 2011 Stefano Sabatini
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Based on the test pattern generator demuxer by Nicolas George:
 * http://lists.mplayerhq.hu/pipermail/ffmpeg-devel/2007-October/037845.html
 */

#include <float.h>

#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "avfilter.h"

typedef struct {
    const AVClass *class;
    int h, w;
    unsigned int nb_frame;
    AVRational time_base;
    int64_t pts, max_pts;
    char *size;                 ///< video frame size
    char *rate;                 ///< video frame rate
    char *duration;             ///< total duration of the generated video
} TestSourceContext;

#define OFFSET(x) offsetof(TestSourceContext, x)

static const AVOption testsrc_options[]= {
    { "size",     "set video size",     OFFSET(size),     FF_OPT_TYPE_STRING, {.str = "320x240"}, 0, 0 },
    { "s",        "set video size",     OFFSET(size),     FF_OPT_TYPE_STRING, {.str = "320x240"}, 0, 0 },
    { "rate",     "set video rate",     OFFSET(rate),     FF_OPT_TYPE_STRING, {.str = "25"},      0, 0 },
    { "r",        "set video rate",     OFFSET(rate),     FF_OPT_TYPE_STRING, {.str = "25"},      0, 0 },
    { "duration", "set video duration", OFFSET(duration), FF_OPT_TYPE_STRING, {.str = NULL},      0, 0 },
    { NULL },
};

static const char *testsrc_get_name(void *ctx)
{
    return "testsrc";
}

static const AVClass testsrc_class = {
    "TestSourceContext",
    testsrc_get_name,
    testsrc_options
};

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    TestSourceContext *test = ctx->priv;
    AVRational frame_rate_q;
    int64_t duration = -1;
    int ret = 0;

    test->class = &testsrc_class;
    av_opt_set_defaults2(test, 0, 0);

    if ((ret = (av_set_options_string(test, args, "=", ":"))) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error parsing options string: '%s'\n", args);
        return ret;
    }

    if ((ret = av_parse_video_size(&test->w, &test->h, test->size)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid frame size: '%s'\n", test->size);
        return ret;
    }

    if ((ret = av_parse_video_rate(&frame_rate_q, test->rate)) < 0 ||
        frame_rate_q.den <= 0 || frame_rate_q.num <= 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid frame rate: '%s'\n", test->rate);
        return ret;
    }

    if ((test->duration) && (ret = av_parse_time(&duration, test->duration, 1)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid duration: '%s'\n", test->duration);
        return ret;
    }

    test->time_base.num = frame_rate_q.den;
    test->time_base.den = frame_rate_q.num;
    test->max_pts = duration >= 0 ?
        av_rescale_q(duration, AV_TIME_BASE_Q, test->time_base) : -1;
    test->nb_frame = 0;
    test->pts = 0;

    av_log(ctx, AV_LOG_INFO, "size:%dx%d rate:%d/%d duration:%f\n",
           test->w, test->h, frame_rate_q.num, frame_rate_q.den,
           duration < 0 ? -1 : test->max_pts * av_q2d(test->time_base));
    return 0;
}

static int config_props(AVFilterLink *outlink)
{
    TestSourceContext *test = outlink->src->priv;

    outlink->w = test->w;
    outlink->h = test->h;
    outlink->time_base = test->time_base;

    return 0;
}

/**
 * Fill a rectangle with value val.
 *
 * @param val the RGB value to set
 * @param dst pointer to the destination buffer to fill
 * @param dst_linesize linesize of destination
 * @param segment_width width of the segment
 * @param x horizontal coordinate where to draw the rectangle in the destination buffer
 * @param y horizontal coordinate where to draw the rectangle in the destination buffer
 * @param w width  of the rectangle to draw, expressed as a number of segment_width units
 * @param h height of the rectangle to draw, expressed as a number of segment_width units
 */
static void draw_rectangle(unsigned val, uint8_t *dst, int dst_linesize, unsigned segment_width,
                           unsigned x, unsigned y, unsigned w, unsigned h)
{
    int i;
    int step = 3;

    dst += segment_width * (step * x + y * dst_linesize);
    w *= segment_width * step;
    h *= segment_width;
    for (i = 0; i < h; i++) {
        memset(dst, val, w);
        dst += dst_linesize;
    }
}

static void draw_digit(int digit, uint8_t *dst, unsigned dst_linesize,
                       unsigned segment_width)
{
#define TOP_HBAR        1
#define MID_HBAR        2
#define BOT_HBAR        4
#define LEFT_TOP_VBAR   8
#define LEFT_BOT_VBAR  16
#define RIGHT_TOP_VBAR 32
#define RIGHT_BOT_VBAR 64
    struct {
        int x, y, w, h;
    } segments[] = {
        { 1,  0, 5, 1 }, /* TOP_HBAR */
        { 1,  6, 5, 1 }, /* MID_HBAR */
        { 1, 12, 5, 1 }, /* BOT_HBAR */
        { 0,  1, 1, 5 }, /* LEFT_TOP_VBAR */
        { 0,  7, 1, 5 }, /* LEFT_BOT_VBAR */
        { 6,  1, 1, 5 }, /* RIGHT_TOP_VBAR */
        { 6,  7, 1, 5 }  /* RIGHT_BOT_VBAR */
    };
    static const unsigned char masks[10] = {
        /* 0 */ TOP_HBAR         |BOT_HBAR|LEFT_TOP_VBAR|LEFT_BOT_VBAR|RIGHT_TOP_VBAR|RIGHT_BOT_VBAR,
        /* 1 */                                                        RIGHT_TOP_VBAR|RIGHT_BOT_VBAR,
        /* 2 */ TOP_HBAR|MID_HBAR|BOT_HBAR|LEFT_BOT_VBAR                             |RIGHT_TOP_VBAR,
        /* 3 */ TOP_HBAR|MID_HBAR|BOT_HBAR                            |RIGHT_TOP_VBAR|RIGHT_BOT_VBAR,
        /* 4 */          MID_HBAR         |LEFT_TOP_VBAR              |RIGHT_TOP_VBAR|RIGHT_BOT_VBAR,
        /* 5 */ TOP_HBAR|BOT_HBAR|MID_HBAR|LEFT_TOP_VBAR                             |RIGHT_BOT_VBAR,
        /* 6 */ TOP_HBAR|BOT_HBAR|MID_HBAR|LEFT_TOP_VBAR|LEFT_BOT_VBAR               |RIGHT_BOT_VBAR,
        /* 7 */ TOP_HBAR                                              |RIGHT_TOP_VBAR|RIGHT_BOT_VBAR,
        /* 8 */ TOP_HBAR|BOT_HBAR|MID_HBAR|LEFT_TOP_VBAR|LEFT_BOT_VBAR|RIGHT_TOP_VBAR|RIGHT_BOT_VBAR,
        /* 9 */ TOP_HBAR|BOT_HBAR|MID_HBAR|LEFT_TOP_VBAR              |RIGHT_TOP_VBAR|RIGHT_BOT_VBAR,
    };
    unsigned mask = masks[digit];
    int i;

    draw_rectangle(0, dst, dst_linesize, segment_width, 0, 0, 8, 13);
    for (i = 0; i < FF_ARRAY_ELEMS(segments); i++)
        if (mask & (1<<i))
            draw_rectangle(255, dst, dst_linesize, segment_width,
                           segments[i].x, segments[i].y, segments[i].w, segments[i].h);
}

#define GRADIENT_SIZE (6 * 256)

static void fill_picture(TestSourceContext *test, AVFilterBufferRef *picref)
{
    uint8_t *p, *p0;
    int x, y;
    int color, color_rest;
    int icolor;
    int radius;
    int quad0, quad;
    int dquad_x, dquad_y;
    int grad, dgrad, rgrad, drgrad;
    int seg_size;
    int second;
    int i;
    uint8_t *data = picref->data[0];
    int width  = picref->video->w;
    int height = picref->video->h;

    /* draw colored bars and circle */
    radius = (width + height) / 4;
    quad0 = width * width / 4 + height * height / 4 - radius * radius;
    dquad_y = 1 - height;
    p0 = data;
    for (y = 0; y < height; y++) {
        p = p0;
        color = 0;
        color_rest = 0;
        quad = quad0;
        dquad_x = 1 - width;
        for (x = 0; x < width; x++) {
            icolor = color;
            if (quad < 0)
                icolor ^= 7;
            quad += dquad_x;
            dquad_x += 2;
            *(p++) = icolor & 1 ? 255 : 0;
            *(p++) = icolor & 2 ? 255 : 0;
            *(p++) = icolor & 4 ? 255 : 0;
            color_rest += 8;
            if (color_rest >= width) {
                color_rest -= width;
                color++;
            }
        }
        quad0 += dquad_y;
        dquad_y += 2;
        p0 += picref->linesize[0];
    }

    /* draw sliding color line */
    p = data + picref->linesize[0] * height * 3/4;
    grad = (256 * test->nb_frame * test->time_base.num / test->time_base.den) %
        GRADIENT_SIZE;
    rgrad = 0;
    dgrad = GRADIENT_SIZE / width;
    drgrad = GRADIENT_SIZE % width;
    for (x = 0; x < width; x++) {
        *(p++) =
            grad < 256 || grad >= 5 * 256 ? 255 :
            grad >= 2 * 256 && grad < 4 * 256 ? 0 :
            grad < 2 * 256 ? 2 * 256 - 1 - grad : grad - 4 * 256;
        *(p++) =
            grad >= 4 * 256 ? 0 :
            grad >= 1 * 256 && grad < 3 * 256 ? 255 :
            grad < 1 * 256 ? grad : 4 * 256 - 1 - grad;
        *(p++) =
            grad < 2 * 256 ? 0 :
            grad >= 3 * 256 && grad < 5 * 256 ? 255 :
            grad < 3 * 256 ? grad - 2 * 256 : 6 * 256 - 1 - grad;
        grad += dgrad;
        rgrad += drgrad;
        if (rgrad >= GRADIENT_SIZE) {
            grad++;
            rgrad -= GRADIENT_SIZE;
        }
        if (grad >= GRADIENT_SIZE)
            grad -= GRADIENT_SIZE;
    }
    for (y = height / 8; y > 0; y--) {
        memcpy(p, p - picref->linesize[0], 3 * width);
        p += picref->linesize[0];
    }

    /* draw digits */
    seg_size = width / 80;
    if (seg_size >= 1 && height >= 13 * seg_size) {
        second = test->nb_frame * test->time_base.num / test->time_base.den;
        x = width - (width - seg_size * 64) / 2;
        y = (height - seg_size * 13) / 2;
        p = data + (x*3 + y * picref->linesize[0]);
        for (i = 0; i < 8; i++) {
            p -= 3 * 8 * seg_size;
            draw_digit(second % 10, p, picref->linesize[0], seg_size);
            second /= 10;
            if (second == 0)
                break;
        }
    }
}

static int request_frame(AVFilterLink *outlink)
{
    TestSourceContext *test = outlink->src->priv;
    AVFilterBufferRef *picref;

    if (test->max_pts >= 0 && test->pts > test->max_pts)
        return AVERROR_EOF;
    picref = avfilter_get_video_buffer(outlink, AV_PERM_WRITE,
                                       test->w, test->h);
    picref->pts = test->pts++;
    test->nb_frame++;
    fill_picture(test, picref);

    avfilter_start_frame(outlink, avfilter_ref_buffer(picref, ~0));
    avfilter_draw_slice(outlink, 0, picref->video->h, 1);
    avfilter_end_frame(outlink);
    avfilter_unref_buffer(picref);

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        PIX_FMT_RGB24, PIX_FMT_NONE
    };
    avfilter_set_common_pixel_formats(ctx, avfilter_make_format_list(pix_fmts));
    return 0;
}

AVFilter avfilter_vsrc_testsrc = {
    .name      = "testsrc",
    .description = NULL_IF_CONFIG_SMALL("Generate test pattern."),
    .priv_size = sizeof(TestSourceContext),
    .init      = init,

    .query_formats   = query_formats,

    .inputs    = (AVFilterPad[]) {{ .name = NULL}},

    .outputs   = (AVFilterPad[]) {{ .name = "default",
                                    .type = AVMEDIA_TYPE_VIDEO,
                                    .request_frame = request_frame,
                                    .config_props  = config_props, },
                                  { .name = NULL }},
};