#ifndef FMV_AUDIO_H
#define FMV_AUDIO_H

/* Streaming audio for Smacker FMV playback. Implemented by the sound backend
 * (openal.c) because it owns the AL device and context; driven from fmv.c.
 */

/* Smacker front-loads about a second of audio into frame 0 and then delivers
 * one frame's worth per frame, so the queue sits ~17 buffers deep. */
#define FMV_SOUND_BUFFERS 32

/* Max of the SmackerSoundVolume menu slider (avp_menudata.c), == ONE_FIXED/512 */
#define FMV_SOUND_VOLUME_MAX 128

#ifdef __cplusplus
extern "C" {
#endif

/* bitdepth is 8 or 16, channels 1 or 2. Returns 1 if a stream was opened. */
extern int FMVSound_Open(int rate, int channels, int bitdepth);
extern void FMVSound_Close(void);

/* Recycles buffers the hardware has finished with, and restarts the source if
 * a stall drained the queue. Call once per frame while a movie is playing. */
extern void FMVSound_Update(void);

/* Non-zero if Queue() has a free buffer to write into. */
extern int FMVSound_CanQueue(void);
extern void FMVSound_Queue(const unsigned char *data, unsigned long size);

/* Non-zero until the queue has drained: the video holds its last frame on it. */
extern int FMVSound_IsPlaying(void);

/* volume is SmackerSoundVolume, 0..FMV_SOUND_VOLUME_MAX */
extern void FMVSound_SetVolume(int volume);

#ifdef __cplusplus
}
#endif

#endif
