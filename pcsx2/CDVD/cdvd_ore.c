/*  PCSX2 - PS2 Emulator for PCs
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

/* ORE images, oxyromon's disc format, read through libsmelt. C89.
 *
 * libsmelt's byte view is the image the ORE was made from: a DVD's ISO,
 * or a CD's tracks end to end, 2352 bytes a sector, which the detector
 * then reads as it reads a flat image. A chunk is a run of sectors of
 * either size, read into the reader's cache. The ingot is used from the
 * thread that owns the reader only, as smelt asks.
 *
 * shortcut: opened by path, not through the frontend's VFS, as libsmelt
 * reads files itself; a VFS-only frontend needs a reader callback in its
 * ABI. */
#include <stdlib.h>
#include <string.h>

#include <compat/strl.h>

#include <smelt.h>

#include "cdvd_reader.h"

/* 147 sectors of 2048 bytes, 128 of 2352: whole sectors of a DVD or a CD */
#define ORE_CHUNK_SIZE (2048u * 147u)

typedef struct cdvd_ore
{
	cdvd_reader_t base;
	smelt_ingot*  ingot;
	/* Where the first track's index 01 lies in the byte view: past a
	 * pregap the ORE holds, which a flat image of it would not. */
	u64           start;
	u64           size;
} cdvd_ore_t;

static cdvd_chunk_t ore_chunk_for_offset(cdvd_reader_t* r, u64 offset)
{
	const cdvd_ore_t* o = (const cdvd_ore_t*)r;
	cdvd_chunk_t      chunk;
	chunk.id     = -1;
	chunk.offset = 0;
	chunk.length = 0;
	if (offset < o->size)
	{
		const u64 left = o->size - offset / ORE_CHUNK_SIZE * ORE_CHUNK_SIZE;
		chunk.id     = (s64)(offset / ORE_CHUNK_SIZE);
		chunk.offset = (u64)chunk.id * ORE_CHUNK_SIZE;
		chunk.length = (u32)(left < ORE_CHUNK_SIZE ? left : ORE_CHUNK_SIZE);
	}
	return chunk;
}

static int ore_read_chunk(cdvd_reader_t* r, void* dst, s64 id)
{
	cdvd_ore_t* o = (cdvd_ore_t*)r;
	u64         at;
	u32         len;
	if (id < 0 || (at = (u64)id * ORE_CHUNK_SIZE) >= o->size)
		return -1;
	len = (u32)(o->size - at < ORE_CHUNK_SIZE ? o->size - at : ORE_CHUNK_SIZE);
	if (smelt_read(o->ingot, o->start + at, (uint8_t*)dst, len) != SMELT_OK)
	{
		log_cb(RETRO_LOG_ERROR, "CDVD: ORE read at %llu: %s\n",
				(unsigned long long)at, smelt_last_error());
		return 0;
	}
	return (int)len;
}

static void ore_close(cdvd_reader_t* r)
{
	cdvd_ore_t* o = (cdvd_ore_t*)r;
	smelt_close(o->ingot);
	o->ingot = NULL;
	o->start = 0;
	o->size  = 0;
}

static bool ore_open(cdvd_reader_t* r, const char* filename)
{
	cdvd_ore_t* o     = (cdvd_ore_t*)r;
	size_t      count = 0;
	uint64_t    size  = 0;

	ore_close(r);
	strlcpy(r->filename, filename, sizeof(r->filename));
	if (smelt_open(filename, NULL, &o->ingot) != SMELT_OK
			|| smelt_logical_size(o->ingot, &size) != SMELT_OK
			|| smelt_track_count(o->ingot, &count) != SMELT_OK)
	{
		log_cb(RETRO_LOG_ERROR, "CDVD: failed to open ORE %s: %s\n",
				filename, smelt_last_error());
		ore_close(r);
		return false;
	}
	if (count == 0)
	{
		/* Not a CD: a PlayStation 2 DVD, not a ROM or another's disc,
		 * which the detector would take for an audio CD */
		char   platform[16];
		size_t len = sizeof(platform);
		if (smelt_platform(o->ingot, platform, &len) != SMELT_OK
				|| strcmp(platform, "ps2"))
		{
			log_cb(RETRO_LOG_ERROR, "CDVD: ORE %s is not a PlayStation 2 disc\n", filename);
			ore_close(r);
			return false;
		}
	}
	else
	{
		smelt_track t;
		if (smelt_track_at(o->ingot, 0, &t) != SMELT_OK)
		{
			ore_close(r);
			return false;
		}
		o->start = t.offset + (u64)t.pregap * SMELT_SECTOR;
	}
	if (size <= o->start)
	{
		ore_close(r);
		return false;
	}
	o->size = size - o->start;
	return true;
}

static u32 ore_block_count(const cdvd_reader_t* r)
{
	return (u32)(((const cdvd_ore_t*)r)->size / r->blocksize);
}

static const cdvd_reader_ops_t ore_ops =
{
	ore_chunk_for_offset,
	ore_read_chunk,
	NULL,
	ore_open,
	ore_close,
	ore_block_count
};

cdvd_reader_t* cdvd_ore_new(void)
{
	cdvd_ore_t* o = (cdvd_ore_t*)calloc(1, sizeof(*o));
	if (!o)
		return NULL;
	cdvd_reader_init(&o->base, &ore_ops);
	return &o->base;
}
