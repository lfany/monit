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

#include "monit.h"
#include "device_sysdep.h"

// libmonit
#include "system/Time.h"
#include "io/File.h"


/* ------------------------------------------------------------- Definitions */


typedef struct Device_T {
        char name[PATH_MAX];
        char type[STRLEN];
} *Device_T;


/* ----------------------------------------------------------------- Private */


static boolean_t _getDevice(char *mountpoint, Device_T device) {
        FILE *f = setmntent("/etc/mtab", "r");
        if (! f) {
                LogError("Cannot open /etc/mtab file\n");
                return false;
        }
        struct mntent *mnt;
        while ((mnt = getmntent(f))) {
                if (IS(mountpoint, mnt->mnt_dir) && ! IS(mnt->mnt_fsname, "rootfs")) {
                        strncpy(device->type, mnt->mnt_type, sizeof(device->type) - 1);
                        if (Str_startsWith(mnt->mnt_type, "cifs") || Str_startsWith(mnt->mnt_type, "nfs")) {
                                strncpy(device->name, mnt->mnt_fsname, sizeof(device->name) - 1);
                        } else  {
                                if (! realpath(mnt->mnt_fsname, device->name)) {
                                        // If the file doesn't exist it's a virtual filesystem -> skip
                                        if (errno != ENOENT && errno != ENOTDIR)
                                                LogError("Mount point %s -- %s\n", mountpoint, STRERROR);
                                        goto error;
                                }
                                snprintf(device->name, sizeof(device->name), "%s", File_basename(device->name));
                        }
                        endmntent(f);
                        return true;
                }
        }
        LogError("Mount point %s -- not found in /etc/mtab\n", mountpoint);
error:
        endmntent(f);
        return false;
}


static boolean_t _getDiskActivity(char *mountpoint, Info_T inf) {
        struct Device_T device = {};
        if (_getDevice(mountpoint, &device)) {
                if (Str_startsWith(device.type, "cifs")) {
                        //FIXME
                } else if (Str_startsWith(device.type, "nfs")) {
                        FILE *f = fopen("/proc/self/mountstats", "r");
                        if (! f) {
                                LogError("Cannot open /proc/self/mountstats file\n");
                                return false;
                        }
                        uint64_t now = Time_milli();
                        char line[PATH_MAX];
                        char pattern[PATH_MAX];
                        boolean_t found = false;
                        snprintf(pattern, sizeof(pattern), "device %s ", device.name);
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
                } else {
                        char path[PATH_MAX];
                        snprintf(path, sizeof(path), "/sys/class/block/%s/stat", device.name);
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
                }
        } else {
                Statistics_reset(&(inf->priv.filesystem.read.time));
                Statistics_reset(&(inf->priv.filesystem.read.bytes));
                Statistics_reset(&(inf->priv.filesystem.read.operations));
                Statistics_reset(&(inf->priv.filesystem.write.time));
                Statistics_reset(&(inf->priv.filesystem.write.bytes));
                Statistics_reset(&(inf->priv.filesystem.write.operations));
        }
        return true;
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
        FILE *f = setmntent("/etc/mtab", "r");
        if (! f) {
                LogError("Cannot open /etc/mtab file\n");
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
        LogError("Device %s not found in /etc/mtab\n", dev);
        return NULL;
}


boolean_t filesystem_usage_sysdep(char *mountpoint, Info_T inf) {
        ASSERT(mountpoint);
        ASSERT(inf);
        return (_getDiskUsage(mountpoint, inf) && _getDiskActivity(mountpoint, inf));
}

