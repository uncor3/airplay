/**
 * RPiPlay - An open-source AirPlay mirroring server for Raspberry Pi
 * Copyright (C) 2019 Florian Draschbacher
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 */

#include "sdl_event.h"
#include "video_renderer.h"

#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <functional>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
}

char av_error[AV_ERROR_MAX_STRING_SIZE] = {0};
#undef av_err2str
#define av_err2str(errnum)                                                     \
  av_make_error_string(av_error, AV_ERROR_MAX_STRING_SIZE, errnum)

typedef struct video_renderer_dummy_s {
  video_renderer_t base;
} video_renderer_dummy_t;

static AVCodecContext *codec_ctx;
static SDL_Renderer *sdl_renderer;
static SDL_Window *sdl_window;
static SDL_Texture *texture;

static void video_renderer_dummy_start(video_renderer_t *renderer) {}

static void video_renderer_dummy_render_buffer(video_renderer_t *renderer,
                                               raop_ntp_t *ntp,
                                               unsigned char *h264buffer,
                                               int h264buffer_size,
                                               uint64_t pts, int type) {
  AVPacket *packet = av_packet_alloc();

  packet->pts = (int64_t)pts;
  packet->data = h264buffer;
  packet->size = h264buffer_size;

  AVFrame *frame = av_frame_alloc();
  int ret = avcodec_send_packet(codec_ctx, packet);
  while (ret >= 0) {
    ret = avcodec_receive_frame(codec_ctx, frame);
    if (ret == 0) {
      printf("frame: %d, %d\n", frame->width, frame->height);
      int w = frame->width;
      int h = frame->height;
      codec_ctx->width = w;
      codec_ctx->height = h;
      SDL_Event event;
      event.type = SDL_USER_FUNC;
      event.user.data1 = new std::function<void()>(
          [w, h]() mutable { SDL_SetWindowSize(sdl_window, w, h); });
      SDL_UpdateYUVTexture(texture, nullptr, frame->data[0], frame->linesize[0],
                           frame->data[1], frame->linesize[1], frame->data[2],
                           frame->linesize[2]);
      SDL_PushEvent(&event);
      SDL_RenderClear(sdl_renderer);
      SDL_RenderCopy(sdl_renderer, texture, nullptr, nullptr);
      SDL_RenderPresent(sdl_renderer);
    }
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      break;
    else if (ret < 0) {
      fprintf(stderr, "Error decoding frame: %s\n", av_err2str(ret));
      exit(1);
    }
  }

  av_packet_unref(packet);
  av_frame_free(&frame);
}

static void video_renderer_dummy_flush(video_renderer_t *renderer) {}

static void video_renderer_dummy_destroy(video_renderer_t *renderer) {
  if (renderer) {
    free(renderer);
  }
  avcodec_free_context(&codec_ctx);
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(sdl_renderer);
  SDL_DestroyWindow(sdl_window);
  SDL_Quit();
}

static void video_renderer_dummy_update_background(video_renderer_t *renderer,
                                                   int type) {}

static const video_renderer_funcs_t video_renderer_dummy_funcs = {
    .start = video_renderer_dummy_start,
    .render_buffer = video_renderer_dummy_render_buffer,
    .flush = video_renderer_dummy_flush,
    .destroy = video_renderer_dummy_destroy,
    .update_background = video_renderer_dummy_update_background,
};

video_renderer_t *
video_renderer_ffmpeg_sdl2_init(logger_t *logger,
                                video_renderer_config_t const *config) {
  video_renderer_dummy_t *renderer;
  renderer =
      (video_renderer_dummy_t *)calloc(1, sizeof(video_renderer_dummy_t));
  if (!renderer) {
    return nullptr;
  }
  renderer->base.logger = logger;
  renderer->base.funcs = &video_renderer_dummy_funcs;
  renderer->base.type = VIDEO_RENDERER_DUMMY;

  // avcodec_register_all();
  const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
  codec_ctx = avcodec_alloc_context3(codec);
  codec_ctx->width = 450;
  codec_ctx->height = 972;
  codec_ctx->time_base.num = 1;
  codec_ctx->time_base.den = 25;

  avcodec_open2(codec_ctx, codec, nullptr);
  printf("codec_ctx: %d, %d\n", codec_ctx->width, codec_ctx->height);

  SDL_Init(SDL_INIT_VIDEO);
  sdl_window = SDL_CreateWindow("H264 Player", 0, 0, 100, 100, 0);
  sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_ACCELERATED);
  texture = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_YV12,
                              SDL_TEXTUREACCESS_STREAMING, codec_ctx->width,
                              codec_ctx->height);

  return &renderer->base;
}
