#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "fixer.h"
#include "win95/cd_player.h"
#include "cdplayer.h"

/* cd_player.cpp */
int CDPlayerVolume;

#if SDL_MAJOR_VERSION < 2
static int HaveCDROM = 0;
static SDL_CD *cdrom = NULL;

/* ** */

void CheckCDVolume()
{
/*
	fprintf(stderr, "CheckCDVolume()\n");
*/
}

/* ** */

void CDDA_Start()
{
/*
	fprintf(stderr, "CDDA_Start()\n");
*/

	int numdrives;
	
	if (!HaveCDROM) {
		HaveCDROM = 1;
		SDL_InitSubSystem(SDL_INIT_CDROM);
	}
	
	if (cdrom != NULL)
		CDDA_End();
	
	numdrives = SDL_CDNumDrives();
	
	if (numdrives == 0)
		return;
	
	cdrom = SDL_CDOpen(0);
}

void CDDA_End()
{
/*
	fprintf(stderr, "CDDA_End()\n");
*/

	if (cdrom != NULL) {
		CDDA_Stop();
		
		SDL_CDClose(cdrom);
	}
	
	cdrom = NULL;
}

void CDDA_ChangeVolume(int volume)
{
	fprintf(stderr, "CDDA_ChangeVolume(%d)\n", volume);
}

int CDDA_CheckNumberOfTracks()
{
/*
	fprintf(stderr, "CDDA_CheckNumberOfTracks()\n");
*/

	if (cdrom == NULL)
		return 0;
			
	return cdrom->numtracks;
}

int CDDA_IsOn()
{
/*
	fprintf(stderr, "CDDA_IsOn()\n");
*/	
	return (cdrom != NULL);
}

int CDDA_IsPlaying()
{
/*
	fprintf(stderr, "CDDA_IsPlaying()\n");
*/	
	if (cdrom == NULL)
		return 0;

	return (SDL_CDStatus(cdrom) == CD_PLAYING);
}

void CDDA_Play(int CDDATrack)
{
/*
	fprintf(stderr, "CDDA_Play(%d)\n", CDDATrack);
*/
	if (cdrom == NULL)
		return;
		
	if (CD_INDRIVE(SDL_CDStatus(cdrom))) {
		int track = CDDATrack - 1;
		int i;
		
		if (cdrom->numtracks == 0)
			return;
		
		track %= cdrom->numtracks;
		
		for (i = 0; i < cdrom->numtracks; i++) {
			if (cdrom->track[track].type == SDL_AUDIO_TRACK) {
				SDL_CDPlayTracks(cdrom, track, 0, 1, 0);
				return;
			}
			
			track++;
			track %= cdrom->numtracks;			
		}
	}
}

void CDDA_PlayLoop(int CDDATrack)
{
	fprintf(stderr, "CDDA_PlayLoop(%d)\n", CDDATrack);
	
	/* can't loop with SDL without a thread, so just play the track */
	CDDA_Play(CDDATrack);
}

void CDDA_Stop()
{
/*
	fprintf(stderr, "CDDA_Stop()\n");
*/
	if (cdrom == NULL)
		return;
	
	if (CD_INDRIVE(SDL_CDStatus(cdrom)))
		SDL_CDStop(cdrom);	
}

void CDDA_SwitchOn()
{
/*
	fprintf(stderr, "CDDA_SwitchOn()\n");
*/	
}

#else

/* The "CD" is the 15 Bink tracks in FMVs/ (AvP Gold shipped its soundtrack as
 * .bik files with a 4x4 dummy video stream). Track numbers come from
 * "CD Tracks.txt" via cdtrackselection.cpp and index the leading number in each
 * filename, e.g. track 3 is "03 Invasion.bik".
 */

#include "3dc.h"
#include "files.h"
#include "bink.h"
#include "audio_stream.h"

/* how much decoded music to keep queued: enough to ride out a level hitch */
#define MUSIC_BUFFER_SECONDS 1
#define MUSIC_CHUNK_BYTES    16384

static BINKSTREAM *MusicStream;
static int MusicSystemOn;
static int MusicStarted;
static int MusicLooping;
static int MusicTrack;
static int MusicEnded;
static unsigned char MusicChunk[MUSIC_CHUNK_BYTES];

/* Find "FMVs/<nn> ....bik" for a track number. The names differ per track, so
 * the directory has to be scanned rather than the path built. */
static int FindMusicTrackFile(int track, char *dest, size_t destSize)
{
	char pattern[16];
	void *dir;
	GameDirectoryFile *entry;
	int found = 0;

	if (track < 1 || track > 99)
	{
		return 0;
	}

	snprintf(pattern, sizeof(pattern), "%02d*", track);

	dir = OpenGameDirectory("FMVs", pattern, FILETYPE_PERM);
	if (dir == NULL)
	{
		return 0;
	}

	while ((entry = ScanGameDirectory(dir)) != NULL)
	{
		const char *dot = strrchr(entry->filename, '.');

		if (dot && (strcasecmp(dot, ".bik") == 0))
		{
			snprintf(dest, destSize, "FMVs/%s", entry->filename);
			found = 1;
			break;
		}
	}

	CloseGameDirectory(dir);

	return found;
}

static void StopMusicStream(void)
{
	if (MusicStream)
	{
		Bink_Close(MusicStream);
		MusicStream = NULL;
	}

	AudioStream_Close(AUDIO_STREAM_MUSIC);
	MusicTrack = 0;
	MusicLooping = 0;
	MusicEnded = 0;
}

static int StartMusicTrack(int track, int looping)
{
	char filename[256];

	StopMusicStream();

	if (!MusicStarted || !MusicSystemOn)
	{
		return 0;
	}

	if (!FindMusicTrackFile(track, filename, sizeof(filename)))
	{
		return 0;
	}

	/* the video stream is a 4x4 dummy, so decoding it would be pure waste */
	MusicStream = Bink_Open(filename, 0);
	if (MusicStream == NULL)
	{
		return 0;
	}

	if (!Bink_HasAudio(MusicStream)
		|| !AudioStream_Open(AUDIO_STREAM_MUSIC, Bink_AudioRate(MusicStream),
		                     Bink_AudioChannels(MusicStream), 16))
	{
		StopMusicStream();
		return 0;
	}

	MusicTrack = track;
	MusicLooping = looping;

	return 1;
}

/* Keep the music queue topped up. Called every frame from SoundSys_Management. */
void CheckCDVolume()
{
	unsigned long target;

	if (MusicStream == NULL)
	{
		return;
	}

	AudioStream_Update(AUDIO_STREAM_MUSIC);
	AudioStream_SetGain(AUDIO_STREAM_MUSIC, (float)CDPlayerVolume / (float)CDDA_VOLUME_MAX);

	target = (unsigned long)Bink_AudioRate(MusicStream) * Bink_AudioChannels(MusicStream)
	         * 2 * MUSIC_BUFFER_SECONDS;

	while (!MusicEnded
		&& AudioStream_CanQueue(AUDIO_STREAM_MUSIC)
		&& AudioStream_QueuedBytes(AUDIO_STREAM_MUSIC) < target)
	{
		unsigned long got = Bink_TakeAudio(MusicStream, MusicChunk, sizeof(MusicChunk));

		if (got == 0)
		{
			if (!Bink_Decode(MusicStream, NULL))
			{
				if (MusicLooping && Bink_Rewind(MusicStream))
				{
					continue;
				}
				MusicEnded = 1;
			}
			continue;
		}

		AudioStream_Queue(AUDIO_STREAM_MUSIC, MusicChunk, got);
	}

	/* once the tail has played out, let the track selector pick the next one */
	if (MusicEnded && !AudioStream_IsPlaying(AUDIO_STREAM_MUSIC))
	{
		StopMusicStream();
	}
}

/* ** */

void CDDA_Start()
{
	MusicStarted = 1;
	MusicSystemOn = 1;

	if (CDPlayerVolume <= 0)
	{
		CDPlayerVolume = CDDA_VOLUME_DEFAULT;
	}
}

void CDDA_End()
{
	StopMusicStream();
	MusicStarted = 0;
	MusicSystemOn = 0;
}

void CDDA_ChangeVolume(int volume)
{
	if (volume < CDDA_VOLUME_MIN) volume = CDDA_VOLUME_MIN;
	if (volume > CDDA_VOLUME_MAX) volume = CDDA_VOLUME_MAX;

	CDPlayerVolume = volume;
	AudioStream_SetGain(AUDIO_STREAM_MUSIC, (float)CDPlayerVolume / (float)CDDA_VOLUME_MAX);
}

int CDDA_GetCurrentVolumeSetting()
{
	return CDPlayerVolume;
}

int CDDA_CheckNumberOfTracks()
{
	char filename[256];
	int count = 0;

	/* the tracks are numbered from 1 with no gaps, so stop at the first miss */
	while (FindMusicTrackFile(count + 1, filename, sizeof(filename)))
	{
		count++;
	}

	return count;
}

int CDDA_IsOn()
{
	return MusicStarted && MusicSystemOn;
}

int CDDA_IsPlaying()
{
	return (MusicStream != NULL) && !MusicEnded;
}

void CDDA_Play(int CDDATrack)
{
	StartMusicTrack(CDDATrack, 0);
}

void CDDA_PlayLoop(int CDDATrack)
{
	StartMusicTrack(CDDATrack, 1);
}

void CDDA_Stop()
{
	StopMusicStream();
}

void CDDA_SwitchOn()
{
	MusicSystemOn = 1;
}

void CDDA_SwitchOff()
{
	StopMusicStream();
	MusicSystemOn = 0;
}

#endif
