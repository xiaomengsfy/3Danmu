#ifndef AUDIO_TEMPO_H
#define AUDIO_TEMPO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Fixed-memory, linked-stereo WSOLA. All counts are stereo PCM16 frames.
 * Single worker owns the object. Push may accept only a prefix: retain the
 * remainder and retry after read. No allocations occur after create.
 * read emits at most 512 frames, and consumes no input if capacity is <512
 * (except a final shorter fragment). source_frames is original content time,
 * including a fractional last frame; use it for the playback clock.
 * Speed changes take effect at the next output grain; already queued PCM and
 * its clock metadata remain valid. The sample rate must stay unchanged. */
typedef struct AudioTempo AudioTempo;
AudioTempo *audio_tempo_create(void);
void audio_tempo_destroy(AudioTempo *tempo);
void audio_tempo_reset(AudioTempo *tempo);
size_t audio_tempo_space(const AudioTempo *tempo);
size_t audio_tempo_push(AudioTempo *tempo, const int16_t *pcm, size_t frames);
size_t audio_tempo_read(AudioTempo *tempo, int16_t *pcm, size_t capacity,
                       int speed_percent, double *source_frames);
void audio_tempo_finish(AudioTempo *tempo);
bool audio_tempo_drained(const AudioTempo *tempo);

#endif
