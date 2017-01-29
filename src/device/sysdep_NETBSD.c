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

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#ifdef HAVE_CTYPE_H
#include <ctype.h>
#endif

#ifdef HAVE_SYS_SYSCTL_H
#include <sys/sysctl.h>
#endif

#ifdef HAVE_SYS_MOUNT_H
#include <sys/mount.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_SYS_STATVFS_H
#include <sys/statvfs.h>
#endif

#ifdef HAVE_SYS_IOSTAT_H
#include <sys/iostat.h>
#endif


#include "monit.h"
#include "device_sysdep.h"

// libmonit
#include "system/Time.h"
#include "io/File.h"


/* ------------------------------------------------------------- Definitions */


static struct {
        uint64_t timestamp;
        size_t diskCount;
        size_t diskLength;
        struct io_sysctl *disk;
} _statistics = {};


/* ------------------------------------------------------- Static destructor */


static void __attribute__ ((destructor)) _destructor() {
        FREE(_statistics.disk);
}


/* ----------------------------------------------------------------- Private */


// Parse the device path like /dev/sd0a -> sd0
static boolean_t _parseDevice(const char *path, char device[IOSTATNAMELEN]) {
        const char *base = File_basename(path);
        for (int len = strlen(base), i = len - 1; i >= 0; i--) {
                if (isdigit(*(base + i))) {
                        strncpy(device, base, i + 1 < sizeof(device) ? i + 1 : sizeof(device) - 1);
                        return true;
                }
        }
        return false;
}


static boolean_t _getDevice(char *mountpoint, char device[IOSTATNAMELEN]) {
        int countfs = getvfsstat(NULL, 0, MNT_NOWAIT);
        if (countfs != -1) {
                struct statvfs *statvfs = CALLOC(countfs, sizeof(struct statvfs));
                if ((countfs = getvfsstat(statvfs, countfs * sizeof(struct statvfs), MNT_NOWAIT)) != -1) {
                        for (int i = 0; i < countfs; i++) {
                                struct statvfs *sfs = statvfs + i;
                                if (IS(sfs->f_mntonname, mountpoint)) {
                                        //FIXME: NetBSD kernel has NFS statistics as well, but there is no clear mapping between the kernel label ("nfsX" style) and the NFS mount => we don't support NFS currently
                                        boolean_t rv = false;
                                        if (! IS(sfs->f_fstypename, "nfs")) {
                                                rv = _parseDevice(sfs->f_mntfromname, device);
                                        }
                                        FREE(statvfs);
                                        return rv;
                                }
                        }
                }
                FREE(statvfs);
        }
        LogError("Mount point %s -- %s\n", mountpoint, STRERROR);
        return false;
}


static boolean_t _getStatistics(uint64_t now) {
        // Refresh only if the statistics are older then 1 second (handle also backward time jumps)
        if (now > _statistics.timestamp + 1000 || now < _statistics.timestamp - 1000) {
                size_t len = 0;
                int mib[3] = {CTL_HW, HW_IOSTATS, sizeof(struct io_sysctl)};
                if (sysctl(mib, 3, NULL, &len, NULL, 0) == -1) {
                        LogError("filesystem statistic error -- cannot get HW_IOSTATS size: %s\n", STRERROR);
                        return false;
                }
                if (_statistics.diskLength != len) {
                        _statistics.diskCount = len / mib[2];
                        _statistics.diskLength = len;
                        RESIZE(_statistics.disk, len);
                }
                if (sysctl(mib, 3, _statistics.disk, &(_statistics.diskLength), NULL, 0) == -1) {
                        LogError("filesystem statistic error -- cannot get HW_IOSTATS: %s\n", STRERROR);
                        return false;
                }
                _statistics.timestamp = now;
        }
        return true;
}


static boolean_t _getDiskActivity(char *mountpoint, Info_T inf) {
        boolean_t rv = true;
        char device[IOSTATNAMELEN] = {};
        if (_getDevice(mountpoint, device)) {
                uint64_t now = Time_milli();
                if ((rv = _getStatistics(now))) {
                        for (int i = 0; i < _statistics.diskCount; i++)     {
                                if (Str_isEqual(device, _statistics.disk[i].name)) {
                                        Statistics_update(&(inf->priv.filesystem.read.bytes), now, _statistics.disk[i].rbytes);
                                        Statistics_update(&(inf->priv.filesystem.write.bytes), now, _statistics.disk[i].wbytes);
                                        Statistics_update(&(inf->priv.filesystem.read.operations),  now, _statistics.disk[i].rxfer);
                                        Statistics_update(&(inf->priv.filesystem.write.operations), now, _statistics.disk[i].wxfer);
                                        Statistics_update(&(inf->priv.filesystem.runTime), now, _statistics.disk[i].time_sec * 1000. + _statistics.disk[i].time_usec / 1000.);
                                        break;
                                }
                        }
                }
        } else {
                Statistics_reset(&(inf->priv.filesystem.read.time));
                Statistics_reset(&(inf->priv.filesystem.read.bytes));
                Statistics_reset(&(inf->priv.filesystem.read.operations));
                Statistics_reset(&(inf->priv.filesystem.write.time));
                Statistics_reset(&(inf->priv.filesystem.write.bytes));
                Statistics_reset(&(inf->priv.filesystem.write.operations));
        }
        return rv;
}


static boolean_t _getDiskUsage(char *mountpoint, Info_T inf) {
        struct statvfs usage;
        if (statvfs(mountpoint, &usage) != 0) {
                LogError("Error getting usage statistics for filesystem '%s' -- %s\n", mountpoint, STRERROR);
                return false;
        }
        inf->priv.filesystem.f_bsize =           usage.f_frsize;
        inf->priv.filesystem.f_blocks =          usage.f_blocks;
        inf->priv.filesystem.f_blocksfree =      usage.f_bavail;
        inf->priv.filesystem.f_blocksfreetotal = usage.f_bfree;
        inf->priv.filesystem.f_files =           usage.f_files;
        inf->priv.filesystem.f_filesfree =       usage.f_ffree;
        inf->priv.filesystem._flags =            inf->priv.filesystem.flags;
        inf->priv.filesystem.flags =             usage.f_flag;
        return true;
}


/* ------------------------------------------------------------------ Public */


char *device_mountpoint_sysdep(char *dev, char *buf, int buflen) {
        ASSERT(dev);
        ASSERT(buf);
        int countfs = getvfsstat(NULL, 0, ST_NOWAIT);
        if (countfs != -1) {
                struct statvfs *statvfs = CALLOC(countfs, sizeof(struct statvfs));
                if ((countfs = getvfsstat(statvfs, countfs * sizeof(struct statvfs), ST_NOWAIT)) != -1) {
                        for (int i = 0; i < countfs; i++) {
                                struct statvfs *sfs = statvfs + i;
                                if (IS(sfs->f_mntfromname, dev)) {
                                        snprintf(buf, buflen, "%s", sfs->f_mntonname);
                                        FREE(statvfs);
                                        return buf;
                                }
                        }
                }
                FREE(statvfs);
        }
        LogError("Error getting mountpoint for filesystem '%s' -- %s\n", dev, STRERROR);
        return NULL;
}


boolean_t filesystem_usage_sysdep(char *mountpoint, Info_T inf) {
        ASSERT(mountpoint);
        ASSERT(inf);
        return (_getDiskUsage(mountpoint, inf) && _getDiskActivity(mountpoint, inf));
}

