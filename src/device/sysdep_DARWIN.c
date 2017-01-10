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
#include "device_sysdep.h"

// libmonit
#include "system/Time.h"


static boolean_t _getDiskActivity(char *mountpoint, Info_T inf) {
        int rv = false;
        DASessionRef session = DASessionCreate(NULL);
        if (session) {
                CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)mountpoint, strlen(mountpoint), true);
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
                                                CFRelease(statistics);
                                        }
                                        IOObjectRelease(ioMedia);
                                }
                                CFRelease(wholeDisk);
                        }
                        CFRelease(disk);
                }
                CFRelease(url);
        }
        inf->priv.filesystem.hasIOStatistics = rv;
        return rv;
}


static boolean_t _getDiskUsage(char *mountpoint, Info_T inf) {
        ASSERT(inf);
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

