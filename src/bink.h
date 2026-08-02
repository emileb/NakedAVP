#ifndef BINK_H
#define BINK_H

/* Bink playback via FFmpeg, for the files libsmacker cannot touch: AvP Gold's
 * intros/outros, the animated menu backdrop and the 15 music tracks (which are
 * .bik with a 4x4 dummy video stream carrying the soundtrack).
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BINKSTREAM BINKSTREAM;

/* filename is relative to the game data dir, e.g. "FMVs/MarineIntro.bik".
 * wantVideo == 0 skips video entirely, which is what the music tracks want. */
extern BINKSTREAM *Bink_Open(const char *filename, int wantVideo);
extern void Bink_Close(BINKSTREAM *bink);

extern int Bink_HasVideo(BINKSTREAM *bink);
extern int Bink_HasAudio(BINKSTREAM *bink);
extern int Bink_VideoWidth(BINKSTREAM *bink);
extern int Bink_VideoHeight(BINKSTREAM *bink);
/* seconds per frame in ONE_FIXED units; 0 when there is no video */
extern int Bink_FrameTime(BINKSTREAM *bink);

/* Decode until a video frame is ready or audio has been buffered. Returns 0 at
 * end of stream. Decoded audio accumulates internally; drain it with
 * Bink_TakeAudio. Set *gotVideo non-zero when a new picture is available. */
extern int Bink_Decode(BINKSTREAM *bink, int *gotVideo);

/* Hand back up to maxBytes of decoded interleaved S16 audio, 0 when empty. */
extern unsigned long Bink_TakeAudio(BINKSTREAM *bink, unsigned char *dest, unsigned long maxBytes);
extern int Bink_AudioRate(BINKSTREAM *bink);
extern int Bink_AudioChannels(BINKSTREAM *bink);

/* Convert the current picture to RGB565 and write it into dest, centred in a
 * destWidth x destHeight buffer with the surrounding area blacked out. */
extern void Bink_BlitRGB565(BINKSTREAM *bink, void *dest, int destWidth, int destHeight, int destPitch);

/* Restart from the beginning, for looping music. */
extern int Bink_Rewind(BINKSTREAM *bink);

#ifdef __cplusplus
}
#endif

#endif
