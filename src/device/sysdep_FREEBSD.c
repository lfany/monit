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

#if defined HAVE_SYS_UCRED_H
#include <sys/ucred.h>
#endif

#ifdef HAVE_SYS_MOUNT_H
#include <sys/mount.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_CTYPE_H
#include <ctype.h>
#endif

#ifdef HAVE_KVM_H
#include <kvm.h>
#endif

#ifdef HAVE_PATHS_H
#include <paths.h>
#endif

#ifdef HAVE_DEVSTAT_H
#include <devstat.h>
#endif

#include "monit.h"

// libmonit
#include "system/Time.h"
#include "io/File.h"


/* ------------------------------------------------------------- Definitions */


typedef struct Device_T {
        char name[SPECNAMELEN];
        int instance;
} *Device_T;


static struct {
        uint64_t timestamp;
        struct statinfo disk;
} _statistics = {};


/* --------------------------------------- Static constructor and destructor */


static void __attribute__ ((constructor)) _constructor() {
        _statistics.disk.dinfo = CALLOC(1, sizeof(struct devinfo));
}


static void __attribute__ ((destructor)) _destructor() {
        FREE(_statistics.disk.dinfo);
}


/* ----------------------------------------------------------------- Private */


static uint64_t _bintimeToMilli(struct bintime *time) {
        return time->sec * 1000 + (((uint64_t)1000 * (uint32_t)(time->frac >> 32)) >> 32);
}


// Parse the device path like /dev/da0p2 into name:instance -> da:0
static boolean_t _parseDevice(const char *path, Device_T device) {
        const char *base = File_basename(path);
        for (int i = 0; base[i]; i++) {
                if (isdigit(*(base + i))) {
                        strncpy(device->name, base, i < sizeof(device->name) ? i : sizeof(device->name) - 1);
                        device->instance = Str_parseInt(base + i);
                        return true;
                }
        }
        return false;
}


static boolean_t _getDevice(char *mountpoint, Device_T device, Info_T inf) {
        int countfs = getfsstat(NULL, 0, MNT_NOWAIT);
        if (countfs != -1) {
                struct statfs *statfs = CALLOC(countfs, sizeof(struct statfs));
                if ((countfs = getfsstat(statfs, countfs * sizeof(struct statfs), MNT_NOWAIT)) != -1) {
                        for (int i = 0; i < countfs; i++) {
                                struct statfs *sfs = statfs + i;
                                if (IS(sfs->f_mntonname, mountpoint)) {
                                        boolean_t rv = false;
                                        snprintf(inf->priv.filesystem.device.type, sizeof(inf->priv.filesystem.device.type), "%s", sfs->f_fstypename);
                                        if (IS(sfs->f_fstypename, "zfs")) {
                                                //FIXME: can add ZFS support (see sysdep_SOLARIS.c), but libzfs headers are not installed on FreeBSD by default (part of "cddl" set)
                                        } else {
                                                rv = _parseDevice(sfs->f_mntfromname, device);
                                        }
                                        FREE(statfs);
                                        return rv;
                                }
                        }
                }
                FREE(statfs);
        }
        LogError("Mount point %s -- %s\n", mountpoint, STRERROR);
        return false;
}


static boolean_t _getStatistics(uint64_t now) {
        // Refresh only if the statistics are older then 1 second (handle also backward time jumps)
        if (now > _statistics.timestamp + 1000 || now < _statistics.timestamp - 1000) {
                if (devstat_getdevs(NULL, &(_statistics.disk)) == -1) {
                        LogError("filesystem statistics error -- devstat_getdevs: %s\n", devstat_errbuf);
                        return false;
                }
                _statistics.timestamp = now;
        }
        return true;
}


static boolean_t _getDiskActivity(char *mountpoint, Info_T inf) {
        boolean_t rv = true;
        struct Device_T device = {};
        if (_getDevice(mountpoint, &device, inf)) {
                uint64_t now = Time_milli();
                if ((rv = _getStatistics(now))) {
                        for (int i = 0; i < _statistics.disk.dinfo->numdevs; i++) {
                                if (_statistics.disk.dinfo->devices[i].unit_number == device.instance && IS(_statistics.disk.dinfo->devices[i].device_name, device.name)) {
                                        uint64_t now = _statistics.disk.snap_time * 1000;
                                        Statistics_update(&(inf->priv.filesystem.read.time), now, _bintimeToMilli(&(_statistics.disk.dinfo->devices[i].duration[DEVSTAT_READ])));
                                        Statistics_update(&(inf->priv.filesystem.read.bytes), now, _statistics.disk.dinfo->devices[i].bytes[DEVSTAT_READ]);
                                        Statistics_update(&(inf->priv.filesystem.read.operations),  now, _statistics.disk.dinfo->devices[i].operations[DEVSTAT_READ]);
                                        Statistics_update(&(inf->priv.filesystem.write.time), now, _bintimeToMilli(&(_statistics.disk.dinfo->devices[i].duration[DEVSTAT_WRITE])));
                                        Statistics_update(&(inf->priv.filesystem.write.bytes), now, _statistics.disk.dinfo->devices[i].bytes[DEVSTAT_WRITE]);
                                        Statistics_update(&(inf->priv.filesystem.write.operations), now, _statistics.disk.dinfo->devices[i].operations[DEVSTAT_WRITE]);
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
        struct statfs usage;
        if (statfs(mountpoint, &usage) != 0) {
                LogError("Error getting usage statistics for filesystem '%s' -- %s\n", mountpoint, STRERROR);
                return false;
        }
        inf->priv.filesystem.f_bsize =           usage.f_bsize;
        inf->priv.filesystem.f_blocks =          usage.f_blocks;
        inf->priv.filesystem.f_blocksfree =      usage.f_bavail;
        inf->priv.filesystem.f_blocksfreetotal = usage.f_bfree;
        inf->priv.filesystem.f_files =           usage.f_files;
        inf->priv.filesystem.f_filesfree =       usage.f_ffree;
        inf->priv.filesystem._flags =            inf->priv.filesystem.flags;
        inf->priv.filesystem.flags =             usage.f_flags;
        return true;
}


/* ------------------------------------------------------------------ Public */


char *device_mountpoint_sysdep(char *dev, char *buf, int buflen) {
        ASSERT(dev);
        ASSERT(buf);
        int countfs = getfsstat(NULL, 0, MNT_NOWAIT);
        if (countfs != -1) {
                struct statfs *statfs = CALLOC(countfs, sizeof(struct statfs));
                if ((countfs = getfsstat(statfs, countfs * sizeof(struct statfs), MNT_NOWAIT)) != -1) {
                        for (int i = 0; i < countfs; i++) {
                                struct statfs *sfs = statfs + i;
                                if (IS(sfs->f_mntfromname, dev)) {
                                        snprintf(buf, buflen, "%s", sfs->f_mntonname);
                                        FREE(statfs);
                                        return buf;
                                }
                        }
                }
                FREE(statfs);
        }
        LogError("Error getting mountpoint for filesystem '%s' -- %s\n", dev, STRERROR);
        return NULL;
}


boolean_t filesystem_usage_sysdep(char *mountpoint, Info_T inf) {
        ASSERT(mountpoint);
        ASSERT(inf);
        return (_getDiskUsage(mountpoint, inf) && _getDiskActivity(mountpoint, inf));
}

