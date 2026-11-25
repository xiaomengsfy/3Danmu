#include "audio_tempo.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define HOP 512
#define WINDOW (HOP * 2)
#define SEARCH 256
#define CAPACITY 8192

struct AudioTempo {
    int16_t input[CAPACITY * 2];
    int16_t overlap[HOP * 2];
    size_t count;
    uint64_t base, total;
    double position;
    bool have_overlap, finished;
};

AudioTempo *audio_tempo_create(void) { return calloc(1, sizeof(AudioTempo)); }
void audio_tempo_destroy(AudioTempo *t) { free(t); }
void audio_tempo_reset(AudioTempo *t) { if (t) memset(t, 0, sizeof(*t)); }
size_t audio_tempo_space(const AudioTempo *t) { return t && !t->finished ? CAPACITY-t->count : 0; }

size_t audio_tempo_push(AudioTempo *t, const int16_t *pcm, size_t frames) {
    size_t space = audio_tempo_space(t);
    if (frames > space) frames = space;
    if (frames && pcm) {
        memcpy(t->input+t->count*2, pcm, frames*2*sizeof(*pcm));
        t->count += frames;
        t->total += frames;
        return frames;
    }
    return 0;
}

static int16_t sample(const AudioTempo *t, uint64_t frame, int channel) {
    if (frame < t->base || frame >= t->total) return 0;
    return t->input[(size_t)(frame-t->base)*2+channel];
}

static uint64_t difference(const AudioTempo *t, uint64_t start) {
    uint64_t sum = 0;
    /* Use both channels with one shared offset: anti-phase and uncorrelated
     * stereo cannot cancel in the matching signal or acquire different timing.
     * Four-frame decimation bounds work on the ARM11 without NEON. */
    for (int i=0; i<HOP; i+=4) for (int c=0;c<2;c++) {
        int32_t d = (int32_t)t->overlap[i*2+c] - sample(t,start+i,c);
        sum += (uint64_t)((int64_t)d*d);
    }
    return sum;
}

size_t audio_tempo_read(AudioTempo *t, int16_t *pcm, size_t capacity,
                       int speed, double *source_frames) {
    if (source_frames) *source_frames=0;
    if (!t || !pcm || !capacity || audio_tempo_drained(t)) return 0;
    if (speed!=100 && speed!=125 && speed!=150 && speed!=200) speed=100;
    double rate=speed/100.0;
    uint64_t target=(uint64_t)t->position;
    /* Keep enough lookahead for every candidate and its saved overlap. This
     * condition never consumes anything, allowing arbitrary producer chunks. */
    if (!t->finished && target+WINDOW+SEARCH > t->total) return 0;
    double remaining=t->total-t->position;
    size_t n=HOP;
    if (t->finished && remaining < HOP*rate) {
        n=(size_t)(remaining/rate);
        if (n*rate < remaining-0.0000001) n++;
    }
    if (capacity<n) return 0;
    uint64_t lo=target>SEARCH ? target-SEARCH : 0;
    if (lo<t->base) lo=t->base;
    uint64_t hi=target+SEARCH;
    /* At EOF prefer a full final window when possible. Tiny inputs are padded
     * only for overlap storage; output length is still exactly source/rate. */
    uint64_t need=t->finished ? n : WINDOW;
    uint64_t last=t->total>need ? t->total-need : 0;
    if (hi>last) hi=last;
    if (hi<lo) hi=lo;
    uint64_t best=target;
    if (best<lo) best=lo;
    if (best>hi) best=hi;
    if (t->have_overlap) {
        uint64_t score=difference(t,best);
        /* Exact continuation at unity is lossless and needs no search. */
        if (score) {
            /* Coarse search, then sample-accurate refinement. +/-256 spans
             * one complete period down to 64Hz, while keeping ARM11 work
             * below an exhaustive +/-128 search. */
            for (uint64_t at=lo; at<=hi; at+=4) {
                uint64_t d=difference(t,at);
                if (d<score) { best=at; score=d; }
            }
            uint64_t fine_lo=best>3 ? best-3 : 0, fine_hi=best+3;
            if (fine_lo<lo) fine_lo=lo;
            if (fine_hi>hi) fine_hi=hi;
            for (uint64_t at=fine_lo;at<=fine_hi;at++) {
                uint64_t d=difference(t,at);
                if (d<score) { best=at; score=d; }
            }
        }
    }
    for (size_t i=0;i<n;i++) for (int c=0;c<2;c++) {
        int32_t value=sample(t,best+i,c);
        if (t->have_overlap)
            value=((HOP-(int)i)*(int32_t)t->overlap[i*2+c]+(int)i*value)/HOP;
        pcm[i*2+c]=(int16_t)value;
    }
    for (int i=0;i<HOP;i++) for (int c=0;c<2;c++)
        t->overlap[i*2+c]=sample(t,best+HOP+i,c);
    t->have_overlap=true;
    double advance=n*rate;
    if (advance>remaining) advance=remaining;
    t->position+=advance;
    if (source_frames) *source_frames=advance;
    /* Retain the next search range, never a pointer into a moved queue. */
    uint64_t keep=(uint64_t)t->position;
    keep=keep>SEARCH ? keep-SEARCH : 0;
    if (keep>t->total) keep=t->total;
    if (keep>t->base) {
        size_t discard=(size_t)(keep-t->base);
        memmove(t->input,t->input+discard*2,(t->count-discard)*2*sizeof(int16_t));
        t->count-=discard;
        t->base=keep;
    }
    return n;
}

void audio_tempo_finish(AudioTempo *t) { if (t) t->finished=true; }
bool audio_tempo_drained(const AudioTempo *t) {
    return !t || (t->finished && t->position >= t->total-0.0000001);
}
