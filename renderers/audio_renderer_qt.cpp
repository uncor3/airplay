/**
 * Qt Audio Renderer for AirPlay
 * Uses Qt Multimedia for audio output and FFmpeg for AAC decoding
 */

#include "audio_renderer.h"
#include <QAudioSink>
#include <QAudioFormat>
#include <QIODevice>
#include <QMediaFormat>
#include <QAudioDecoder>
#include <QBuffer>
#include <QCoreApplication>
#include <memory>

typedef struct audio_renderer_qt_s {
    audio_renderer_t base;
    QAudioSink *audio_sink;
    QIODevice *audio_device;
    QAudioDecoder *decoder;
    QAudioFormat audio_format;
    QBuffer *input_buffer;
    QByteArray pending_data;
} audio_renderer_qt_t;

static void audio_renderer_qt_start(audio_renderer_t *renderer) {
    audio_renderer_qt_t *r = (audio_renderer_qt_t *)renderer;
    if (r->audio_sink) {
        r->audio_device = r->audio_sink->start();
    }
}

static void audio_renderer_qt_render_buffer(audio_renderer_t *renderer, raop_ntp_t *ntp, 
                                           unsigned char *data, int data_len, uint64_t pts) {
    audio_renderer_qt_t *r = (audio_renderer_qt_t *)renderer;
    
    // Create buffer from AAC data
    QByteArray aac_data((const char*)data, data_len);
    r->input_buffer->setData(aac_data);
    r->input_buffer->open(QIODevice::ReadOnly);
    
    // Set source for decoder
    // r->decoder->setSource(r->input_buffer);
    // r->decoder->start();
}

static void audio_renderer_qt_flush(audio_renderer_t *renderer) {
    audio_renderer_qt_t *r = (audio_renderer_qt_t *)renderer;
    if (r->decoder) {
        r->decoder->stop();
    }
    r->pending_data.clear();
}

static void audio_renderer_qt_destroy(audio_renderer_t *renderer) {
    audio_renderer_qt_t *r = (audio_renderer_qt_t *)renderer;
    if (r->audio_sink) {
        r->audio_sink->stop();
        delete r->audio_sink;
    }
    if (r->decoder) {
        delete r->decoder;
    }
    if (r->input_buffer) {
        delete r->input_buffer;
    }
    free(renderer);
}

static void audio_renderer_qt_set_volume(audio_renderer_t *renderer, float volume) {
    audio_renderer_qt_t *r = (audio_renderer_qt_t *)renderer;
    if (r->audio_sink) {
        r->audio_sink->setVolume(volume);
    }
}

static const audio_renderer_funcs_t audio_renderer_qt_funcs = {
    .start = audio_renderer_qt_start,
    .render_buffer = audio_renderer_qt_render_buffer,
    .set_volume = audio_renderer_qt_set_volume,
    .flush = audio_renderer_qt_flush,
    .destroy = audio_renderer_qt_destroy,
};

extern "C" audio_renderer_t *audio_renderer_qt_init(logger_t *logger, 
                                                    video_renderer_t *video_renderer,
                                                    audio_renderer_config_t const *config) {
    audio_renderer_qt_t *renderer = (audio_renderer_qt_t *)calloc(1, sizeof(audio_renderer_qt_t));
    
    // Setup audio format for 44.1kHz 16-bit stereo (typical AirPlay format)
    renderer->audio_format.setSampleRate(44100);
    renderer->audio_format.setChannelCount(2);
    renderer->audio_format.setSampleFormat(QAudioFormat::Int16);
    
    // Create audio sink
    renderer->audio_sink = new QAudioSink(renderer->audio_format);
    renderer->input_buffer = new QBuffer();
    
    // Create decoder for AAC
    renderer->decoder = new QAudioDecoder();
    
    // Connect decoder signals
    QObject::connect(renderer->decoder, &QAudioDecoder::bufferReady, [renderer]() {
        QAudioBuffer buffer = renderer->decoder->read();
        if (renderer->audio_device && buffer.isValid()) {
            const char* data = buffer.constData<char>();
            int size = buffer.byteCount();
            renderer->audio_device->write(data, size);
        }
    });
    
    renderer->base.logger = logger;
    renderer->base.funcs = &audio_renderer_qt_funcs;
    renderer->base.type = AUDIO_RENDERER_QT;
    
    return &renderer->base;
}
