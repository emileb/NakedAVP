/* KJL 15:25:20 8/16/97
 *
 * smacker.c - functions to handle FMV playback
 *
 */
#include "3dc.h"
#include "module.h"
#include "inline.h"
#include "stratdef.h"
#include "gamedef.h"
#include "fmv.h"
#include "fmv_audio.h"
#include "files.h"
#include "avp_menus.h"
#include "avp_userprofile.h"
#include "oglfunc.h" // move this into opengl.c
#include "libsmacker/smacker.h"

#define UseLocalAssert 1
#include "ourasert.h"

int VolumeOfNearestVideoScreen;
int PanningOfNearestVideoScreen;

extern char *ScreenBuffer;
extern int GotAnyKey;
extern int NormalFrameTime;
extern void DirectReadKeyboard(void);
extern IMAGEHEADER ImageHeaderArray[];
#if MaxImageGroups>1
extern int NumImagesArray[];
#else
extern int NumImages;
#endif

void PlayFMV(char *filenamePtr);

void FindLightingValueFromFMV(unsigned short *bufferPtr);
static void FindLightingValuesFromFMVFrame(unsigned char *bufferPtr, FMVTEXTURE *ftPtr, unsigned long pixels);

int SmackerSoundVolume=ONE_FIXED/512;
int MoviesAreActive;
int IntroOutroMoviesAreActive=1;

int FmvColourRed;
int FmvColourGreen;
int FmvColourBlue;

void ReleaseFMVTexture(FMVTEXTURE *ftPtr);

/* libsmacker signals errors by returning -1 in a plain char, which is unsigned
 * on ARM - every result has to come back through a signed type to compare. */
#define SMK_RESULT(call) ((signed char)(call))

/* SetupFMVTexture's buffers are sized for this. The game's own FMVs are all
 * 128x96; refuse anything larger rather than overrun them. */
#define FMV_MAX_WIDTH  128
#define FMV_MAX_HEIGHT 128

/* size of the TV static shown when a screen has no movie to play */
#define FMV_STATIC_WIDTH  128
#define FMV_STATIC_HEIGHT 96

/* most frames a movie may decode in one update, so a hitch costs dropped time
 * rather than a burst of fast-forward */
#define FMV_MAX_CATCHUP_FRAMES 4

/* KJL 12:45:23 10/08/98 - FMVTEXTURE stuff */
#define MAX_NO_FMVTEXTURES 10
FMVTEXTURE FMVTexture[MAX_NO_FMVTEXTURES];
int NumberOfFMVTextures;

/* A plot message plays on every triggered screen at once, so they share one
 * decoder rather than opening the same file once per screen. */
static struct
{
	smk Handle;
	int MessageNumber;
	int Started;
	int Ended;
	unsigned long Width;
	unsigned long Height;
	unsigned long FrameCount;
	unsigned long FrameIndex;
	int FrameTime;			/* fixed point seconds per frame */
	int FrameTimer;
	int HasAudio;
	unsigned long Serial;
} TriggeredFMV;

/* Frames are stamped with this so a texture can tell whether it has already
 * uploaded what the decoder is currently holding. Never zero for a real frame. */
static unsigned long FMVSerial = 1;

/* Smacker stores a frame rate as microseconds per frame; the engine times
 * everything in ONE_FIXED units per second. */
static int FrameTimeFromUsf(double usf)
{
	int frameTime = (int)((usf * (double)ONE_FIXED) / 1000000.0);

	return (frameTime > 0) ? frameTime : (ONE_FIXED / 15);
}

static smk OpenFMVFile(const char *filename, int wantAudio, unsigned long *width, unsigned long *height)
{
	FILE *file;
	smk handle;
	unsigned char yScaleMode;
	unsigned char trackMask = 0;

	/* OpenGameFile applies the game data path and retries lowercased, which is
	 * what makes the Windows-style "FMVs\name.smk" work on a case sensitive
	 * filesystem. */
	file = OpenGameFile(filename, FILEMODE_READONLY, FILETYPE_PERM);
	if (file == NULL)
	{
		return NULL;
	}

	/* SMK_MODE_MEMORY reads the file up front so decoding a frame later never
	 * touches storage - worth the one-off cost on scoped storage.
	 * smk_open_filepointer closes the file itself. */
	handle = smk_open_filepointer(file, SMK_MODE_MEMORY);
	if (handle == NULL)
	{
		return NULL;
	}

	if (SMK_RESULT(smk_info_video(handle, width, height, &yScaleMode)) < 0
		|| *width == 0 || *height == 0
		|| *width > FMV_MAX_WIDTH || *height > FMV_MAX_HEIGHT)
	{
		fprintf(stderr, "FMV: %s has unusable dimensions\n", filename);
		smk_close(handle);
		return NULL;
	}

	if (wantAudio && SMK_RESULT(smk_info_audio(handle, &trackMask, NULL, NULL, NULL)) < 0)
	{
		trackMask = 0;
	}

	/* decoding audio nobody listens to is pure cost, so only the movie with
	 * sound enables a track */
	smk_enable_all(handle, (wantAudio ? (trackMask & SMK_AUDIO_TRACK_0) : 0) | SMK_VIDEO_TRACK);

	return handle;
}

static void StopTriggeredFMV(void)
{
	int i;

	if (TriggeredFMV.Handle)
	{
		smk_close(TriggeredFMV.Handle);
		TriggeredFMV.Handle = NULL;
	}

	if (TriggeredFMV.HasAudio)
	{
		FMVSound_Close();
	}

	TriggeredFMV.MessageNumber = 0;
	TriggeredFMV.HasAudio = 0;
	TriggeredFMV.Started = 0;
	TriggeredFMV.Ended = 0;
	TriggeredFMV.FrameIndex = 0;
	TriggeredFMV.FrameTimer = 0;
	TriggeredFMV.Serial = 0;

	/* let the screens fall back to static */
	i = NumberOfFMVTextures;
	while(i--)
	{
		if (FMVTexture[i].IsTriggeredPlotFMV)
		{
			FMVTexture[i].StaticImageDrawn = 0;
		}
	}
}

static int StartTriggeredFMV(int number, unsigned long startFrame)
{
	char filename[64];
	unsigned long frame;
	double usf;
	unsigned char trackMask = 0;
	unsigned char channels[7];
	unsigned char bitdepth[7];
	unsigned long rate[7];

	StopTriggeredFMV();

	if (!MoviesAreActive)
	{
		return 0;
	}

	sprintf(filename, "FMVs/message%d.smk", number);

	TriggeredFMV.Handle = OpenFMVFile(filename, 1, &TriggeredFMV.Width, &TriggeredFMV.Height);
	if (TriggeredFMV.Handle == NULL)
	{
		return 0;
	}

	smk_info_all(TriggeredFMV.Handle, &frame, &TriggeredFMV.FrameCount, &usf);
	TriggeredFMV.FrameTime = FrameTimeFromUsf(usf);
	TriggeredFMV.MessageNumber = number;

	if (SMK_RESULT(smk_info_audio(TriggeredFMV.Handle, &trackMask, channels, bitdepth, rate)) == 0
		&& (trackMask & SMK_AUDIO_TRACK_0))
	{
		TriggeredFMV.HasAudio = FMVSound_Open((int)rate[0], channels[0], bitdepth[0]);
		if (TriggeredFMV.HasAudio)
		{
			FMVSound_SetVolume(SmackerSoundVolume);
		}
	}

	/* restoring a save mid-message: only keyframes are seekable, so this lands
	 * on the nearest one before the wanted frame */
	if (startFrame > 0 && startFrame < TriggeredFMV.FrameCount)
	{
		if (SMK_RESULT(smk_seek_keyframe(TriggeredFMV.Handle, startFrame)) < 0)
		{
			StopTriggeredFMV();
			return 0;
		}
		smk_info_all(TriggeredFMV.Handle, &TriggeredFMV.FrameIndex, NULL, NULL);
		TriggeredFMV.Started = 1;
		TriggeredFMV.Serial = FMVSerial++;
	}

	return 1;
}

/* Pull the next frame out of a decoder, queueing its audio if this is the movie
 * with sound. Returns zero once the movie is over or has failed. */
static int DecodeFrame(smk handle, int started, int hasAudio)
{
	signed char result;

	result = started ? SMK_RESULT(smk_next(handle)) : SMK_RESULT(smk_first(handle));

	if (result == SMK_ERROR || result == SMK_DONE)
	{
		return 0;
	}

	if (hasAudio)
	{
		const unsigned char *audio = smk_get_audio(handle, 0);
		unsigned long size = smk_get_audio_size(handle, 0);

		if (audio != NULL && size > 0)
		{
			FMVSound_Queue(audio, size);
		}
	}

	return 1;
}

static void AdvanceTriggeredFMV(void)
{
	if (TriggeredFMV.Handle == NULL)
	{
		return;
	}

	if (!MoviesAreActive)
	{
		StopTriggeredFMV();
		return;
	}

	if (TriggeredFMV.HasAudio)
	{
		FMVSound_SetVolume(SmackerSoundVolume);
		FMVSound_Update();
	}

	if (!TriggeredFMV.Started)
	{
		if (!DecodeFrame(TriggeredFMV.Handle, 0, TriggeredFMV.HasAudio))
		{
			StopTriggeredFMV();
			return;
		}
		TriggeredFMV.Started = 1;
		TriggeredFMV.Serial = FMVSerial++;
		return;
	}

	if (TriggeredFMV.Ended)
	{
		/* hold the last frame up until the queued sound has finished playing */
		if (!TriggeredFMV.HasAudio || !FMVSound_IsPlaying())
		{
			StopTriggeredFMV();
		}
		return;
	}

	/* Video is the clock. Smacker front-loads about a second of audio into
	 * frame 0 and then delivers one frame's worth per frame, so decoding on
	 * time keeps the sound queue deep without it ever running ahead. */
	TriggeredFMV.FrameTimer += NormalFrameTime;

	/* a level load or other long hitch should not make the movie sprint */
	if (TriggeredFMV.FrameTimer > TriggeredFMV.FrameTime * FMV_MAX_CATCHUP_FRAMES)
	{
		TriggeredFMV.FrameTimer = TriggeredFMV.FrameTime * FMV_MAX_CATCHUP_FRAMES;
	}

	while (TriggeredFMV.FrameTimer >= TriggeredFMV.FrameTime)
	{
		/* no free sound buffer means the queue is already a long way ahead:
		 * leave the timer standing and try again next frame */
		if (TriggeredFMV.HasAudio && !FMVSound_CanQueue())
		{
			break;
		}

		TriggeredFMV.FrameTimer -= TriggeredFMV.FrameTime;

		if (!DecodeFrame(TriggeredFMV.Handle, 1, TriggeredFMV.HasAudio))
		{
			TriggeredFMV.Ended = 1;
			break;
		}

		TriggeredFMV.FrameIndex++;
		TriggeredFMV.Serial = FMVSerial++;
	}
}

static void AdvanceAmbientFMV(FMVTEXTURE *ftPtr)
{
	smk handle = (smk)ftPtr->SmackHandle;

	if (handle == NULL || !MoviesAreActive)
	{
		return;
	}

	if (ftPtr->FrameSerial == 0)
	{
		if (DecodeFrame(handle, 0, 0))
		{
			ftPtr->FrameSerial = FMVSerial++;
		}
		return;
	}

	ftPtr->FrameTimer += NormalFrameTime;

	if (ftPtr->FrameTimer > ftPtr->FrameTime * FMV_MAX_CATCHUP_FRAMES)
	{
		ftPtr->FrameTimer = ftPtr->FrameTime * FMV_MAX_CATCHUP_FRAMES;
	}

	while (ftPtr->FrameTimer >= ftPtr->FrameTime)
	{
		ftPtr->FrameTimer -= ftPtr->FrameTime;

		if (!DecodeFrame(handle, 1, 0))
		{
			/* these screens are wallpaper: loop them forever */
			if (!DecodeFrame(handle, 0, 0))
			{
				smk_close(handle);
				ftPtr->SmackHandle = NULL;
				ftPtr->StaticImageDrawn = 0;
				return;
			}
		}

		ftPtr->FrameSerial = FMVSerial++;
	}
}

void ScanImagesForFMVs(void)
{

	extern void SetupFMVTexture(FMVTEXTURE *ftPtr);
	int i;
	IMAGEHEADER *ihPtr;

	/* re-scanned on every level load, and nothing ever calls
	 * ReleaseAllFMVTextures, so drop the last level's decoders here */
	StopTriggeredFMV();
	for (i = 0; i < NumberOfFMVTextures; i++)
	{
		if (FMVTexture[i].SmackHandle)
		{
			smk_close((smk)FMVTexture[i].SmackHandle);
			FMVTexture[i].SmackHandle = NULL;
		}
	}

	NumberOfFMVTextures=0;

	#if MaxImageGroups>1
	for (j=0; j<MaxImageGroups; j++)
	{
		if (NumImagesArray[j])
		{
			ihPtr = &ImageHeaderArray[j*MaxImages];
			for (i = 0; i<NumImagesArray[j]; i++, ihPtr++)
			{
	#else
	{
		if(NumImages)
		{
			ihPtr = &ImageHeaderArray[0];
			for (i = 0; i<NumImages; i++, ihPtr++)
			{
	#endif
				char *strPtr;
				if((strPtr = strstr(ihPtr->ImageName,"FMVs")))
				{
					char filename[64];
					smk smackHandle;
					unsigned long width = 0, height = 0;
					{
						char *filenamePtr = filename;
						do
						{
							*filenamePtr++ = *strPtr;
						}
						while(*strPtr++!='.');

						*filenamePtr++='s';
						*filenamePtr++='m';
						*filenamePtr++='k';
						*filenamePtr=0;
					}

					/* A screen with a movie of its own loops it silently; one
					 * without (AvP Gold ships none of them) becomes a plot
					 * message screen instead. */
					smackHandle = OpenFMVFile(filename, 0, &width, &height);

					if (smackHandle)
					{
						FMVTexture[NumberOfFMVTextures].IsTriggeredPlotFMV = 0;
					}
					else
					{
						FMVTexture[NumberOfFMVTextures].IsTriggeredPlotFMV = 1;
					}

					{
						FMVTexture[NumberOfFMVTextures].SmackHandle = smackHandle;
						FMVTexture[NumberOfFMVTextures].ImagePtr = ihPtr;
						FMVTexture[NumberOfFMVTextures].StaticImageDrawn=0;
						FMVTexture[NumberOfFMVTextures].FrameSerial=0;
						FMVTexture[NumberOfFMVTextures].ShownSerial=0;
						FMVTexture[NumberOfFMVTextures].FrameTimer=0;
						FMVTexture[NumberOfFMVTextures].FrameWidth=width;
						FMVTexture[NumberOfFMVTextures].FrameHeight=height;

						if (smackHandle)
						{
							unsigned long frame, frameCount;
							double usf;

							smk_info_all(smackHandle, &frame, &frameCount, &usf);
							FMVTexture[NumberOfFMVTextures].FrameTime = FrameTimeFromUsf(usf);
						}
						else
						{
							FMVTexture[NumberOfFMVTextures].FrameTime = ONE_FIXED / 15;
						}

						SetupFMVTexture(&FMVTexture[NumberOfFMVTextures]);
						NumberOfFMVTextures++;

						if (NumberOfFMVTextures == MAX_NO_FMVTEXTURES)
						{
							break;
						}
					}
				}
			}
		}
	}


}

void UpdateAllFMVTextures(void)
{
	extern void UpdateFMVTexture(FMVTEXTURE *ftPtr);
	int i = NumberOfFMVTextures;

	AdvanceTriggeredFMV();

	while(i--)
	{
		AdvanceAmbientFMV(&FMVTexture[i]);
		UpdateFMVTexture(&FMVTexture[i]);
	}

}

void ReleaseAllFMVTextures(void)
{
	extern void UpdateFMVTexture(FMVTEXTURE *ftPtr);
	int i = NumberOfFMVTextures;

	StopTriggeredFMV();

	while(i--)
	{
		ReleaseFMVTexture(&FMVTexture[i]);
	}

}

/* The decoder a screen should be showing this frame, or null for TV static. */
static smk FMVTextureSource(FMVTEXTURE *ftPtr, unsigned long *serial,
                            unsigned long *width, unsigned long *height)
{
	if (!MoviesAreActive)
	{
		return NULL;
	}

	if (ftPtr->SmackHandle && ftPtr->FrameSerial)
	{
		*serial = ftPtr->FrameSerial;
		*width = ftPtr->FrameWidth;
		*height = ftPtr->FrameHeight;
		return (smk)ftPtr->SmackHandle;
	}

	if (ftPtr->IsTriggeredPlotFMV && TriggeredFMV.Handle && TriggeredFMV.Started)
	{
		*serial = TriggeredFMV.Serial;
		*width = TriggeredFMV.Width;
		*height = TriggeredFMV.Height;
		return TriggeredFMV.Handle;
	}

	return NULL;
}

int NextFMVTextureFrame(FMVTEXTURE *ftPtr, void *bufferPtr)
{
	unsigned long serial = 0, width = 0, height = 0;
	smk handle = FMVTextureSource(ftPtr, &serial, &width, &height);

	if (handle)
	{
		const unsigned char *pixels = smk_get_video(handle);

		if (pixels == NULL)
		{
			return 0;
		}

		/* nothing new since the last upload */
		if (serial == ftPtr->ShownSerial)
		{
			return 0;
		}
		ftPtr->ShownSerial = serial;

		ftPtr->FrameWidth = width;
		ftPtr->FrameHeight = height;

		memcpy(bufferPtr, pixels, width * height);
		FindLightingValuesFromFMVFrame((unsigned char*)bufferPtr, ftPtr, width * height);

		/* make sure fresh static is generated when the movie stops */
		ftPtr->StaticImageDrawn = 0;
		return 1;
	}

	{
		int w = FMV_STATIC_WIDTH;

		ftPtr->FrameWidth = FMV_STATIC_WIDTH;
		ftPtr->FrameHeight = FMV_STATIC_HEIGHT;

		if (!ftPtr->StaticImageDrawn)
		{
			int i = w*FMV_STATIC_HEIGHT/4;
			unsigned int seed = FastRandom();
			int *ptr = (int*)bufferPtr;
			do
			{
				seed = ((seed*1664525)+1013904223);
				*ptr++ = seed;
			}
			while(--i);
			ftPtr->StaticImageDrawn=1;
		}
		FindLightingValuesFromFMVFrame((unsigned char*)bufferPtr, ftPtr,
		                               (unsigned long)w*FMV_STATIC_HEIGHT);
		return 1;
	}

}

void UpdateFMVTexturePalette(FMVTEXTURE *ftPtr)
{
	unsigned long serial = 0, width = 0, height = 0;
	smk handle = FMVTextureSource(ftPtr, &serial, &width, &height);
	int i;

	if (handle)
	{
		const unsigned char *palette = smk_get_palette(handle);

		if (palette)
		{
			for(i=0;i<256;i++)
			{
				ftPtr->SrcPalette[i].peRed   = palette[i*3+0];
				ftPtr->SrcPalette[i].peGreen = palette[i*3+1];
				ftPtr->SrcPalette[i].peBlue  = palette[i*3+2];
			}
			return;
		}
	}

	{
	  	{
			unsigned int seed = FastRandom();
			for(i=0;i<256;i++)
			{
				int l = (seed&(seed>>24)&(seed>>16));
				seed = ((seed*1664525)+1013904223);
				ftPtr->SrcPalette[i].peRed=l;
				ftPtr->SrcPalette[i].peGreen=l;
		   		ftPtr->SrcPalette[i].peBlue=l;
		 	}
		}
	}
}

extern void StartTriggerPlotFMV(int number)
{
	int i;

	if (CheatMode_Active != CHEATMODE_NONACTIVE) return;

	if (!StartTriggeredFMV(number, 0))
	{
		return;
	}

	i = NumberOfFMVTextures;
	while(i--)
	{
		if (FMVTexture[i].IsTriggeredPlotFMV)
		{
			FMVTexture[i].MessageNumber = number;
		}
	}
}

extern void StartFMVAtFrame(int number, int frame)
{
	int i;

	if (number <= 0 || frame < 0)
	{
		return;
	}

	if (!StartTriggeredFMV(number, (unsigned long)frame))
	{
		return;
	}

	i = NumberOfFMVTextures;
	while(i--)
	{
		if (FMVTexture[i].IsTriggeredPlotFMV)
		{
			FMVTexture[i].MessageNumber = number;
		}
	}
}

extern void GetFMVInformation(int *messageNumberPtr, int *frameNumberPtr)
{
	if (TriggeredFMV.Handle)
	{
		*messageNumberPtr = TriggeredFMV.MessageNumber;
		*frameNumberPtr = (int)TriggeredFMV.FrameIndex;
		return;
	}

	*messageNumberPtr = 0;
	*frameNumberPtr = 0;
}


extern void InitialiseTriggeredFMVs(void)
{
	int i = NumberOfFMVTextures;

	StopTriggeredFMV();

	while(i--)
	{
		if (FMVTexture[i].IsTriggeredPlotFMV)
		{
			FMVTexture[i].MessageNumber = 0;
		}
	}
}

static void FindLightingValuesFromFMVFrame(unsigned char *bufferPtr, FMVTEXTURE *ftPtr, unsigned long pixels)
{
	unsigned int totalRed=0;
	unsigned int totalBlue=0;
	unsigned int totalGreen=0;
	unsigned long count = pixels;

	if (count == 0) return;

	do
	{
		unsigned char source = (*bufferPtr++);
		totalBlue += ftPtr->SrcPalette[source].peBlue;
		totalGreen += ftPtr->SrcPalette[source].peGreen;
		totalRed += ftPtr->SrcPalette[source].peRed;
	}
	while(--count);

	/* the original worked out (total/48)*16 over a 128x96 screen, which is the
	 * mean channel value times 4096 - keep that whatever size the movie is */
	FmvColourRed = (totalRed/pixels)*4096;
	FmvColourGreen = (totalGreen/pixels)*4096;
	FmvColourBlue = (totalBlue/pixels)*4096;

}

void SetupFMVTexture(FMVTEXTURE *ftPtr)
{
	if (ftPtr->PalettedBuf == NULL)
	{
		ftPtr->PalettedBuf = (unsigned char*) calloc(1, FMV_MAX_WIDTH*FMV_MAX_HEIGHT+FMV_MAX_WIDTH*FMV_MAX_HEIGHT*4);
	}

	if (ftPtr->RGBBuf == NULL)
	{
		if (ftPtr->PalettedBuf == NULL)
		{
			return;
		}

		ftPtr->RGBBuf = &ftPtr->PalettedBuf[FMV_MAX_WIDTH*FMV_MAX_HEIGHT];
	}
}

void UpdateFMVTexture(FMVTEXTURE *ftPtr)
{
	unsigned char *srcPtr;
	unsigned char *dstPtr;
	unsigned long pixels;

	if (ftPtr->PalettedBuf == NULL)
	{
		return;
	}

	/* palette first: the lighting values below are worked out through it */
	UpdateFMVTexturePalette(ftPtr);

	// get the next frame into the paletted buffer
	if (!NextFMVTextureFrame(ftPtr, &ftPtr->PalettedBuf[0]))
	{
		return;
	}

	pixels = ftPtr->FrameWidth * ftPtr->FrameHeight;

	srcPtr = &ftPtr->PalettedBuf[0];
	dstPtr = &ftPtr->RGBBuf[0];

	// not using paletted textures, so convert to rgb manually
	do
	{
		unsigned char source = (*srcPtr++);
		dstPtr[0] = ftPtr->SrcPalette[source].peRed;
		dstPtr[1] = ftPtr->SrcPalette[source].peGreen;
		dstPtr[2] = ftPtr->SrcPalette[source].peBlue;
		dstPtr[3] = 255;

		dstPtr += 4;
	} while(--pixels);

//#warning move this into opengl.c
	// update the opengl texture
	pglBindTexture(GL_TEXTURE_2D, ftPtr->ImagePtr->D3DTexture->id);

	/* the buffer above is RGBA, and every AVP texture is created GL_RGBA */
	pglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ftPtr->FrameWidth, ftPtr->FrameHeight, GL_RGBA, GL_UNSIGNED_BYTE, &ftPtr->RGBBuf[0]);
}

void ReleaseFMVTexture(FMVTEXTURE *ftPtr)
{
	ftPtr->MessageNumber = 0;

	if (ftPtr->SmackHandle)
	{
		smk_close((smk)ftPtr->SmackHandle);
		ftPtr->SmackHandle = NULL;
		ftPtr->FrameSerial = 0;
	}

	if (ftPtr->PalettedBuf != NULL)
	{
		free(ftPtr->PalettedBuf);
		ftPtr->PalettedBuf = NULL;
	}

	ftPtr->RGBBuf = NULL;
}
