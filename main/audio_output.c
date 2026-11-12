#include "audio_output.h"
static sf64_pcm_sink_fn s_sink;
static void *s_context;
void sf64_audio_set_sink(sf64_pcm_sink_fn sink, void *context) {
    s_sink=sink;
    s_context=context;
}
sf64_pcm_status_t sf64_audio_submit(const int16_t *samples, size_t frames, unsigned sample_rate) {
    if (!samples || !frames || frames>8192 || sample_rate<8000 || sample_rate>48000)
        return SF64_PCM_INVALID;
    if (!s_sink) return SF64_PCM_NO_SINK;
    return s_sink(s_context,samples,frames,sample_rate) ? SF64_PCM_OK : SF64_PCM_BACKPRESSURE;
}
