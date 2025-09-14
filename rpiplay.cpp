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

#include <cstring>
#include <fstream>
#include <signal.h>
#include <stddef.h>
#include <string>
#include <unistd.h>
#include <vector>

#include "lib/dnssd.h"
#include "lib/logger.h"
#include "lib/raop.h"
#include "lib/stream.h"
#include "log.h"
#include "renderers/audio_renderer.h"
#include "renderers/video_renderer.h"
#include <functional>

#ifdef HAS_FFMPEG_SDL2_RENDERER
#include "renderers/sdl_event.h"
#endif

#define VERSION "1.2"

#define DEFAULT_NAME "RPiPlay"
#define DEFAULT_BACKGROUND_MODE BACKGROUND_MODE_ON
#define DEFAULT_AUDIO_DEVICE AUDIO_DEVICE_HDMI
#define DEFAULT_LOW_LATENCY false
#define DEFAULT_DEBUG_LOG false
#define DEFAULT_ROTATE 0
#define DEFAULT_DISPLAY_WIDTH 1920
#define DEFAULT_DISPLAY_HEIGHT 1080
#define DEFAULT_DISPLAY_FRAMERATE 60.0
#define DEFAULT_FLIP FLIP_NONE
#define DEFAULT_HW_ADDRESS                                                     \
    {(char)0x48, (char)0x5d, (char)0x60, (char)0x7c, (char)0xee, (char)0x22}

int start_server(std::vector<char> hw_addr, std::string name, bool debug_log,
                 video_renderer_config_t const *video_config,
                 audio_renderer_config_t const *audio_config, int display_width,
                 int display_height, float display_framerate);

int stop_server();

int display_width = 1920;
int display_height = 1080;
float display_framerate = 60.0;

typedef video_renderer_t *(*video_init_func_t)(
    logger_t *logger, video_renderer_config_t const *config);
typedef audio_renderer_t *(*audio_init_func_t)(
    logger_t *logger, video_renderer_t *video_renderer,
    audio_renderer_config_t const *config);

typedef struct video_renderer_list_entry_s {
    const char *name;
    const char *description;
    video_init_func_t init_func;
} video_renderer_list_entry_t;

typedef struct audio_renderer_list_entry_s {
    const char *name;
    const char *description;
    audio_init_func_t init_func;
} audio_renderer_list_entry_t;

static bool running = false;
static dnssd_t *dnssd = NULL;
static raop_t *raop = NULL;
static video_init_func_t video_init_func = NULL;
static audio_init_func_t audio_init_func = NULL;
static video_renderer_t *video_renderer = NULL;
static audio_renderer_t *audio_renderer = NULL;
static logger_t *render_logger = NULL;

static void signal_handler(int sig)
{
    switch (sig) {
    case SIGINT:
    case SIGTERM:
        running = 0;
        break;
    }
}

static void init_signals(void)
{
#ifdef _WIN32
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#else
    struct sigaction sigact;

    sigact.sa_handler = signal_handler;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigaction(SIGINT, &sigact, NULL);
    sigaction(SIGTERM, &sigact, NULL);
#endif
}

static int parse_hw_addr(std::string str, std::vector<char> &hw_addr)
{
    for (int i = 0; i < str.length(); i += 3) {
        hw_addr.push_back((char)stol(str.substr(i), NULL, 16));
    }
    return 0;
}

// Server callbacks
extern "C" void conn_init(void *cls)
{
    if (video_renderer)
        video_renderer->funcs->update_background(video_renderer, 1);
}

extern "C" void conn_destroy(void *cls)
{
    if (video_renderer)
        video_renderer->funcs->update_background(video_renderer, -1);
}

extern "C" void audio_process(void *cls, raop_ntp_t *ntp,
                              audio_decode_struct *data)
{
    if (audio_renderer != NULL) {
        audio_renderer->funcs->render_buffer(audio_renderer, ntp, data->data,
                                             data->data_len,
                                             data->ntp_time_remote);
    }
}

extern "C" void video_process(void *cls, raop_ntp_t *ntp,
                              h264_decode_struct *data)
{
    if (video_renderer != NULL) {
        video_renderer->funcs->render_buffer(video_renderer, ntp, data->data,
                                             data->data_len, 0, 0);
    }
}

extern "C" void audio_flush(void *cls)
{
    if (audio_renderer)
        audio_renderer->funcs->flush(audio_renderer);
}

extern "C" void video_flush(void *cls)
{
    if (video_renderer)
        video_renderer->funcs->flush(video_renderer);
}

extern "C" void audio_set_volume(void *cls, float volume)
{
    if (audio_renderer != NULL) {
        audio_renderer->funcs->set_volume(audio_renderer, volume);
    }
}

extern "C" void log_callback(void *cls, int level, const char *msg)
{
    switch (level) {
    case LOGGER_DEBUG: {
        LOGD("%s", msg);
        break;
    }
    case LOGGER_WARNING: {
        LOGW("%s", msg);
        break;
    }
    case LOGGER_INFO: {
        LOGI("%s", msg);
        break;
    }
    case LOGGER_ERR: {
        LOGE("%s", msg);
        break;
    }
    default:
        break;
    }
}

int start_server(std::vector<char> hw_addr, std::string name, bool debug_log,
                 video_renderer_config_t const *video_config,
                 audio_renderer_config_t const *audio_config, int display_width,
                 int display_height, float display_framerate)
{
    raop_callbacks_t raop_cbs;
    memset(&raop_cbs, 0, sizeof(raop_cbs));
    raop_cbs.conn_init = conn_init;
    raop_cbs.conn_destroy = conn_destroy;
    raop_cbs.audio_process = audio_process;
    raop_cbs.video_process = video_process;
    raop_cbs.audio_flush = audio_flush;
    raop_cbs.video_flush = video_flush;
    raop_cbs.audio_set_volume = audio_set_volume;

    raop = raop_init(10, &raop_cbs);
    if (raop == NULL) {
        LOGE("Error initializing raop!");
        return -1;
    }

    raop_set_log_callback(raop, log_callback, NULL);
    raop_set_log_level(raop, debug_log ? RAOP_LOG_DEBUG : LOGGER_INFO);

    render_logger = logger_init();
    logger_set_callback(render_logger, log_callback, NULL);
    logger_set_level(render_logger, debug_log ? LOGGER_DEBUG : LOGGER_INFO);

    if (video_config->low_latency)
        logger_log(render_logger, LOGGER_INFO, "Using low-latency mode");

    if ((video_renderer = video_init_func(render_logger, video_config)) ==
        NULL) {
        LOGE("Could not init video renderer");
        return -1;
    }

    if (audio_config->device == AUDIO_DEVICE_NONE) {
        LOGI("Audio disabled");
    } else if ((audio_renderer = audio_init_func(render_logger, video_renderer,
                                                 audio_config)) == NULL) {
        LOGE("Could not init audio renderer");
        return -1;
    }

    if (video_renderer)
        video_renderer->funcs->start(video_renderer);
    if (audio_renderer)
        audio_renderer->funcs->start(audio_renderer);

    unsigned short port = 0;
    raop_start(raop, &port);
    raop_set_port(raop, port);
    //    raop_set_display(raop, display_width, display_height,
    //    display_framerate);

    int error;
    dnssd = dnssd_init(name.c_str(), strlen(name.c_str()), hw_addr.data(),
                       hw_addr.size(), &error);
    if (error) {
        LOGE("Could not initialize dnssd library!");
        return -2;
    }

    raop_set_dnssd(raop, dnssd);

    dnssd_register_raop(dnssd, port);
    dnssd_register_airplay(dnssd, port + 1);

    return 0;
}

int stop_server()
{
    raop_destroy(raop);
    dnssd_unregister_raop(dnssd);
    dnssd_unregister_airplay(dnssd);
    // If we don't destroy these two in the correct order, we get a deadlock
    // from the ilclient library
    if (audio_renderer)
        audio_renderer->funcs->destroy(audio_renderer);
    if (video_renderer)
        video_renderer->funcs->destroy(video_renderer);
    logger_destroy(render_logger);
    return 0;
}
