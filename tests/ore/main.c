/* An ORE reads as the image it was made from. C89.
 *
 * Each pair is opened through cdvd_iso_open, as the core opens a disc:
 * the detector must find the same sector size, type and block count, and
 * every sector must read the same, through the reader's cache. An image
 * prefixed "!" is one the core must refuse, an ORE of a ROM.
 * Usage: ore_read <reference> <ore> ... [!<ore>] */
#include <stdio.h>
#include <string.h>

#include <libretro.h>

#include "CDVD/cdvd_iso.h"

static void quiet_log(enum retro_log_level level, const char *fmt, ...)
{
	(void)level;
	(void)fmt;
}
retro_log_printf_t log_cb = quiet_log;

static int same_image(const char *ref_path, const char *ore_path)
{
	cdvd_iso_t ref, ore;
	u8         a[2456], b[2456];
	u32        lsn;
	int        ok = 1;

	cdvd_iso_init(&ref);
	cdvd_iso_init(&ore);
	if (!cdvd_iso_open(&ref, ref_path, "", "") || !cdvd_iso_open(&ore, ore_path, "", ""))
	{
		printf("FAIL %s: open\n", ore_path);
		cdvd_iso_close(&ref);
		cdvd_iso_close(&ore);
		return 0;
	}
	if (ref.blocks != ore.blocks || ref.blocksize != ore.blocksize || ref.type != ore.type
			|| ref.offset != ore.offset || ref.blockofs != ore.blockofs)
	{
		printf("FAIL %s: blocks %u/%u size %u/%u type %d/%d\n", ore_path,
				ref.blocks, ore.blocks, ref.blocksize, ore.blocksize, ref.type, ore.type);
		ok = 0;
	}
	/* Backwards, so every chunk is loaded cold and from its end. */
	for (lsn = ref.blocks; ok && lsn-- > 0;)
	{
		memset(a, 0, sizeof(a));
		memset(b, 0xFF, sizeof(b));
		if (cdvd_iso_read(&ref, a, lsn) < 0 || cdvd_iso_read(&ore, b, lsn) < 0
				|| memcmp(a + ref.blockofs, b + ore.blockofs, ref.blocksize))
		{
			printf("FAIL %s: sector %u\n", ore_path, lsn);
			ok = 0;
		}
	}
	if (ok)
		printf("PASS %s: %u sectors of %u\n", ore_path, ref.blocks, ref.blocksize);
	cdvd_iso_close(&ref);
	cdvd_iso_close(&ore);
	return ok;
}

int main(int argc, char **argv)
{
	int ok = 1;
	int i;

	for (i = 1; i < argc; i++)
	{
		if (argv[i][0] == '!')
		{
			cdvd_iso_t iso;
			cdvd_iso_init(&iso);
			if (cdvd_iso_open(&iso, argv[i] + 1, "", ""))
			{
				printf("FAIL %s: opened\n", argv[i] + 1);
				ok = 0;
			}
			else
				printf("PASS %s: refused\n", argv[i] + 1);
			cdvd_iso_close(&iso);
		}
		else if (i + 1 < argc)
		{
			ok &= same_image(argv[i], argv[i + 1]);
			i++;
		}
	}
	return ok ? 0 : 1;
}
