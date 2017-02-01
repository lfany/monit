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

#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

#ifdef HAVE_SYS_STATVFS_H
# include <sys/statvfs.h>
#endif

#ifdef HAVE_MNTENT_H
#include <mntent.h>
#endif

#ifdef HAVE_PATHS_H
#include <paths.h>
#endif

#include "monit.h"
#include "device_sysdep.h"

// libmonit
#include "system/Time.h"
#include "io/File.h"


/* ------------------------------------------------------------- Definitions */


#define CIFSSTAT "/proc/fs/cifs/Stats"
#define NFSSTAT  "/proc/self/mountstats"


typedef struct Device_T {
        char name[PATH_MAX];
        boolean_t (*getDiskActivity)(struct Device_T *device, Info_T inf);
} *Device_T;


/* ----------------------------------------------------------------- Private */


static boolean_t _getCifsDiskActivity(Device_T device, Info_T inf) {
        FILE *f = fopen(CIFSSTAT, "r");
        if (! f) {
                LogError("Cannot open %s\n", CIFSSTAT);
                return false;
        }
        uint64_t now = Time_milli();
        char line[PATH_MAX];
        boolean_t found = false;
        while (fgets(line, sizeof(line), f)) {
                if (! found) {
                        int index;
                        char name[PATH_MAX];
                        if (sscanf(line, "%d) %1023s", &index, name) == 2 && IS(name, device->name)) {
                                found = true;
                        }
                } else if (found) {
                        char label1[256];
                        char label2[256];
                        uint64_t operations;
                        uint64_t bytes;
                        if (sscanf(line, "%255[^:]: %"PRIu64" %255[^:]: %"PRIu64, label1, &operations, label2, &bytes) == 4) {
                                if (IS(label1, "Reads") && IS(label2, "Bytes")) {
                                        Statistics_update(&(inf->priv.filesystem.read.bytes), now, bytes);
                                        Statistics_update(&(inf->priv.filesystem.read.operations), now, operations);
                                } else if (IS(label1, "Writes") && IS(label2, "Bytes")) {
                                        Statistics_update(&(inf->priv.filesystem.write.bytes), now, bytes);
                                        Statistics_update(&(inf->priv.filesystem.write.operations), now, operations);
                                        break;
                                }
                        }
                }
        }
        fclose(f);
        return true;
}


static boolean_t _getNfsDiskActivity(Device_T device, Info_T inf) {
        FILE *f = fopen(NFSSTAT, "r");
        if (! f) {
                LogError("Cannot open %s\n", NFSSTAT);
                return false;
        }
        uint64_t now = Time_milli();
        char line[PATH_MAX];
        char pattern[PATH_MAX];
        boolean_t found = false;
        snprintf(pattern, sizeof(pattern), "device %s ", device->name);
        while (fgets(line, sizeof(line), f)) {
                if (! found && Str_startsWith(line, pattern)) {
                        found = true;
                } else if (found) {
                        char name[256];
                        uint64_t operations;
                        uint64_t bytesSent;
                        uint64_t bytesReceived;
                        uint64_t time;
                        if (sscanf(line, " %255[^:]: %"PRIu64" %*u %*u %"PRIu64 " %"PRIu64" %*u %*u %"PRIu64, name, &operations, &bytesSent, &bytesReceived, &time) == 5) {
                                if (IS(name, "READ")) {
                                        Statistics_update(&(inf->priv.filesystem.read.time), now, time / 1000.); // us -> ms
                                        Statistics_update(&(inf->priv.filesystem.read.bytes), now, bytesReceived);
                                        Statistics_update(&(inf->priv.filesystem.read.operations), now, operations);
                                } else if (IS(name, "WRITE")) {
                                        Statistics_update(&(inf->priv.filesystem.write.time), now, time / 1000.); // us -> ms
                                        Statistics_update(&(inf->priv.filesystem.write.bytes), now, bytesSent);
                                        Statistics_update(&(inf->priv.filesystem.write.operations), now, operations);
                                        break;
                                }
                        }
                }
        }
        fclose(f);
        return true;
}


static boolean_t _getBlockDiskActivity(Device_T device, Info_T inf) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "/sys/class/block/%s/stat", device->name);
        FILE *f = fopen(path, "r");
        if (f) {
                uint64_t now = Time_milli();
                uint64_t readOperations = 0ULL, readSectors = 0ULL, readTime = 0ULL;
                uint64_t writeOperations = 0ULL, writeSectors = 0ULL, writeTime = 0ULL;
                if (fscanf(f, "%"PRIu64" %*u %"PRIu64" %"PRIu64" %"PRIu64" %*u %"PRIu64" %"PRIu64" %*u %*u %*u", &readOperations, &readSectors, &readTime, &writeOperations, &writeSectors, &writeTime) != 6) {
                        fclose(f);
                        LogError("filesystem statistic error: cannot parse %s -- %s\n", path, STRERROR);
                        return false;
                }
                Statistics_update(&(inf->priv.filesystem.read.time), now, readTime);
                Statistics_update(&(inf->priv.filesystem.read.bytes), now, readSectors * 512);
                Statistics_update(&(inf->priv.filesystem.read.operations), now, readOperations);
                Statistics_update(&(inf->priv.filesystem.write.time), now, writeTime);
                Statistics_update(&(inf->priv.filesystem.write.bytes), now, writeSectors * 512);
                Statistics_update(&(inf->priv.filesystem.write.operations), now, writeOperations);
                fclose(f);
        } else {
                LogError("filesystem statistic error: cannot read %s -- %s\n", path, STRERROR);
                return false;
        }
        return true;
}


static boolean_t _getDevice(char *mountpoint, Device_T device, Info_T inf) {
        FILE *f = setmntent(_PATH_MOUNTED, "r");
        if (! f) {
                LogError("Cannot open %s\n", _PATH_MOUNTED);
                return false;
        }
        struct mntent *mnt;
        while ((mnt = getmntent(f))) {
                if (IS(mountpoint, mnt->mnt_dir) && ! IS(mnt->mnt_fsname, "rootfs")) {
                        snprintf(inf->priv.filesystem.type, sizeof(inf->priv.filesystem.type), "%s", mnt->mnt_type);
                        if (IS(mnt->mnt_type, "cifs")) {
                                strncpy(device->name, mnt->mnt_fsname, sizeof(device->name) - 1);
                                Str_replaceChar(device->name, '/', '\\');
                                device->getDiskActivity = _getCifsDiskActivity;
                        } else if (Str_startsWith(mnt->mnt_type, "nfs")) {
                                strncpy(device->name, mnt->mnt_fsname, sizeof(device->name) - 1);
                                device->getDiskActivity = _getNfsDiskActivity;
                        } else  {
                                if (! realpath(mnt->mnt_fsname, device->name)) {
                                        // If the file doesn't exist it's a virtual filesystem -> skip
                                        if (errno != ENOENT && errno != ENOTDIR)
                                                LogError("Mount point %s -- %s\n", mountpoint, STRERROR);
                                        goto error;
                                }
                                snprintf(device->name, sizeof(device->name), "%s", File_basename(device->name));
                                device->getDiskActivity = _getBlockDiskActivity;
                        }
                        endmntent(f);
                        return true;
                }
        }
        LogError("Mount point %s -- not found in %s\n", mountpoint, _PATH_MOUNTED);
error:
        endmntent(f);
        return false;
}


static boolean_t _getDiskActivity(char *mountpoint, Info_T inf) {
        boolean_t rv = true;
        struct Device_T device = {};
        if (_getDevice(mountpoint, &device, inf)) {
                rv = device.getDiskActivity(&device, inf);
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
        FILE *f = setmntent(_PATH_MOUNTED, "r");
        if (! f) {
                LogError("Cannot open %s\n", _PATH_MOUNTED);
                return NULL;
        }
        struct mntent *mnt;
        while ((mnt = getmntent(f))) {
                /* Try to compare the the filesystem as is, if failed, try to use the symbolic link target */
                if (IS(dev, mnt->mnt_fsname) || (realpath(mnt->mnt_fsname, buf) && IS(dev, buf))) {
                        snprintf(buf, buflen, "%s", mnt->mnt_dir);
                        endmntent(f);
                        return buf;
                }
        }
        endmntent(f);
        LogError("Device %s not found in %s\n", dev, _PATH_MOUNTED);
        return NULL;
}


boolean_t filesystem_usage_sysdep(char *mountpoint, Info_T inf) {
        ASSERT(mountpoint);
        ASSERT(inf);
        return (_getDiskUsage(mountpoint, inf) && _getDiskActivity(mountpoint, inf));
}

