#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include "mfs.h"
#include "backup.h"

#define BUFSIZE 512 * 2048

#define backup_usage()
#define mfsadd_usage()
#define restore_usage()

static unsigned int
get_percent (unsigned int current, unsigned int max)
{
	unsigned int prcnt;
	if (max <= 0x7fffffff / 10000)
	{
		prcnt = current * 10000 / max;
	}
	else if (max <= 0x7fffffff / 100)
	{
		prcnt = current * 100 / (max / 100);
	}
	else
	{
		prcnt = current / (max / 10000);
	}

	return prcnt;
}

int
restore_main (int argc, char **argv)
{
	char *drive, *drive2, *tmp;
	struct backup_info *info;
	int loop;
	int opt;
	unsigned int varsize = 0, swapsize = 0, flags = 0;
	char *filename = 0;
	int quiet = 0;

	tivo_partition_direct ();

	while ((opt = getopt (argc, argv, "i:v:s:zq")) > 0)
	{
		switch (opt)
		{
		case 'q':
			quiet++;
			break;
		case 'i':
			filename = optarg;
			break;
		case 'v':
			varsize = strtoul (optarg, &tmp, 10);
			varsize *= 1024 * 2;
			if (tmp && *tmp)
			{
				fprintf (stderr, "%s: Integer argument expected for -v.\n", argv[0]);
				return 1;
			}
			break;
		case 's':
			swapsize = strtoul (optarg, &tmp, 10);
			swapsize *= 1024 * 2;
			if (tmp && *tmp)
			{
				fprintf (stderr, "%s: Integer argument expected for -s.\n", argv[0]);
				return 1;
			}
			break;
		case 'z':
			flags |= RF_ZEROPART;
			break;
		default:
			restore_usage ();
			return 1;
		}
	}

	if (!filename)
	{
		fprintf (stderr, "%s: Backup file name expected.\n", argv[0]);
		return 1;
	}

	drive = 0;
	drive2 = 0;

	if (optind < argc)
		drive = argv[optind++];
	if (optind < argc)
		drive2 = argv[optind++];

	if (optind < argc || !drive)
	{
		fprintf (stderr, "%s: Device name expected.\n", argv[0]);
		return 1;
	}

	info = init_restore (flags);
	if (info)
	{
		int fd, nread, nwrit, secleft = 0;
		char buf[BUFSIZE];
		unsigned int cursec = 0, curcount;

		if (varsize)
			restore_set_varsize (info, varsize);
		if (swapsize)
			restore_set_swapsize (info, swapsize);

		if (filename[0] == '-' && filename[1] == '\0')
			fd = 0;
		else
			fd = open (filename, O_RDONLY);

		if (fd < 0)
		{
			perror (filename);
			return 1;
		}

		nread = read (fd, buf, BUFSIZE);
		if (nread <= 0)
		{
			fprintf (stderr, "Restore failed: %s: %s\n", filename, sys_errlist[errno]);
			return 1;
		}

		nwrit = restore_write (info, buf, nread);
		if (nwrit < 0)
		{
			if (last_err (info))
				fprintf (stderr, "Restore failed: %s\n", last_err (info));
			else
				fprintf (stderr, "Restore failed.\n", last_err (info));
			return 1;
		}

		if (restore_trydev (info, drive, drive2) < 0)
		{
			if (last_err (info))
				fprintf (stderr, "Restore failed: %s\n", last_err (info));
			else
				fprintf (stderr, "Restore failed.\n", last_err (info));
			return 1;
		}

		if (restore_start (info) < 0)
		{
			if (last_err (info))
				fprintf (stderr, "Restore failed: %s\n", last_err (info));
			else
				fprintf (stderr, "Restore failed.\n", last_err (info));
			return 1;
		}

		if (restore_write (info, buf + nwrit, nread - nwrit) != nread - nwrit)
		{
			if (last_err (info))
				fprintf (stderr, "Restore failed: %s\n", last_err (info));
			else
				fprintf (stderr, "Restore failed.\n", last_err (info));
			return 1;
		}

		fprintf (stderr, "Starting restore\nUncompressed backup size: %d megabytes\n", info->nsectors / 2048);
		while ((curcount = read (fd, buf, BUFSIZE)) > 0)
		{
			unsigned int prcnt, compr;
			if (restore_write (info, buf, curcount) != curcount)
			{
				if (last_err (info))
					fprintf (stderr, "Restore failed: %s\n", last_err (info));
				else
					fprintf (stderr, "Restore failed.\n");
				return 1;
			}
			cursec += curcount / 512;
			prcnt = get_percent (info->cursector, info->nsectors);
			compr = get_percent (info->cursector - cursec, info->cursector);
			if (quiet < 1)
			{
				if (info->back_flags & BF_COMPRESSED)
					fprintf (stderr, "Restoring %d of %d megabytes (%d.%02d%%) (%d.%02d%% compression)    \r", info->cursector / 2048, info->nsectors / 2048, prcnt / 100, prcnt % 100, compr / 100, compr % 100);
				else
					fprintf (stderr, "Restoring %d of %d megabytes (%d.%02d%%)    \r", info->cursector / 2048, info->nsectors / 2048, prcnt / 100, prcnt % 100);
			}
		}

		if (curcount < 0)
		{
			fprintf (stderr, "Restore failed: %s: %s\n", filename, sys_errlist[errno]);
			return 1;
		}
	}
	else
	{
		fprintf (stderr, "Restore failed.");
		return 1;
	}

	if (quiet < 1)
		fprintf (stderr, "\n");

	if (quiet < 2)
		fprintf (stderr, "Cleaning up restore.  Please wait a moment.\n");

	if (restore_finish (info) < 0)
	{
		if (last_err (info))
			fprintf (stderr, "Restore failed: %s\n", last_err (info));
		else
			fprintf (stderr, "Restore failed.\n");
		return 1;
	}

	if (quiet < 2)
		fprintf (stderr, "Restore done!\n");

	return 0;
}
