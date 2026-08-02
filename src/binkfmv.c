/* Full screen Bink playback: the species intros/outros, the Fox/Rebellion
 * logos and the animated menu backdrop.
 *
 * Everything is drawn into the 640x480 RGB565 software surface that
 * FlipBuffers() already uploads as a full screen texture, which is the same
 * path the rest of the front end uses.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "fixer.h"
#include "3dc.h"
#include "platform.h"
#include "bink.h"
#include "audio_stream.h"

#define BINK_SURFACE_WIDTH  640
#define BINK_SURFACE_HEIGHT 480

/* how much decoded sound to keep queued while a movie plays */
#define BINK_AUDIO_TARGET_SECONDS 1
#define BINK_AUDIO_CHUNK 16384

extern SDL_Surface *surface;
extern unsigned char GotAnyKey;
extern int IntroOutroMoviesAreActive;
extern void CheckForWindowsMessages(void);
extern void ClearScreenToBlack(void);

static BINKSTREAM *MenuBackground;
static int MenuBackgroundFrameTime;
static int MenuBackgroundTimer;
static unsigned int MenuBackgroundLastTick;

static unsigned char BinkAudioChunk[BINK_AUDIO_CHUNK];

/* Move whatever the decoder has produced into the sound stream. */
static void FeedBinkAudio(BINKSTREAM *bink)
{
	unsigned long target;

	if (!Bink_HasAudio(bink))
	{
		return;
	}

	AudioStream_Update(AUDIO_STREAM_BINK);

	target = (unsigned long)Bink_AudioRate(bink) * Bink_AudioChannels(bink)
	         * 2 * BINK_AUDIO_TARGET_SECONDS;

	while (AudioStream_CanQueue(AUDIO_STREAM_BINK)
		&& AudioStream_QueuedBytes(AUDIO_STREAM_BINK) < target)
	{
		unsigned long got = Bink_TakeAudio(bink, BinkAudioChunk, sizeof(BinkAudioChunk));

		if (got == 0)
		{
			break;
		}

		AudioStream_Queue(AUDIO_STREAM_BINK, BinkAudioChunk, got);
	}
}

static void BlitBinkToSurface(BINKSTREAM *bink)
{
	if (surface == NULL)
	{
		return;
	}

	if (SDL_MUSTLOCK(surface))
	{
		if (!SDL_LockSurface(surface))
		{
			return;
		}
	}

	Bink_BlitRGB565(bink, surface->pixels, BINK_SURFACE_WIDTH, BINK_SURFACE_HEIGHT,
	                surface->pitch);

	if (SDL_MUSTLOCK(surface))
	{
		SDL_UnlockSurface(surface);
	}
}

void PlayBinkedFMV(char *filenamePtr)
{
	BINKSTREAM *bink;
	unsigned int startTick;
	int frameTimeMs;
	long frameIndex = 0;
	int finished = 0;

	if (!IntroOutroMoviesAreActive)
	{
		return;
	}

	bink = Bink_Open(filenamePtr, 1);
	if (bink == NULL)
	{
		fprintf(stderr, "Bink: could not open %s\n", filenamePtr);
		return;
	}

	if (!Bink_HasVideo(bink))
	{
		Bink_Close(bink);
		return;
	}

	if (Bink_HasAudio(bink))
	{
		AudioStream_Open(AUDIO_STREAM_BINK, Bink_AudioRate(bink),
		                 Bink_AudioChannels(bink), 16);
		AudioStream_SetGain(AUDIO_STREAM_BINK, 1.0f);
	}

	frameTimeMs = (Bink_FrameTime(bink) * 1000) / ONE_FIXED;
	if (frameTimeMs <= 0)
	{
		frameTimeMs = 66;
	}

	GotAnyKey = 0;
	startTick = SDL_GetTicks();

	while (!finished)
	{
		long wantFrame;

		CheckForWindowsMessages();

		/* A tap, a key or Back all skip, as the original stopped on any key.
		 * GotAnyKey only sees the mouse while relative mode is on, which it is
		 * not in the menus, so a touch is checked for separately - SDL turns
		 * one into mouse button 1 on Android. */
		if (GotAnyKey || (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_MASK(1)))
		{
			break;
		}

		FeedBinkAudio(bink);

		/* catch the picture up to the wall clock, decoding audio on the way */
		wantFrame = (long)((SDL_GetTicks() - startTick) / (unsigned int)frameTimeMs);

		while (frameIndex <= wantFrame)
		{
			int gotVideo = 0;

			if (!Bink_Decode(bink, &gotVideo))
			{
				finished = 1;
				break;
			}

			if (gotVideo)
			{
				frameIndex++;
			}
		}

		BlitBinkToSurface(bink);
		FlipBuffers();
	}

	/* let the sound stop with the picture rather than playing on over the menu */
	AudioStream_Close(AUDIO_STREAM_BINK);
	Bink_Close(bink);

	ClearScreenToBlack();
	FlipBuffers();

	GotAnyKey = 0;
}

void StartMenuBackgroundBink()
{
	if (MenuBackground)
	{
		return;
	}

	MenuBackground = Bink_Open("FMVs/Menubackground.bik", 1);
	if (MenuBackground == NULL)
	{
		return;
	}

	if (!Bink_HasVideo(MenuBackground))
	{
		Bink_Close(MenuBackground);
		MenuBackground = NULL;
		return;
	}

	MenuBackgroundFrameTime = Bink_FrameTime(MenuBackground);
	MenuBackgroundTimer = 0;
	MenuBackgroundLastTick = SDL_GetTicks();
}

/* Returns non-zero when it has drawn the backdrop, so the caller falls back to
 * the still image when there is no movie. */
int PlayMenuBackgroundBink()
{
	unsigned int now;
	int elapsedMs;

	if (MenuBackground == NULL)
	{
		return 0;
	}

	now = SDL_GetTicks();
	elapsedMs = (int)(now - MenuBackgroundLastTick);
	MenuBackgroundLastTick = now;

	/* menus can sit for a long time between frames; never sprint to catch up */
	if (elapsedMs > 250)
	{
		elapsedMs = 250;
	}

	MenuBackgroundTimer += (elapsedMs * ONE_FIXED) / 1000;

	while (MenuBackgroundTimer >= MenuBackgroundFrameTime)
	{
		int gotVideo = 0;

		MenuBackgroundTimer -= MenuBackgroundFrameTime;

		if (!Bink_Decode(MenuBackground, &gotVideo))
		{
			/* the backdrop is a short loop */
			if (!Bink_Rewind(MenuBackground))
			{
				Bink_Close(MenuBackground);
				MenuBackground = NULL;
				return 0;
			}
			continue;
		}

		if (!gotVideo)
		{
			MenuBackgroundTimer += MenuBackgroundFrameTime;
			break;
		}
	}

	BlitBinkToSurface(MenuBackground);

	return 1;
}

void EndMenuBackgroundBink()
{
	if (MenuBackground)
	{
		Bink_Close(MenuBackground);
		MenuBackground = NULL;
	}
}
