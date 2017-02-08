/*
 * Copyright (C) Tildeslash Ltd. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 *
 * You must obey the GNU Affero General Public License in all respects
 * for all of the code used other than OpenSSL.
 */

/**
 *  System dependent filesystem methods.
 *
 *  @file
 */

#include "config.h"

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#ifdef HAVE_SYS_VFS_H
# include <sys/vfs.h>
#endif

#ifdef HAVE_MNTENT_H
#include <mntent.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

#include "monit.h"


/* ------------------------------------------------------------- Definitions */


#define MOUNTS "/etc/mnttab"


/* ----------------------------------------------------------------- Private */


static boolean_t _getDiskActivity(void *inf) {
        //FIXME: not implemented
        return true;
}


static boolean_t _getDiskUsage(void *_inf) {
        Info_T inf = _inf;
        struct statfs usage;
        if (statfs(inf->priv.filesystem.object.mountpoint, &usage) != 0) {
                LogError("Error getting usage statistics for filesystem '%s' -- %s\n", inf->priv.filesystem.object.mountpoint, STRERROR);
                return false;
        }
        inf->priv.filesystem.f_bsize = usage.f_bsize;
        inf->priv.filesystem.f_blocks = usage.f_blocks;
        inf->priv.filesystem.f_blocksfree = usage.f_bavail;
        inf->priv.filesystem.f_blocksfreetotal = usage.f_bfree;
        inf->priv.filesystem.f_files = usage.f_files;
        inf->priv.filesystem.f_filesfree = usage.f_ffree;
        return true;
}


static boolean_t _compareMountpoint(const char *mountpoint, struct mntent *mnt) {
        return IS(mountpoint, mnt->mnt_dir);
}


static boolean_t _compareDevice(const char *device, struct mntent *mnt) {
        return IS(device, mnt->mnt_fsname);
}


static boolean_t _setDevice(Info_T inf, const char *path, boolean_t (*compare)(const char *path, struct mntent *mnt)) {
        FILE *f = setmntent(MOUNTS, "r");
        if (! f) {
                LogError("Cannot open %s\n", MOUNTS);
                return false;
        }
        struct mntent *mnt;
        while ((mnt = getmntent(f))) {
                if (compare(path, mnt)) {
                        strncpy(inf->priv.filesystem.object.device, mnt->mnt_fsname, sizeof(inf->priv.filesystem.object.device) - 1);
                        strncpy(inf->priv.filesystem.object.mountpoint, mnt->mnt_dir, sizeof(inf->priv.filesystem.object.mountpoint) - 1);
                        strncpy(inf->priv.filesystem.object.type, mnt->mnt_type, sizeof(inf->priv.filesystem.object.type) - 1);
                        inf->priv.filesystem.object.getDiskUsage = _getDiskUsage;
                        inf->priv.filesystem.object.getDiskActivity = _getDiskActivity;
                        endmntent(f);
                        inf->priv.filesystem.object.mounted = true;
                        return true;
                }
        }
        LogError("Lookup for '%s' filesystem failed  -- not found in %s\n", path, MOUNTS);
error:
        endmntent(f);
        inf->priv.filesystem.object.mounted = false;
        return false;
}


static boolean_t _getDevice(Info_T inf, const char *path, boolean_t (*compare)(const char *path, struct mntent *mnt)) {
        if (_setDevice(inf, path, compare)) {
                return (inf->priv.filesystem.object.getDiskUsage(inf) && inf->priv.filesystem.object.getDiskActivity(inf));
        }
        return false;
}


/* ------------------------------------------------------------------ Public */


boolean_t Filesystem_getByMountpoint(Info_T inf, const char *path) {
        ASSERT(inf);
        ASSERT(path);
        return _getDevice(inf, path, _compareMountpoint);
}


boolean_t Filesystem_getByDevice(Info_T inf, const char *path) {
        ASSERT(inf);
        ASSERT(path);
        return _getDevice(inf, path, _compareDevice);
}

