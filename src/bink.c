/* Bink playback via FFmpeg. See bink.h. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

#include "3dc.h"
#include "files.h"
#include "bink.h"

/* Bink audio arrives in 1920 sample blocks; keep a couple of seconds so the
 * caller can top an audio stream up in whatever sized chunks it likes. */
#define BINK_AUDIO_FIFO_BYTES (44100 * 2 * 2 * 2)

/* FFmpeg reads through this rather than opening the path itself: OpenGameFile
 * already knows the game data dir, fixes the Windows separators and retries
 * lowercased, and on Android it is the call SAFFAL intercepts. */
#define BINK_IO_BUFFER 32768

struct BINKSTREAM
{
	FILE *File;
	AVFormatContext *Format;
	AVIOContext *Avio;
	unsigned char *AvioBuffer;

	int VideoStream;
	int AudioStream;
	AVCodecContext *VideoCodec;
	AVCodecContext *AudioCodec;
	AVPacket *Packet;
	AVFrame *Frame;

	struct SwsContext *Sws;
	int SwsWidth;
	int SwsHeight;

	SwrContext *Swr;
	int AudioRate;
	int AudioChannels;

	unsigned char *Fifo;
	unsigned long FifoBytes;

	int HavePicture;
	int Finished;
};

static int BinkRead(void *opaque, unsigned char *buf, int size)
{
	BINKSTREAM *bink = (BINKSTREAM *)opaque;
	size_t got = fread(buf, 1, size, bink->File);

	if (got == 0)
	{
		return AVERROR_EOF;
	}

	return (int)got;
}

static int64_t BinkSeek(void *opaque, int64_t offset, int whence)
{
	BINKSTREAM *bink = (BINKSTREAM *)opaque;

	if (whence == AVSEEK_SIZE)
	{
		long here = ftell(bink->File);
		long end;

		if (fseek(bink->File, 0, SEEK_END) != 0) return -1;
		end = ftell(bink->File);
		fseek(bink->File, here, SEEK_SET);
		return end;
	}

	if (fseek(bink->File, (long)offset, whence) != 0)
	{
		return -1;
	}

	return ftell(bink->File);
}

static AVCodecContext *OpenDecoder(AVFormatContext *format, int stream)
{
	AVCodecParameters *par = format->streams[stream]->codecpar;
	const AVCodec *codec = avcodec_find_decoder(par->codec_id);
	AVCodecContext *ctx;

	if (codec == NULL)
	{
		fprintf(stderr, "Bink: no decoder for %s\n", avcodec_get_name(par->codec_id));
		return NULL;
	}

	ctx = avcodec_alloc_context3(codec);
	if (ctx == NULL)
	{
		return NULL;
	}

	if (avcodec_parameters_to_context(ctx, par) < 0 || avcodec_open2(ctx, codec, NULL) < 0)
	{
		avcodec_free_context(&ctx);
		return NULL;
	}

	return ctx;
}

BINKSTREAM *Bink_Open(const char *filename, int wantVideo)
{
	BINKSTREAM *bink;
	unsigned int i;

	bink = (BINKSTREAM *)calloc(1, sizeof(BINKSTREAM));
	if (bink == NULL)
	{
		return NULL;
	}

	bink->VideoStream = -1;
	bink->AudioStream = -1;

	bink->File = OpenGameFile(filename, FILEMODE_READONLY, FILETYPE_PERM);
	if (bink->File == NULL)
	{
		free(bink);
		return NULL;
	}

	bink->AvioBuffer = (unsigned char *)av_malloc(BINK_IO_BUFFER);
	if (bink->AvioBuffer == NULL)
	{
		Bink_Close(bink);
		return NULL;
	}

	bink->Avio = avio_alloc_context(bink->AvioBuffer, BINK_IO_BUFFER, 0, bink,
	                                BinkRead, NULL, BinkSeek);
	if (bink->Avio == NULL)
	{
		av_free(bink->AvioBuffer);
		bink->AvioBuffer = NULL;
		Bink_Close(bink);
		return NULL;
	}

	bink->Format = avformat_alloc_context();
	if (bink->Format == NULL)
	{
		Bink_Close(bink);
		return NULL;
	}
	bink->Format->pb = bink->Avio;
	/* without this avformat_close_input() would avio_close() the context we
	 * allocated, and Bink_Close would then free it a second time */
	bink->Format->flags |= AVFMT_FLAG_CUSTOM_IO;

	if (avformat_open_input(&bink->Format, NULL, NULL, NULL) < 0)
	{
		/* avformat_open_input frees the context on failure */
		bink->Format = NULL;
		Bink_Close(bink);
		return NULL;
	}

	if (avformat_find_stream_info(bink->Format, NULL) < 0)
	{
		Bink_Close(bink);
		return NULL;
	}

	for (i = 0; i < bink->Format->nb_streams; i++)
	{
		AVCodecParameters *par = bink->Format->streams[i]->codecpar;

		if (wantVideo && par->codec_type == AVMEDIA_TYPE_VIDEO && bink->VideoStream < 0)
		{
			bink->VideoStream = (int)i;
		}
		else if (par->codec_type == AVMEDIA_TYPE_AUDIO && bink->AudioStream < 0)
		{
			bink->AudioStream = (int)i;
		}
	}

	if (bink->VideoStream >= 0)
	{
		bink->VideoCodec = OpenDecoder(bink->Format, bink->VideoStream);
		if (bink->VideoCodec == NULL)
		{
			bink->VideoStream = -1;
		}
	}

	if (bink->AudioStream >= 0)
	{
		bink->AudioCodec = OpenDecoder(bink->Format, bink->AudioStream);
		if (bink->AudioCodec == NULL)
		{
			bink->AudioStream = -1;
		}
	}

	if (bink->VideoStream < 0 && bink->AudioStream < 0)
	{
		Bink_Close(bink);
		return NULL;
	}

	if (bink->AudioStream >= 0)
	{
		AVChannelLayout out;

		bink->AudioRate = bink->AudioCodec->sample_rate;
		bink->AudioChannels = bink->AudioCodec->ch_layout.nb_channels > 1 ? 2 : 1;

		av_channel_layout_default(&out, bink->AudioChannels);

		/* Bink audio decodes to float, planar for the DCT variant and packed
		 * for RDFT, so both need converting to the S16 OpenAL wants. */
		if (swr_alloc_set_opts2(&bink->Swr, &out, AV_SAMPLE_FMT_S16, bink->AudioRate,
		                        &bink->AudioCodec->ch_layout, bink->AudioCodec->sample_fmt,
		                        bink->AudioCodec->sample_rate, 0, NULL) < 0
			|| swr_init(bink->Swr) < 0)
		{
			av_channel_layout_uninit(&out);
			Bink_Close(bink);
			return NULL;
		}
		av_channel_layout_uninit(&out);

		bink->Fifo = (unsigned char *)malloc(BINK_AUDIO_FIFO_BYTES);
		if (bink->Fifo == NULL)
		{
			Bink_Close(bink);
			return NULL;
		}
	}

	bink->Packet = av_packet_alloc();
	bink->Frame = av_frame_alloc();
	if (bink->Packet == NULL || bink->Frame == NULL)
	{
		Bink_Close(bink);
		return NULL;
	}

	return bink;
}

void Bink_Close(BINKSTREAM *bink)
{
	if (bink == NULL)
	{
		return;
	}

	if (bink->Sws) sws_freeContext(bink->Sws);
	if (bink->Swr) swr_free(&bink->Swr);
	if (bink->Frame) av_frame_free(&bink->Frame);
	if (bink->Packet) av_packet_free(&bink->Packet);
	if (bink->VideoCodec) avcodec_free_context(&bink->VideoCodec);
	if (bink->AudioCodec) avcodec_free_context(&bink->AudioCodec);
	if (bink->Format) avformat_close_input(&bink->Format);

	/* avformat_close_input does not free a caller supplied AVIOContext */
	if (bink->Avio)
	{
		av_freep(&bink->Avio->buffer);
		avio_context_free(&bink->Avio);
	}

	if (bink->File) fclose(bink->File);
	if (bink->Fifo) free(bink->Fifo);

	free(bink);
}

int Bink_HasVideo(BINKSTREAM *bink) { return bink && bink->VideoStream >= 0; }
int Bink_HasAudio(BINKSTREAM *bink) { return bink && bink->AudioStream >= 0; }
int Bink_AudioRate(BINKSTREAM *bink) { return bink ? bink->AudioRate : 0; }
int Bink_AudioChannels(BINKSTREAM *bink) { return bink ? bink->AudioChannels : 0; }

int Bink_VideoWidth(BINKSTREAM *bink)
{
	return (bink && bink->VideoCodec) ? bink->VideoCodec->width : 0;
}

int Bink_VideoHeight(BINKSTREAM *bink)
{
	return (bink && bink->VideoCodec) ? bink->VideoCodec->height : 0;
}

int Bink_FrameTime(BINKSTREAM *bink)
{
	AVRational rate;
	int frameTime;

	if (bink == NULL || bink->VideoStream < 0)
	{
		return 0;
	}

	rate = bink->Format->streams[bink->VideoStream]->avg_frame_rate;
	if (rate.num <= 0 || rate.den <= 0)
	{
		return ONE_FIXED / 15;
	}

	frameTime = (int)(((int64_t)ONE_FIXED * rate.den) / rate.num);

	return (frameTime > 0) ? frameTime : (ONE_FIXED / 15);
}

static void BufferAudio(BINKSTREAM *bink)
{
	unsigned long space = BINK_AUDIO_FIFO_BYTES - bink->FifoBytes;
	int frameBytes = bink->AudioChannels * 2;
	int room = (int)(space / frameBytes);
	int converted;
	unsigned char *dest = &bink->Fifo[bink->FifoBytes];

	if (room <= 0)
	{
		return;
	}

	converted = swr_convert(bink->Swr, &dest, room,
	                        (const unsigned char **)bink->Frame->extended_data,
	                        bink->Frame->nb_samples);
	if (converted > 0)
	{
		bink->FifoBytes += (unsigned long)converted * frameBytes;
	}
}

int Bink_Decode(BINKSTREAM *bink, int *gotVideo)
{
	if (gotVideo) *gotVideo = 0;

	if (bink == NULL || bink->Finished)
	{
		return 0;
	}

	for (;;)
	{
		int ret = av_read_frame(bink->Format, bink->Packet);

		if (ret < 0)
		{
			bink->Finished = 1;
			return 0;
		}

		if (bink->Packet->stream_index == bink->VideoStream && bink->VideoStream >= 0)
		{
			if (avcodec_send_packet(bink->VideoCodec, bink->Packet) == 0
				&& avcodec_receive_frame(bink->VideoCodec, bink->Frame) == 0)
			{
				bink->HavePicture = 1;
				if (gotVideo) *gotVideo = 1;
				av_packet_unref(bink->Packet);
				return 1;
			}
		}
		else if (bink->Packet->stream_index == bink->AudioStream && bink->AudioStream >= 0)
		{
			if (avcodec_send_packet(bink->AudioCodec, bink->Packet) == 0)
			{
				int buffered = 0;

				while (avcodec_receive_frame(bink->AudioCodec, bink->Frame) == 0)
				{
					BufferAudio(bink);
					buffered = 1;
				}

				if (buffered)
				{
					av_packet_unref(bink->Packet);
					return 1;
				}
			}
		}

		av_packet_unref(bink->Packet);
	}
}

unsigned long Bink_TakeAudio(BINKSTREAM *bink, unsigned char *dest, unsigned long maxBytes)
{
	unsigned long take;

	if (bink == NULL || bink->Fifo == NULL || bink->FifoBytes == 0)
	{
		return 0;
	}

	take = (maxBytes < bink->FifoBytes) ? maxBytes : bink->FifoBytes;

	/* keep whole sample frames together or the channels swap over */
	take -= take % (unsigned long)(bink->AudioChannels * 2);
	if (take == 0)
	{
		return 0;
	}

	memcpy(dest, bink->Fifo, take);
	bink->FifoBytes -= take;
	if (bink->FifoBytes)
	{
		memmove(bink->Fifo, &bink->Fifo[take], bink->FifoBytes);
	}

	return take;
}

void Bink_BlitRGB565(BINKSTREAM *bink, void *dest, int destWidth, int destHeight, int destPitch)
{
	int w, h, x, y;
	unsigned char *dst[4] = { NULL, NULL, NULL, NULL };
	int lineSize[4] = { 0, 0, 0, 0 };

	if (bink == NULL || !bink->HavePicture || dest == NULL)
	{
		return;
	}

	w = bink->Frame->width;
	h = bink->Frame->height;
	if (w <= 0 || h <= 0)
	{
		return;
	}

	/* the movies are all narrower than the 640x480 menu surface, so they are
	 * centred and letterboxed rather than scaled */
	if (w > destWidth) w = destWidth;
	if (h > destHeight) h = destHeight;

	x = (destWidth - w) / 2;
	y = (destHeight - h) / 2;

	if (bink->Sws == NULL || bink->SwsWidth != w || bink->SwsHeight != h)
	{
		if (bink->Sws) sws_freeContext(bink->Sws);
		bink->Sws = sws_getContext(bink->Frame->width, bink->Frame->height, bink->Frame->format,
		                           w, h, AV_PIX_FMT_RGB565LE, SWS_BILINEAR, NULL, NULL, NULL);
		bink->SwsWidth = w;
		bink->SwsHeight = h;
		if (bink->Sws == NULL)
		{
			return;
		}
	}

	/* black out the bars once per frame; the picture area is overwritten below */
	if (y > 0)
	{
		memset(dest, 0, (size_t)y * destPitch);
		memset((unsigned char *)dest + (size_t)(y + h) * destPitch, 0,
		       (size_t)(destHeight - y - h) * destPitch);
	}
	if (x > 0)
	{
		int row;

		for (row = y; row < y + h; row++)
		{
			memset((unsigned char *)dest + (size_t)row * destPitch, 0, destPitch);
		}
	}

	dst[0] = (unsigned char *)dest + (size_t)y * destPitch + (size_t)x * 2;
	lineSize[0] = destPitch;

	sws_scale(bink->Sws, (const unsigned char * const *)bink->Frame->data,
	          bink->Frame->linesize, 0, bink->Frame->height, dst, lineSize);
}

int Bink_Rewind(BINKSTREAM *bink)
{
	if (bink == NULL)
	{
		return 0;
	}

	if (av_seek_frame(bink->Format, -1, 0, AVSEEK_FLAG_BACKWARD) < 0)
	{
		return 0;
	}

	if (bink->VideoCodec) avcodec_flush_buffers(bink->VideoCodec);
	if (bink->AudioCodec) avcodec_flush_buffers(bink->AudioCodec);

	bink->FifoBytes = 0;
	bink->HavePicture = 0;
	bink->Finished = 0;

	return 1;
}
