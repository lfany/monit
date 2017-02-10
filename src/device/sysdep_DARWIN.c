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

#ifdef HAVE_SYS_UCRED_H
#include <sys/ucred.h>
#endif

#ifdef HAVE_SYS_MOUNT_H
#include <sys/mount.h>
#endif

#ifdef HAVE_DISKARBITRATION_DISKARBITRATION_H
#include <DiskArbitration/DiskArbitration.h>
#endif

#ifdef HAVE_IOKIT_STORAGE_IOBLOCKSTORAGEDRIVER_H
#include <IOKit/storage/IOBlockStorageDriver.h>
#endif

#include "monit.h"

// libmonit
#include "system/Time.h"


/* ----------------------------------------------------------------- Private */


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
        inf->priv.filesystem._flags = inf->priv.filesystem.flags;
        inf->priv.filesystem.flags = usage.f_flags;
        return true;
}


static boolean_t _getDummyDiskActivity(void *_inf) {
        return true;
}


static boolean_t _getBlockDiskActivity(void *_inf) {
        int rv = false;
        Info_T inf = _inf;
        DASessionRef session = DASessionCreate(NULL);
        if (session) {
                CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)inf->priv.filesystem.object.mountpoint, strlen(inf->priv.filesystem.object.mountpoint), true);
                DADiskRef disk = DADiskCreateFromVolumePath(NULL, session, url);
                if (disk) {
                        DADiskRef wholeDisk = DADiskCopyWholeDisk(disk);
                        if (wholeDisk) {
                                io_service_t ioMedia = DADiskCopyIOMedia(wholeDisk);
                                if (ioMedia) {
                                        CFTypeRef statistics = IORegistryEntrySearchCFProperty(ioMedia, kIOServicePlane, CFSTR(kIOBlockStorageDriverStatisticsKey), kCFAllocatorDefault, kIORegistryIterateRecursively | kIORegistryIterateParents);
                                        if (statistics) {
                                                rv = true;
                                                UInt64 value = 0;
                                                uint64_t now = Time_milli();
                                                // Total read bytes
                                                CFNumberRef number = CFDictionaryGetValue(statistics, CFSTR(kIOBlockStorageDriverStatisticsBytesReadKey));
                                                if (number) {
                                                        CFNumberGetValue(number, kCFNumberSInt64Type, &value);
                                                        Statistics_update(&(inf->priv.filesystem.read.bytes), now, value);
                                                }
                                                // Total read operations
                                                number = CFDictionaryGetValue(statistics, CFSTR(kIOBlockStorageDriverStatisticsReadsKey));
                                                if (number) {
                                                        CFNumberGetValue(number, kCFNumberSInt64Type, &value);
                                                        Statistics_update(&(inf->priv.filesystem.read.operations), now, value);
                                                }
                                                // Total read time
                                                number = CFDictionaryGetValue(statistics, CFSTR(kIOBlockStorageDriverStatisticsTotalReadTimeKey));
                                                if (number) {
                                                        CFNumberGetValue(number, kCFNumberSInt64Type, &value);
                                                        Statistics_update(&(inf->priv.filesystem.read.time), now, value / 1048576.); // ns -> ms
                                                }
                                                // Total write bytes
                                                number = (CFNumberRef)CFDictionaryGetValue(statistics, CFSTR(kIOBlockStorageDriverStatisticsBytesWrittenKey));
                                                if (number) {
                                                        CFNumberGetValue(number, kCFNumberSInt64Type, &value);
                                                        Statistics_update(&(inf->priv.filesystem.write.bytes), now, value);
                                                }
                                                // Total write operations
                                                number = CFDictionaryGetValue(statistics, CFSTR(kIOBlockStorageDriverStatisticsWritesKey));
                                                if (number) {
                                                        CFNumberGetValue(number, kCFNumberSInt64Type, &value);
                                                        Statistics_update(&(inf->priv.filesystem.write.operations), now, value);
                                                }
                                                // Total write time
                                                number = CFDictionaryGetValue(statistics, CFSTR(kIOBlockStorageDriverStatisticsTotalWriteTimeKey));
                                                if (number) {
                                                        CFNumberGetValue(number, kCFNumberSInt64Type, &value);
                                                        Statistics_update(&(inf->priv.filesystem.write.time), now, value / 1048576.); // ns -> ms
                                                }
                                                //FIXME: add disk error statistics test: can use kIOBlockStorageDriverStatisticsWriteErrorsKey + kIOBlockStorageDriverStatisticsReadErrorsKey
                                                CFRelease(statistics);
                                        }
                                        IOObjectRelease(ioMedia);
                                }
                                CFRelease(wholeDisk);
                        }
                        CFRelease(disk);
                }
                CFRelease(url);
                CFRelease(session);
        }
        return rv;
}


static boolean_t _compareMountpoint(const char *mountpoint, struct statfs *mnt) {
        return IS(mountpoint, mnt->f_mntonname);
}


static boolean_t _compareDevice(const char *device, struct statfs *mnt) {
        return IS(device, mnt->f_mntfromname);
}


static boolean_t _setDevice(Info_T inf, const char *path, boolean_t (*compare)(const char *path, struct statfs *mnt)) {
        int countfs = getfsstat(NULL, 0, MNT_NOWAIT);
        if (countfs != -1) {
                struct statfs *mnt = CALLOC(countfs, sizeof(struct statfs));
                if ((countfs = getfsstat(mnt, countfs * sizeof(struct statfs), MNT_NOWAIT)) != -1) {
                        for (int i = 0; i < countfs; i++) {
                                struct statfs *mntItem = mnt + i;
                                if (compare(path, mntItem)) {
                                        if (IS(mntItem->f_fstypename, "hfs")) {
                                                inf->priv.filesystem.object.getDiskActivity = _getBlockDiskActivity;
                                        } else {
                                                inf->priv.filesystem.object.getDiskActivity = _getDummyDiskActivity;
                                        }
                                        strncpy(inf->priv.filesystem.object.device, mntItem->f_mntfromname, sizeof(inf->priv.filesystem.object.device) - 1);
                                        strncpy(inf->priv.filesystem.object.mountpoint, mntItem->f_mntonname, sizeof(inf->priv.filesystem.object.mountpoint) - 1);
                                        strncpy(inf->priv.filesystem.object.type, mntItem->f_fstypename, sizeof(inf->priv.filesystem.object.type) - 1);
                                        inf->priv.filesystem.object.getDiskUsage = _getDiskUsage;
                                        inf->priv.filesystem.object.mounted = true;
                                        FREE(mnt);
                                        return true;
                                }
                        }
                }
                FREE(mnt);
        }
        LogError("Lookup for '%s' filesystem failed\n", path);
        inf->priv.filesystem.object.mounted = false;
        return false;
}


static boolean_t _getDevice(Info_T inf, const char *path, boolean_t (*compare)(const char *path, struct statfs *mnt)) {
        //FIXME: cache mount informations (register for mount/unmount notification)
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

