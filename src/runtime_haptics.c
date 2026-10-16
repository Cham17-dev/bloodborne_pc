/* DualSense haptics, as PS5 games drive them: an audio signal on the controller's own sound
 * device (USB: "Speakers (DualSense Wireless Controller)", 4 channels, the last two the left and
 * right actuators). The signal is made from the game's main audio port, its sound effects
 * without the music (that is the BGM port), so steps, rolls, swings, hits and landings are felt:
 * low-passed to the actuators' range, and only what is both sudden and centred passes. The
 * player's own sounds are in both channels alike; the world's (a creaking sign to one side, rain,
 * fire) are off to a side or steady, and buzzed constantly when let through. The game's own
 * vibration (scePadSetVibration) is mixed in as a low tone, since SDL's rumble would switch the
 * controller's audio haptics off. Tuned on recordings of Central Yharnam (BB_HAPTICS_DUMP).
 *
 * bbport.ini dualsense_haptics: strength in percent, 0 off (plain rumble, as before). Over
 * Bluetooth the controller has no sound device on Windows; it keeps the rumble. */
#define _GNU_SOURCE
#include "runtime.h"
#include "gpu/bbgpu.h"
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL3/SDL.h>

#define RATE 48000
#define CHANNELS 4 /* front left/right (the speaker, silent here), then the two actuators */
#define MAX_FRAMES 2048

static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static SDL_AudioStream *stream;
static SDL_AudioDeviceID device;
static uint64_t next_scan_ns;
static atomic_int dualsense_in_use; /* the controller the game reads is a DualSense */
static atomic_int rumble_large;     /* the game's vibration, 0..255 */
static atomic_int open_flag;        /* stream open: the rumble goes through it */

static uint64_t now_ns(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec;
}

/* Second-order sections (RBJ), one per actuator. */
typedef struct { float b0, b1, b2, a1, a2, z1, z2; } Biquad;
static void biquad_set(Biquad *q, int highpass, float freq) {
    const float w=2.f*3.14159265f*freq/RATE, c=cosf(w), alpha=sinf(w)/(2.f*0.70710678f), a0=1.f+alpha;
    const float side=highpass ? (1.f+c)/2.f : (1.f-c)/2.f;
    q->b0=side/a0; q->b1=(highpass ? -(1.f+c) : 1.f-c)/a0; q->b2=side/a0;
    q->a1=-2.f*c/a0; q->a2=(1.f-alpha)/a0;
}
static float biquad(Biquad *q, float x) {
    const float y=q->b0*x+q->z1;
    q->z1=q->b1*x-q->a1*y+q->z2;
    q->z2=q->b2*x-q->a2*y;
    return y;
}

typedef struct { Biquad low1, low2, high; float fast, slow; } Side;
static Side sides[2], centre;
static double phase;
static int filters_ready;

static void filters_init(void) {
    for (int s=0;s<2;++s) {
        memset(&sides[s],0,sizeof(sides[s]));
        biquad_set(&sides[s].low1,0,180.f); /* the actuators' range: below ~200 Hz */
        biquad_set(&sides[s].low2,0,180.f);
        biquad_set(&sides[s].high,1,30.f);  /* no DC or sub-bass drift */
    }
    memset(&centre,0,sizeof(centre));
    filters_ready=1;
}

/* Envelopes: fast (2 ms attack, 60 ms release) and slow (`slow` per sample). */
static void follow(Side *s, float level, float slow) {
    s->fast+=(level-s->fast)*(level>s->fast ? 0.0104f : 0.00035f);
    s->slow+=(level-s->slow)*slow;
}

/* One sample for both actuators from the game's sound at the left and right. */
static float shape(float left, float right) {
    float x[2]={left,right};
    for (int s=0;s<2;++s) {
        x[s]=biquad(&sides[s].high,biquad(&sides[s].low2,biquad(&sides[s].low1,x[s])));
        follow(&sides[s],fabsf(x[s]),0.00007f);
    }
    /* Centred: as loud on the left as on the right. */
    const float hi=fmaxf(sides[0].fast,sides[1].fast), lo=fminf(sides[0].fast,sides[1].fast);
    const float centred=hi>1e-5f ? lo/hi : 0.f;
    /* Sudden: well above its own level over the last second or so (a step, a roll, a hit). */
    const float mid=0.5f*(x[0]+x[1]);
    follow(&centre,fabsf(mid),0.00002f);
    float sudden=centre.fast>1e-5f ? (centre.fast-1.5f*centre.slow)/centre.fast : 0.f;
    sudden=sudden<0.f ? 0.f : sudden>1.f ? 1.f : sudden;
    return mid*centred*centred*sudden*sudden;
}

static void close_stream(void) {
    if (stream) SDL_DestroyAudioStream(stream);
    stream=NULL; device=0;
    atomic_store(&open_flag,0);
}

/* The DualSense's sound device, or 0. */
static SDL_AudioDeviceID find_device(void) {
    int count=0;
    SDL_AudioDeviceID *ids=SDL_GetAudioPlaybackDevices(&count), found=0;
    for (int i=0;ids && i<count && !found;++i) {
        const char *name=SDL_GetAudioDeviceName(ids[i]);
        SDL_AudioSpec spec;
        if (name && strstr(name,"DualSense") && SDL_GetAudioDeviceFormat(ids[i],&spec,NULL) && spec.channels==CHANNELS)
            found=ids[i];
    }
    SDL_free(ids);
    return found;
}

/* Opens or closes the stream as the setting and the controllers change; checked every 2 s. */
static void update_stream(int want) {
    const uint64_t now=now_ns();
    if (!want) { if (stream) { close_stream(); puts("Runtime: DualSense haptics off"); } return; }
    if (now<next_scan_ns) return;
    next_scan_ns=now+2000000000u;
    const SDL_AudioDeviceID found=find_device();
    if (stream && found!=device) { close_stream(); puts("Runtime: DualSense haptics: controller gone"); }
    if (stream || !found) return;
    const SDL_AudioSpec spec={SDL_AUDIO_F32,CHANNELS,RATE};
    stream=SDL_OpenAudioDeviceStream(found,&spec,NULL,NULL);
    if (!stream) { printf("Runtime: DualSense haptics unavailable (%s)\n",SDL_GetError()); return; }
    device=found;
    SDL_ResumeAudioStreamDevice(stream);
    if (!filters_ready) filters_init();
    atomic_store(&open_flag,1);
    printf("Runtime: DualSense haptics on (%s)\n",SDL_GetAudioDeviceName(found));
}

void runtime_haptics_feed(const float *stereo, int frames) {
    const int strength=bbgpu_haptics_strength();
    pthread_mutex_lock(&lock);
    update_stream(strength>0 && atomic_load(&dualsense_in_use));
    if (!stream || frames<=0 || frames>MAX_FRAMES) { pthread_mutex_unlock(&lock); return; }
    static float out[MAX_FRAMES*CHANNELS];
    /* Steps sit far below full scale, swings and hits near it: +26 dB at 100%, then a soft
     * limit, so a step is felt and a hit does not overpower it by much. */
    const float drive=20.f*(float)strength/100.f;
    const float rumble=(float)atomic_load(&rumble_large)/255.f;
    for (int f=0;f<frames;++f) {
        const float tone=rumble>0.f ? 0.8f*rumble*(float)sin(phase) : 0.f;
        phase+=2.0*3.14159265358979*55.0/RATE; /* the game's motor as a 55 Hz hum */
        if (phase>2.0*3.14159265358979) phase-=2.0*3.14159265358979;
        float *o=&out[f*CHANNELS];
        o[0]=o[1]=0.f;
        o[2]=o[3]=tanhf(drive*shape(stereo[f*2],stereo[f*2+1])+tone);
    }
    /* Keep it close to the sound: at most ~40 ms queued (a stall drops the backlog), and a
     * little silence ahead when it runs dry. */
    const int bytes=frames*CHANNELS*(int)sizeof(float), queued=SDL_GetAudioStreamQueued(stream);
    if (queued>8*bytes) SDL_ClearAudioStream(stream);
    else if (queued<bytes) {
        static const float silence[MAX_FRAMES*CHANNELS];
        SDL_PutAudioStreamData(stream,silence,bytes);
    }
    SDL_PutAudioStreamData(stream,out,bytes);
    /* BB_HAPTICS_DUMP=<file>: what the actuators get (f32, 4 channels, 48 kHz), for tuning. */
    static FILE *dump; static int dump_checked;
    if (!dump_checked) { dump_checked=1; const char *path=getenv("BB_HAPTICS_DUMP"); if (path && path[0]) dump=fopen(path,"wb"); }
    if (dump) fwrite(out,1,(size_t)bytes,dump);
    pthread_mutex_unlock(&lock);
}

int runtime_haptics_rumble(int dualsense, uint8_t large) {
    atomic_store(&dualsense_in_use,dualsense);
    atomic_store(&rumble_large,large);
    return dualsense && atomic_load(&open_flag);
}
