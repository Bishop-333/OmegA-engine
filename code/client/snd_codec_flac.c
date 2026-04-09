/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2005 Stuart Dalton (badcdev@gmail.com)
Copyright (C) 2026 Bishop-333

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

// FLAC support is enabled by this define
#ifdef USE_FLAC

// includes for the Q3 sound system
#include "client.h"
#include "snd_codec.h"

// includes for the FLAC codec
#define DR_FLAC_NO_STDIO
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"

// The FLAC codec can return the samples in a number of different formats,
// we use the standard signed short format.
#define FLAC_SAMPLEWIDTH 2

// Q3 FLAC codec
snd_codec_t flac_codec =
{
	"flac",
	S_FLAC_CodecLoad,
	S_FLAC_CodecOpenStream,
	S_FLAC_CodecReadStream,
	S_FLAC_CodecCloseStream,
	NULL
};

// read() replacement
size_t S_FLAC_Callback_read( void *datasource, void *ptr, size_t bytesToRead )
{
	snd_stream_t *stream;
	int bytesRead = 0;

	// check if input is valid
	if (!ptr)
	{
		return 0;
	}
 
	if (!datasource)
	{
		return 0;
	}
	
	// we use a snd_stream_t in the generic pointer to pass around
	stream = (snd_stream_t *) datasource;

	// read it with the Q3 function FS_Read()
	bytesRead = FS_Read(ptr, bytesToRead, stream->file);
	if (bytesRead < 0)
	{
		return 0;
	}

	// update the file position
	stream->pos += bytesRead;
	
	return bytesRead;
}

// seek() replacement
drflac_bool32 S_FLAC_Callback_seek(void *datasource, int offset, drflac_seek_origin whence)
{
	snd_stream_t *stream;
	int retVal = 0;

	// check if input is valid
	if (!datasource)
	{
		return DRFLAC_FALSE;
	}

	// snd_stream_t in the generic pointer
	stream = (snd_stream_t *) datasource;

	// we must map the whence to its Q3 counterpart
	switch (whence)
	{
		case DRFLAC_SEEK_SET :
		{
			// set the file position in the actual file with the Q3 function
			retVal = FS_Seek(stream->file, (long) offset, FS_SEEK_SET);

			// something has gone wrong, so we return here
			if (retVal < 0)
			{
				return DRFLAC_FALSE;
			}

			// keep track of file position
			stream->pos = (int) offset;
			break;
		}
  
		case DRFLAC_SEEK_CUR :
		{
			// set the file position in the actual file with the Q3 function
			retVal = FS_Seek(stream->file, (long) offset, FS_SEEK_CUR);

			// something has gone wrong, so we return here
			if (retVal < 0)
			{
				return DRFLAC_FALSE;
			}

			// keep track of file position
			stream->pos += (int) offset;
			break;
		}
 
		case DRFLAC_SEEK_END :
		{
			// set the file position in the actual file with the Q3 function
			retVal = FS_Seek(stream->file, (long) offset, FS_SEEK_END);

			// something has gone wrong, so we return here
			if (retVal < 0)
			{
				return DRFLAC_FALSE;
			}

			// keep track of file position
			stream->pos = stream->length + (int) offset;
			break;
		}
  
		default :
		{
			// unknown whence, so we return an error
			return DRFLAC_FALSE;
		}
	}

	// stream->pos shouldn't be smaller than zero or bigger than the filesize
	stream->pos = (stream->pos < 0) ? 0 : stream->pos;
	stream->pos = (stream->pos > stream->length) ? stream->length : stream->pos;

	return DRFLAC_TRUE;
}

// tell() replacement
drflac_bool32 S_FLAC_Callback_tell(void *datasource, drflac_int64 *cursor)
{
	snd_stream_t   *stream;

	// check if input is valid
	if (!datasource || !cursor)
	{
		return DRFLAC_FALSE;
	}

	// snd_stream_t in the generic pointer
	stream = (snd_stream_t *) datasource;

	*cursor = (drflac_int64) FS_FTell(stream->file);
	return DRFLAC_TRUE;
}

/*
=================
S_FLAC_CodecOpenStream
=================
*/
snd_stream_t *S_FLAC_CodecOpenStream(const char *filename)
{
	snd_stream_t *stream;

	// FLAC codec control structure
	drflac *df;

	// some variables used to get informations about the FLAC 
	drflac_uint64 numSamples;

	// check if input is valid
	if (!filename)
	{
		return NULL;
	}

	// Open the stream
	stream = S_CodecUtilOpen(filename, &flac_codec);
	if (!stream)
	{
		return NULL;
	}

	// open the codec with our callbacks and stream as the generic pointer
	df = drflac_open(S_FLAC_Callback_read, S_FLAC_Callback_seek, S_FLAC_Callback_tell, stream, NULL);
	if (!df)
	{
		S_CodecUtilClose(&stream);

		return NULL;
	}

	// we only support mono/stereo streams
	if (df->channels < 1 || df->channels > 2)
	{
		drflac_close(df);
		S_CodecUtilClose(&stream);

		return NULL;
	}
 
	if (df->totalPCMFrameCount == 0)
	{
		drflac_close(df);
		S_CodecUtilClose(&stream);

		return NULL;  
	}

	// get the number of sample-frames in the FLAC
	numSamples = df->totalPCMFrameCount;

	// fill in the info-structure in the stream
	stream->info.rate = df->sampleRate;
	stream->info.width = FLAC_SAMPLEWIDTH;
	stream->info.channels = df->channels;
	stream->info.samples = numSamples;
	stream->info.size = stream->info.samples * stream->info.channels * stream->info.width;
	stream->info.dataofs = 0;

	// We use stream->pos for the file pointer in the compressed flac file 
	stream->pos = 0;
	
	// We use the generic pointer in stream for the FLAC codec control structure
	stream->ptr = df;

	return stream;
}

/*
=================
S_FLAC_CodecCloseStream
=================
*/
void S_FLAC_CodecCloseStream(snd_stream_t *stream)
{
	// check if input is valid
	if (!stream)
	{
		return;
	}
	
	// let the FLAC codec cleanup its stuff
	drflac_close((drflac *) stream->ptr);

	// close the stream
	S_CodecUtilClose(&stream);
}

/*
=================
S_FLAC_CodecReadStream
=================
*/
int S_FLAC_CodecReadStream(snd_stream_t *stream, int bytes, void *buffer)
{
	drflac_uint64 framesRead;
	int frameSize;

	// check if input is valid
	if (!(stream && buffer))
	{
		return 0;
	}

	if (bytes <= 0)
	{
		return 0;
	}

	frameSize = stream->info.channels * FLAC_SAMPLEWIDTH;
	if (frameSize <= 0)
	{
		return 0;
	}

	framesRead = drflac_read_pcm_frames_s16((drflac *) stream->ptr, bytes / frameSize, (drflac_int16 *) buffer);

	return (int) framesRead * frameSize;
}

/*
=====================================================================
S_FLAC_CodecLoad

We handle S_FLAC_CodecLoad as a special case of the streaming functions 
where we read the whole stream at once.
======================================================================
*/
void *S_FLAC_CodecLoad(const char *filename, snd_info_t *info)
{
	snd_stream_t *stream;
	byte *buffer;
	int bytesRead;
	
	// check if input is valid
	if (!(filename && info))
	{
		return NULL;
	}
	
	// open the file as a stream
	stream = S_FLAC_CodecOpenStream(filename);
	if (!stream)
	{
		return NULL;
	}
	
	// copy over the info
	info->rate = stream->info.rate;
	info->width = stream->info.width;
	info->channels = stream->info.channels;
	info->samples = stream->info.samples;
	info->size = stream->info.size;
	info->dataofs = stream->info.dataofs;

	// allocate a buffer
	// this buffer must be free-ed by the caller of this function
    buffer = Hunk_AllocateTempMemory(info->size);
	if (!buffer)
	{
		S_FLAC_CodecCloseStream(stream);
	
		return NULL;	
	}

	// fill the buffer
	bytesRead = S_FLAC_CodecReadStream(stream, info->size, buffer);

	// we don't even have read a single byte
	if (bytesRead <= 0)
	{
		Hunk_FreeTempMemory(buffer);
		S_FLAC_CodecCloseStream(stream);

		return NULL;
	}

	S_FLAC_CodecCloseStream(stream);

	return buffer;
}

#endif // USE_FLAC
