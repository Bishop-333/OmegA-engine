/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2005 Stuart Dalton (badcdev@gmail.com)
Copyright (C) 2005-2006 Joerg Dietrich <dietrich_joerg@gmx.de>
Copyright (C) 2006 Thilo Schulz <arny@ats.s.bawue.de>
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
#ifdef USE_SYSTEM_FLAC
#include <FLAC/stream_decoder.h>
#else
#include "FLAC/stream_decoder.h"
#endif

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
        NULL };

// structure used for info purposes
struct snd_codec_flac_info {
	FLAC__StreamDecoder *decoder;

	byte *pcmbuf;   // buffer for not-used samples.
	int buflen;     // length of buffer data.
	int pcmbufsize; // amount of allocated memory for
	// pcmbuf. This should have at least
	// the size of a decoded flac frame.

	byte *dest;   // copy decoded data here.
	int destlen;  // amount of already copied data.
	int destsize; // amount of bytes we must decode.
};

/*************** Callback functions for quake3 ***************/

FLAC__StreamDecoderReadStatus read_callback( const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data ) {
	snd_stream_t *stream = (snd_stream_t *)client_data;
	int read_bytes;
	(void)decoder;

	if ( *bytes > 0 ) {
		read_bytes = FS_Read( buffer, *bytes, stream->file );
		if ( read_bytes <= 0 ) {
			*bytes = 0;
			return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
		}
		*bytes = read_bytes;
		return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
	}
	return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
}

FLAC__StreamDecoderSeekStatus seek_callback( const FLAC__StreamDecoder *decoder, FLAC__uint64 absolute_byte_offset, void *client_data ) {
	snd_stream_t *stream = (snd_stream_t *)client_data;
	(void)decoder;
	if ( FS_Seek( stream->file, absolute_byte_offset, FS_SEEK_SET ) < 0 )
		return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
	return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
}

FLAC__StreamDecoderTellStatus tell_callback( const FLAC__StreamDecoder *decoder, FLAC__uint64 *absolute_byte_offset, void *client_data ) {
	snd_stream_t *stream = (snd_stream_t *)client_data;
	(void)decoder;
	*absolute_byte_offset = FS_FTell( stream->file );
	return FLAC__STREAM_DECODER_TELL_STATUS_OK;
}

FLAC__StreamDecoderLengthStatus length_callback( const FLAC__StreamDecoder *decoder, FLAC__uint64 *stream_length, void *client_data ) {
	snd_stream_t *stream = (snd_stream_t *)client_data;
	(void)decoder;
	*stream_length = stream->length;
	return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
}

FLAC__bool eof_callback( const FLAC__StreamDecoder *decoder, void *client_data ) {
	snd_stream_t *stream = (snd_stream_t *)client_data;
	(void)decoder;
	return ( FS_FTell( stream->file ) >= stream->length );
}

FLAC__StreamDecoderWriteStatus write_callback( const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 *const buffer[], void *client_data ) {
	snd_stream_t *stream = (snd_stream_t *)client_data;
	struct snd_codec_flac_info *flacinfo = stream->ptr;
	size_t i, j;
	size_t samples = frame->header.blocksize;
	size_t channels = frame->header.channels;
	size_t total_bytes = samples * channels * FLAC_SAMPLEWIDTH;
	short *interleaved;
	short *dest16;
	int needed, leftover;

	(void)decoder;

	if ( channels != 2 && channels != 1 )
		return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;

	interleaved = (short *)Z_Malloc( total_bytes );
	dest16 = interleaved;

	for ( i = 0; i < samples; i++ ) {
		for ( j = 0; j < channels; j++ ) {
			*dest16++ = (short)buffer[j][i];
		}
	}

	needed = flacinfo->destsize - flacinfo->destlen;
	if ( total_bytes <= needed ) {
		memcpy( flacinfo->dest + flacinfo->destlen, interleaved, total_bytes );
		flacinfo->destlen += total_bytes;
	} else {
		memcpy( flacinfo->dest + flacinfo->destlen, interleaved, needed );
		flacinfo->destlen += needed;

		leftover = total_bytes - needed;
		if ( flacinfo->pcmbufsize < leftover ) {
			if ( flacinfo->pcmbuf ) Z_Free( flacinfo->pcmbuf );
			flacinfo->pcmbufsize = leftover * 2;
			flacinfo->pcmbuf = Z_Malloc( flacinfo->pcmbufsize );
		}
		memcpy( flacinfo->pcmbuf, (byte *)interleaved + needed, leftover );
		flacinfo->buflen = leftover;
	}

	Z_Free( interleaved );
	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

void metadata_callback( const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data ) {
	snd_stream_t *stream = (snd_stream_t *)client_data;
	(void)decoder;

	if ( metadata->type == FLAC__METADATA_TYPE_STREAMINFO ) {
		stream->info.rate = metadata->data.stream_info.sample_rate;
		stream->info.channels = metadata->data.stream_info.channels;
		stream->info.width = FLAC_SAMPLEWIDTH;
		stream->info.samples = metadata->data.stream_info.total_samples;
		stream->info.size = stream->info.samples * stream->info.channels * stream->info.width;
		stream->info.dataofs = 0;
	}
}

void error_callback( const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data ) {
	(void)decoder;
	(void)client_data;
	(void)status;
}

/*
=================
S_FLAC_CodecOpenStream
=================
*/

snd_stream_t *S_FLAC_CodecOpenStream( const char *filename ) {
	snd_stream_t *stream;
	struct snd_codec_flac_info *flacinfo;

	// Open the stream
	stream = S_CodecUtilOpen( filename, &flac_codec );
	if ( !stream || stream->length <= 0 )
		return NULL;

	// Initialize the flac info structure we need for streaming
	flacinfo = Z_Malloc( sizeof( *flacinfo ) );
	if ( !flacinfo ) {
		S_CodecUtilClose( &stream );
		return NULL;
	}

	stream->ptr = flacinfo;

	// initialize the libflac control structures.
	flacinfo->decoder = FLAC__stream_decoder_new();
	if ( !flacinfo->decoder ) {
		S_FLAC_CodecCloseStream( stream );
		return NULL;
	}

	if ( FLAC__stream_decoder_init_stream(
	         flacinfo->decoder,
	         read_callback, seek_callback, tell_callback, length_callback, eof_callback,
	         write_callback, metadata_callback, error_callback, stream ) != FLAC__STREAM_DECODER_INIT_STATUS_OK ) {
		S_FLAC_CodecCloseStream( stream );
		return NULL;
	}

	if ( !FLAC__stream_decoder_process_until_end_of_metadata( flacinfo->decoder ) ) {
		S_FLAC_CodecCloseStream( stream );
		return NULL;
	}

	return stream;
}

/*
=================
S_FLAC_CodecCloseStream
=================
*/

// free all memory we allocated.
void S_FLAC_CodecCloseStream( snd_stream_t *stream ) {
	struct snd_codec_flac_info *flacinfo;

	if ( !stream )
		return;

	// free all data in our flacinfo tree

	if ( stream->ptr ) {
		flacinfo = stream->ptr;

		if ( flacinfo->pcmbuf )
			Z_Free( flacinfo->pcmbuf );

		if ( flacinfo->decoder ) {
			FLAC__stream_decoder_finish( flacinfo->decoder );
			FLAC__stream_decoder_delete( flacinfo->decoder );
		}

		Z_Free( stream->ptr );
	}

	S_CodecUtilClose( &stream );
}

/*
=================
S_FLAC_CodecReadStream
=================
*/
int S_FLAC_CodecReadStream( snd_stream_t *stream, int bytes, void *buffer ) {
	struct snd_codec_flac_info *flacinfo;

	if ( !stream )
		return -1;

	flacinfo = stream->ptr;

	// Make sure we get complete frames all the way through.
	bytes -= bytes % ( stream->info.channels * stream->info.width );

	if ( flacinfo->buflen ) {
		if ( bytes < flacinfo->buflen ) {
			// we still have enough bytes in our decoded pcm buffer
			memcpy( buffer, flacinfo->pcmbuf, bytes );

			// remove the portion from our buffer.
			flacinfo->buflen -= bytes;
			memmove( flacinfo->pcmbuf, &flacinfo->pcmbuf[bytes], flacinfo->buflen );
			return bytes;
		} else {
			// copy over the samples we already have.
			memcpy( buffer, flacinfo->pcmbuf, flacinfo->buflen );
			flacinfo->destlen = flacinfo->buflen;
			flacinfo->buflen = 0;
		}
	} else
		flacinfo->destlen = 0;

	flacinfo->dest = buffer;
	flacinfo->destsize = bytes;

	while ( flacinfo->destlen < flacinfo->destsize ) {
		if ( !FLAC__stream_decoder_process_single( flacinfo->decoder ) )
			break;

		if ( FLAC__stream_decoder_get_state( flacinfo->decoder ) == FLAC__STREAM_DECODER_END_OF_STREAM )
			break;
	}

	return flacinfo->destlen;
}

/*
=====================================================================
S_FLAC_CodecLoad

We handle S_FLAC_CodecLoad as a special case of the streaming functions
where we read the whole stream at once.
======================================================================
*/
void *S_FLAC_CodecLoad( const char *filename, snd_info_t *info ) {
	snd_stream_t *stream;
	byte *pcmbuffer;

	// check if input is valid
	if ( !filename )
		return NULL;

	stream = S_FLAC_CodecOpenStream( filename );

	if ( !stream )
		return NULL;

	// copy over the info
	info->rate = stream->info.rate;
	info->width = stream->info.width;
	info->channels = stream->info.channels;
	info->samples = stream->info.samples;
	info->dataofs = stream->info.dataofs;

	// allocate enough buffer for all pcm data
	pcmbuffer = Hunk_AllocateTempMemory( stream->info.size );
	if ( !pcmbuffer ) {
		S_FLAC_CodecCloseStream( stream );
		return NULL;
	}

	info->size = S_FLAC_CodecReadStream( stream, stream->info.size, pcmbuffer );

	if ( info->size <= 0 ) {
		// we didn't read anything at all. darn.
		Hunk_FreeTempMemory( pcmbuffer );
		pcmbuffer = NULL;
	}

	S_FLAC_CodecCloseStream( stream );

	return pcmbuffer;
}

#endif // USE_FLAC
