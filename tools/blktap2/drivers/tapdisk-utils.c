/* 
 * Copyright (c) 2008, XenSource Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of XenSource Inc. nor the names of its contributors
 *       may be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/resource.h>

#include "blk.h"
#include "tapdisk.h"
#include "disktypes.h"
#include "blktaplib.h"
#include "tapdisk-log.h"
#include "tapdisk-utils.h"

void
tapdisk_start_logging(const char *name)
{
	static char buf[128];

	snprintf(buf, sizeof(buf), "%s[%d]", name, getpid());
	openlog(buf, LOG_CONS | LOG_ODELAY, LOG_DAEMON);
	open_tlog("/tmp/tapdisk.log", (64 << 10), TLOG_WARN, 0);
}

void
tapdisk_stop_logging(void)
{
	closelog();
	close_tlog();
}

int
tapdisk_set_resource_limits(void)
{
	int err;
	struct rlimit rlim;

	rlim.rlim_cur = RLIM_INFINITY;
	rlim.rlim_max = RLIM_INFINITY;

	err = setrlimit(RLIMIT_MEMLOCK, &rlim);
	if (err == -1) {
		EPRINTF("RLIMIT_MEMLOCK failed: %d\n", errno);
		return -errno;
	}

	err = mlockall(MCL_CURRENT | MCL_FUTURE);
	if (err == -1) {
		EPRINTF("mlockall failed: %d\n", errno);
		return -errno;
	}

#define CORE_DUMP
#if defined(CORE_DUMP)
	err = setrlimit(RLIMIT_CORE, &rlim);
	if (err == -1)
		EPRINTF("RLIMIT_CORE failed: %d\n", errno);
#endif

	return 0;
}

int
tapdisk_namedup(char **dup, const char *name)
{
	*dup = NULL;

	if (strnlen(name, MAX_NAME_LEN) >= MAX_NAME_LEN)
		return -ENAMETOOLONG;
	
	*dup = strdup(name);
	if (!*dup)
		return -ENOMEM;

	return 0;
}

int
tapdisk_parse_disk_type(const char *params, char **_path, int *_type)
{
        int i, err, size, handle_len;
	char *ptr, *path, handle[10];

	if (strlen(params) + 1 >= MAX_NAME_LEN)
		return -ENAMETOOLONG;

	ptr = strchr(params, ':');
	if (!ptr)
		return -EINVAL;

	path = ptr + 1;

        handle_len = ptr - params;
        if (handle_len > sizeof(handle))
          return -ENAMETOOLONG;
        
        memcpy(handle, params, handle_len);
        handle[handle_len] = '\0';
               
	size = sizeof(dtypes) / sizeof(disk_info_t *);
	for (i = 0; i < size; i++) {
          if (strncmp(handle, dtypes[i]->handle, handle_len))
			continue;

		if (dtypes[i]->idnum == -1)
			return -ENODEV;

		*_type = dtypes[i]->idnum;
		*_path = path;

		return 0;
	}

	return -ENODEV;
}

/*Get Image size, secsize*/
int
tapdisk_get_image_size(int fd, uint64_t *_sectors, uint32_t *_sector_size)
{
	int ret;
	struct stat stat;
	uint64_t sectors;
	uint64_t sector_size;

	sectors       = 0;
	sector_size   = 0;
	*_sectors     = 0;
	*_sector_size = 0;

	if (fstat(fd, &stat)) {
		DPRINTF("ERROR: fstat failed, Couldn't stat image");
		return -EINVAL;
	}

	if (S_ISBLK(stat.st_mode)) {
		/*Accessing block device directly*/
		if (blk_getimagesize(fd, &sectors) != 0)
			return -EINVAL;

		/*Get the sector size*/
		if (blk_getsectorsize(fd, &sector_size) != 0)
			sector_size = DEFAULT_SECTOR_SIZE;
	} else {
		/*Local file? try fstat instead*/
		sectors     = (stat.st_size >> SECTOR_SHIFT);
		sector_size = DEFAULT_SECTOR_SIZE;
	}

	if (sectors == 0) {		
		sectors     = 16836057ULL;
		sector_size = DEFAULT_SECTOR_SIZE;
	}

	return 0;
}
