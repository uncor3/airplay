#include "video_renderer.h"
#include <cstdlib>
#include <functional>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

typedef struct video_renderer_qt_s {
    video_renderer_t base;
    AVCodecContext *codec_ctx;
    AVFrame *av_frame;
    AVPacket *av_packet;
    SwsContext *sws_ctx;
    AVFrame *av_frame_rgb;
    uint8_t *av_frame_rgb_buffer;
    std::function<void(uint8_t*, int, int)> render_callback;
} video_renderer_qt_t;

static char av_error[AV_ERROR_MAX_STRING_SIZE] = {0};
#define av_err2str(errnum) av_make_error_string(av_error, AV_ERROR_MAX_STRING_SIZE, errnum)

extern std::function<void(uint8_t*, int, int)> qt_video_callback;

static void video_renderer_qt_start(video_renderer_t *renderer) {
    // Start callback is handled by Qt window
}

static void video_renderer_qt_render_buffer(video_renderer_t *renderer,
                                           raop_ntp_t *ntp,
                                           unsigned char *h264buffer,
                                           int h264buffer_size,
                                           uint64_t pts, int type) {
    video_renderer_qt_t *qt_renderer = (video_renderer_qt_t*)renderer;
    
    AVPacket *packet = qt_renderer->av_packet;
    packet->pts = (int64_t)pts;
    packet->data = h264buffer;
    packet->size = h264buffer_size;
    AVFrame *frame = qt_renderer->av_frame;

    int ret = avcodec_send_packet(qt_renderer->codec_ctx, packet);
    while (ret >= 0) {
        ret = avcodec_receive_frame(qt_renderer->codec_ctx, frame);
        if (ret == 0) {
            int w = frame->width;
            int h = frame->height;
            
            // Reinit scale context if dimensions changed
            if (!qt_renderer->sws_ctx || 
                qt_renderer->av_frame_rgb->width != w ||
                qt_renderer->av_frame_rgb->height != h) {
                
                if (qt_renderer->sws_ctx) {
                    sws_freeContext(qt_renderer->sws_ctx);
                }
                
                qt_renderer->sws_ctx = sws_getContext(
                    w, h, qt_renderer->codec_ctx->pix_fmt,
                    w, h, AV_PIX_FMT_RGB24,
                    SWS_BICUBIC, nullptr, nullptr, nullptr);
                
                if (qt_renderer->av_frame_rgb_buffer) {
                    av_free(qt_renderer->av_frame_rgb_buffer);
                }
                
                int buf_size = av_image_get_buffer_size(AV_PIX_FMT_RGB24, w, h, 1);
                qt_renderer->av_frame_rgb_buffer = (uint8_t*)av_malloc(buf_size);
                av_image_fill_arrays(qt_renderer->av_frame_rgb->data,
                                   qt_renderer->av_frame_rgb->linesize,
                                   qt_renderer->av_frame_rgb_buffer,
                                   AV_PIX_FMT_RGB24, w, h, 1);
                qt_renderer->av_frame_rgb->width = w;
                qt_renderer->av_frame_rgb->height = h;
            }
            
            sws_scale(qt_renderer->sws_ctx,
                     frame->data, frame->linesize, 0, frame->height,
                     qt_renderer->av_frame_rgb->data, qt_renderer->av_frame_rgb->linesize);
            
            // Call Qt callback with RGB data
            if (qt_video_callback) {
                qt_video_callback(qt_renderer->av_frame_rgb->data[0], w, h);
            }
        }
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        else if (ret < 0) {
            break;
        }
    }
}

static void video_renderer_qt_flush(video_renderer_t *renderer) {
    // Flush handled by Qt
}

static void video_renderer_qt_destroy(video_renderer_t *renderer) {
    video_renderer_qt_t *qt_renderer = (video_renderer_qt_t*)renderer;
    if (qt_renderer) {
        if (qt_renderer->av_frame) av_frame_free(&qt_renderer->av_frame);
        if (qt_renderer->av_packet) av_packet_free(&qt_renderer->av_packet);
        if (qt_renderer->codec_ctx) avcodec_free_context(&qt_renderer->codec_ctx);
        if (qt_renderer->sws_ctx) sws_freeContext(qt_renderer->sws_ctx);
        if (qt_renderer->av_frame_rgb) av_frame_free(&qt_renderer->av_frame_rgb);
        if (qt_renderer->av_frame_rgb_buffer) av_free(qt_renderer->av_frame_rgb_buffer);
        free(qt_renderer);
    }
}

static void video_renderer_qt_update_background(video_renderer_t *renderer, int type) {
    // Background handled by Qt
}

static const video_renderer_funcs_t video_renderer_qt_funcs = {
    .start = video_renderer_qt_start,
    .render_buffer = video_renderer_qt_render_buffer,
    .flush = video_renderer_qt_flush,
    .destroy = video_renderer_qt_destroy,
    .update_background = video_renderer_qt_update_background,
};

video_renderer_t *video_renderer_qt_init(logger_t *logger, video_renderer_config_t const *config) {
    video_renderer_qt_t *renderer = (video_renderer_qt_t*)calloc(1, sizeof(video_renderer_qt_t));
    
    renderer->av_frame = av_frame_alloc();
    renderer->av_packet = av_packet_alloc();
    renderer->av_frame_rgb = av_frame_alloc();
    
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    renderer->codec_ctx = avcodec_alloc_context3(codec);
    renderer->codec_ctx->time_base = {1, 25};
    renderer->codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    avcodec_open2(renderer->codec_ctx, codec, nullptr);
    
    renderer->base.logger = logger;
    renderer->base.funcs = &video_renderer_qt_funcs;
    renderer->base.type = VIDEO_RENDERER_FFMPEG_SDL2; // Reuse existing type
    
    return &renderer->base;
}
