/* DualSense haptics, as PS5 games drive them: an audio signal on the controller's own sound
 * device (USB: "Speakers (DualSense Wireless Controller)", 4 channels, the last two the left and
 * right actuators). The signal is the centre channel of the game's main audio port: the game
 * plays the player's own sounds there (steps, rolls, swings, hits) and the world's in the other
 * channels, so the world (wind, rain, fire, a creaking sign) is left out. Low-passed to the
 * actuators' range, with a soft gate under the level of the player's quiet sounds (cloth,
 * breath). The game's own vibration (scePadSetVibration) is mixed in as a low tone, since SDL's
 * rumble would switch the controller's audio haptics off. Tuned on recordings of Central
 * Yharnam (BB_AUDIO_DUMP, BB_HAPTICS_DUMP).
 *
 * Threads: the game's audio thread only copies the samples into a ring; the filter runs in SDL's
 * thread for the controller's sound device (the stream's callback), and a thread of its own opens
 * and closes that device every second, as the setting and the controllers change.
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
#include <SDL3/SDL.h>

#define RATE 48000
#define CHANNELS 4  /* front left/right (the speaker, silent here), then the two actuators */
#define RING 16384  /* samples, a power of two: ~340 ms */

/* Game audio thread -> the controller's device thread. */
static float ring[RING];
static atomic_uint ring_write, ring_read;

static atomic_int dualsense_in_use; /* the controller the game reads is a DualSense */
static atomic_int rumble_large;     /* the game's vibration, 0..255 */
static atomic_int open_flag;        /* stream open: the rumble goes through it */
static atomic_int strength;         /* percent, read by the device thread */

/* Second-order sections (RBJ). */
typedef struct { float b0, b1, b2, a1, a2, z1, z2; } Biquad;
static void biquad_set(Biquad *q, int highpass, float freq) {
    const float w=2.f*3.14159265f*freq/RATE, c=cosf(w), alpha=sinf(w)/(2.f*0.70710678f), a0=1.f+alpha;
    const float side=highpass ? (1.f+c)/2.f : (1.f-c)/2.f;
    q->b0=side/a0; q->b1=(highpass ? -(1.f+c) : 1.f-c)/a0; q->b2=side/a0;
    q->a1=-2.f*c/a0; q->a2=(1.f-alpha)/a0; q->z1=q->z2=0.f;
}
static float biquad(Biquad *q, float x) {
    const float y=q->b0*x+q->z1;
    q->z1=q->b1*x-q->a1*y+q->z2;
    q->z2=q->b2*x-q->a2*y;
    return y;
}

/* Device thread only. */
static Biquad low1, low2, high;
static float envelope;
static double phase;
static FILE *dump;

static void filters_reset(void) {
    biquad_set(&low1,0,180.f); /* the actuators' range: below ~200 Hz */
    biquad_set(&low2,0,180.f);
    biquad_set(&high,1,30.f);  /* no DC or sub-bass drift */
    envelope=0.f;
}

static float shape(float x) {
    x=biquad(&high,biquad(&low2,biquad(&low1,x)));
    /* Envelope: 2 ms attack, 60 ms release. Below the gate (the player's breath and cloth, a
     * little of the world) nothing; above it, the part over the gate. */
    const float level=fabsf(x), gate=0.005f;
    envelope+=(level-envelope)*(level>envelope ? 0.0104f : 0.00035f);
    return envelope>gate ? x*(envelope-gate)/envelope : 0.f;
}

/* SDL's thread for the controller's device asks for more: the ring's samples through the filter. */
static void SDLCALL produce(void *userdata, SDL_AudioStream *stream, int additional, int total) {
    (void)userdata; (void)total;
    static float out[1024*CHANNELS];
    int frames=additional/(int)(CHANNELS*sizeof(float));
    unsigned read=atomic_load(&ring_read);
    const unsigned write=atomic_load_explicit(&ring_write,memory_order_acquire);
    /* Close to the sound: more than 50 ms behind (a stall), skip to 10 ms behind. */
    if (write-read>RATE/20) read=write-RATE/100;
    const float drive=40.f*(float)atomic_load(&strength)/100.f;
    const float rumble=(float)atomic_load(&rumble_large)/255.f;
    while (frames>0) {
        const int n=frames<1024 ? frames : 1024;
        for (int f=0;f<n;++f) {
            const float x=read!=write ? ring[read++%RING] : 0.f;
            const float tone=rumble>0.f ? 0.8f*rumble*(float)sin(phase) : 0.f;
            phase+=2.0*3.14159265358979*55.0/RATE; /* the game's motor as a 55 Hz hum */
            if (phase>2.0*3.14159265358979) phase-=2.0*3.14159265358979;
            float *o=&out[f*CHANNELS];
            o[0]=o[1]=0.f;
            o[2]=o[3]=tanhf(drive*shape(x)+tone);
        }
        SDL_PutAudioStreamData(stream,out,n*CHANNELS*(int)sizeof(float));
        if (dump) fwrite(out,sizeof(float),(size_t)n*CHANNELS,dump);
        frames-=n;
    }
    atomic_store(&ring_read,read);
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

/* Opens and closes the controller's stream as the setting and the controllers change. */
static void *watch(void *arg) {
    (void)arg;
    SDL_AudioStream *stream=NULL;
    SDL_AudioDeviceID device=0;
    const char *path=getenv("BB_HAPTICS_DUMP");
    if (path && path[0]) dump=fopen(path,"wb");
    for (unsigned tick=0;;++tick,SDL_Delay(1000)) {
        const int level=bbgpu_haptics_strength();
        atomic_store(&strength,level);
        const int want=level>0 && atomic_load(&dualsense_in_use);
        if (!want) {
            if (stream) {
                atomic_store(&open_flag,0);
                SDL_DestroyAudioStream(stream);
                stream=NULL; device=0;
                puts("Runtime: DualSense haptics off");
            }
            continue;
        }
        if (stream && tick%3) continue; /* still there? checked every 3 s */
        const SDL_AudioDeviceID found=find_device();
        if (stream && found==device) continue;
        if (stream) {
            atomic_store(&open_flag,0);
            SDL_DestroyAudioStream(stream);
            stream=NULL; device=0;
            puts("Runtime: DualSense haptics: controller gone");
        }
        if (!found) continue;
        filters_reset();
        atomic_store(&ring_read,atomic_load(&ring_write));
        const SDL_AudioSpec spec={SDL_AUDIO_F32,CHANNELS,RATE};
        stream=SDL_OpenAudioDeviceStream(found,&spec,produce,NULL);
        if (!stream) { printf("Runtime: DualSense haptics unavailable (%s)\n",SDL_GetError()); continue; }
        device=found;
        SDL_ResumeAudioStreamDevice(stream);
        atomic_store(&open_flag,1);
        printf("Runtime: DualSense haptics on (%s)\n",SDL_GetAudioDeviceName(found));
    }
    return NULL;
}

void runtime_haptics_feed(const float *centre, int frames) {
    static atomic_int started;
    if (!atomic_exchange(&started,1)) {
        pthread_t thread;
        if (!pthread_create(&thread,NULL,watch,NULL)) pthread_detach(thread);
    }
    if (!atomic_load(&open_flag)) return;
    unsigned write=atomic_load(&ring_write);
    for (int f=0;f<frames;++f) ring[write++%RING]=centre[f];
    atomic_store_explicit(&ring_write,write,memory_order_release);
}

int runtime_haptics_rumble(int dualsense, uint8_t large) {
    atomic_store(&dualsense_in_use,dualsense);
    atomic_store(&rumble_large,large);
    return dualsense && atomic_load(&open_flag);
}
