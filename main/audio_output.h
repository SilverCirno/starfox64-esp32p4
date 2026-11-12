#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Stereo signed 16-bit interleaved PCM. A sink must consume/copy samples before
// returning; buffers remain producer-owned. Register from the audio task only.
typedef bool (*sf64_pcm_sink_fn)(void *context, const int16_t *samples,
                                size_t frames, unsigned sample_rate);
typedef enum {
    SF64_PCM_OK, SF64_PCM_NO_SINK, SF64_PCM_INVALID, SF64_PCM_BACKPRESSURE
} sf64_pcm_status_t;
void sf64_audio_set_sink(sf64_pcm_sink_fn sink, void *context);
sf64_pcm_status_t sf64_audio_submit(const int16_t *samples, size_t frames, unsigned sample_rate);
#ifdef __cplusplus
}
#endif
