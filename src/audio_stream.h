#ifndef AUDIO_STREAM_H
#define AUDIO_STREAM_H

/* Queued-buffer audio streams, for content that is decoded as it plays rather
 * than loaded whole like every ACTIVESOUNDSAMPLE. Implemented by the sound
 * backend (openal.c) because it owns the AL device and context.
 *
 * Several run at once: a Smacker plot movie on a video screen (fmv.c), the Bink
 * music track (cdplayer.c) and a full screen Bink intro (binkfmv.c), so each
 * stream is addressed by id.
 */

#define AUDIO_STREAM_FMV	0
#define AUDIO_STREAM_MUSIC	1
#define AUDIO_STREAM_BINK	2
#define AUDIO_STREAM_COUNT	3

/* Smacker front-loads about a second of audio into frame 0 and then delivers
 * one frame's worth per frame, so the FMV queue sits ~17 buffers deep. */
#define AUDIO_STREAM_BUFFERS 32

#ifdef __cplusplus
extern "C" {
#endif

/* bitdepth is 8 or 16, channels 1 or 2. Returns 1 if the stream was opened. */
extern int AudioStream_Open(int id, int rate, int channels, int bitdepth);
extern void AudioStream_Close(int id);

/* Recycles buffers the hardware has finished with, and restarts the source if
 * a stall drained the queue. Call once per frame while the stream is playing. */
extern void AudioStream_Update(int id);

/* Non-zero if Queue() has a free buffer to write into. */
extern int AudioStream_CanQueue(int id);
extern void AudioStream_Queue(int id, const unsigned char *data, unsigned long size);

/* Non-zero until the queue has drained. */
extern int AudioStream_IsPlaying(int id);

/* How much sound is queued up but not yet played, in bytes. */
extern unsigned long AudioStream_QueuedBytes(int id);

/* gain is 0.0 .. 1.0 */
extern void AudioStream_SetGain(int id, float gain);

#ifdef __cplusplus
}
#endif

#endif
