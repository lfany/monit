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

#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif

#ifdef HAVE_KSTAT_H
#include <kstat.h>
#endif

#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif

#ifdef HAVE_SYS_STATVFS_H
# include <sys/statvfs.h>
#endif

#ifdef HAVE_SYS_MNTENT_H
# include <sys/mntent.h>
#endif

#ifdef HAVE_SYS_MNTTAB_H
# include <sys/mnttab.h>
#endif

#include "monit.h"
#include "device_sysdep.h"

// libmonit
#include "system/Time.h"
#include "io/File.h"


/* ------------------------------------------------------------- Definitions */


typedef struct Device_T {
        char module[256];
        char name[256];
        int instance;
        char partition;
} *Device_T;


/* ----------------------------------------------------------------- Private */


static boolean_t _getDevice(char *mountpoint, Device_T device) {
        FILE *mntfd = fopen(MNTTAB, "r");
        if (! mntfd) {
                LogError("Cannot open %s file\n", MNTTAB);
                return false;
        }
        struct extmnttab mnt;
        boolean_t rv = false;
        resetmnttab(mntfd);
        while (getextmntent(mntfd, &mnt, sizeof(struct extmnttab)) == 0) {
                if (IS(mnt.mnt_mountp, mountpoint)) {
                        if (Str_startsWith(mnt.mnt_fstype, MNTTYPE_NFS)) {
                                strncpy(device->module, "nfs", sizeof(device->module) - 1);
                                snprintf(device->name, sizeof(device->name), "nfs%d", mnt.mnt_minor);
                                device->instance = mnt.mnt_minor;
                                rv = true;
                        } else {
                                char special[PATH_MAX];
                                if (! realpath(mnt.mnt_special, special)) {
                                        // If the file doesn't exist it's a virtual filesystem -> skip
                                        if (errno != ENOENT && errno != ENOTDIR)
                                                LogError("Mount point %s -- %s\n", mountpoint, STRERROR);
                                } else if (! Str_startsWith(special, "/devices/")) {
                                        LogError("Mount point %s -- invalid device %s\n", mountpoint, special);
                                } else {
                                        // Strip "/devices" prefix and :X partition postfix: /devices/pci@0,0/pci15ad,1976@10/sd@0,0:a -> /pci@0,0/pci15ad,1976@10/sd@0,0
                                        int speclen = strlen(special);
                                        int devlen = strlen("/devices");
                                        int len = speclen - devlen - 2;
                                        device->partition = *(special + speclen - 1);
                                        memmove(special, special + devlen, len);
                                        special[len] = 0;
                                        char line[PATH_MAX] = {};
                                        FILE *pti = fopen("/etc/path_to_inst", "r");
                                        if (! pti) {
                                                LogError("Cannot open /etc/path_to_inst file\n");
                                                return false;
                                        }
                                        while (fgets(line, sizeof(line), pti)) {
                                                char path[1024] = {};
                                                if (sscanf(line, "\"%1023[^\"]\" %d \"%255[^\"]\"", path, &(device->instance), device->module) == 3) {
                                                        if (IS(path, special)) {
                                                                if (IS(device->module, "cmdk")) {
                                                                        // the "common disk driver" has no "partition" iostat class, only whole "disk" (at least on Solaris 10)
                                                                        snprintf(device->name, sizeof(device->name), "%s%d", device->module, device->instance);
                                                                } else {
                                                                        // use partition for other drivers
                                                                        snprintf(device->name, sizeof(device->name), "%s%d,%c", device->module, device->instance, device->partition);
                                                                }
                                                                rv = true;
                                                                break;
                                                        }
                                                }
                                        }
                                        fclose(pti);
                                }
                        }
                        break;
                }
        }
        fclose(mntfd);
        return rv;
}


static boolean_t _getDiskActivity(char *mountpoint, Info_T inf) {
        boolean_t rv = true;
        struct Device_T device = {};
        if (_getDevice(mountpoint, &device)) {
                kstat_ctl_t *kctl = kstat_open();
                if (kctl) {
                        kstat_t *kstat;
                        for (kstat = kctl->kc_chain; kstat; kstat = kstat->ks_next) {
                                if (kstat->ks_type == KSTAT_TYPE_IO && kstat->ks_instance == device.instance && IS(kstat->ks_module, device.module) && IS(kstat->ks_name, device.name)) {
                                        static kstat_io_t kio;
                                        if (kstat_read(kctl, kstat, &kio) == -1) {
                                                LogError("filesystem statistics error: kstat_read failed -- %s\n", STRERROR);
                                        } else {
                                                uint64_t now = Time_milli();
                                                Statistics_update(&(inf->priv.filesystem.read.bytes), now, kio.nread);
                                                Statistics_update(&(inf->priv.filesystem.write.bytes), now, kio.nwritten);
                                                Statistics_update(&(inf->priv.filesystem.read.operations),  now, kio.reads);
                                                Statistics_update(&(inf->priv.filesystem.write.operations), now, kio.writes);
                                                Statistics_update(&(inf->priv.filesystem.waitTime), now, kio.wtime / 1000000.);
                                                Statistics_update(&(inf->priv.filesystem.runTime), now, kio.rtime / 1000000.);
                                        }
                                }
                        }
                        kstat_close(kctl);
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
        int size =                               usage.f_frsize ? (usage.f_bsize / usage.f_frsize) : 1;
        inf->priv.filesystem.f_bsize =           usage.f_bsize;
        inf->priv.filesystem.f_blocks =          usage.f_blocks / size;
        inf->priv.filesystem.f_blocksfree =      usage.f_bavail / size;
        inf->priv.filesystem.f_blocksfreetotal = usage.f_bfree  / size;
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
        FILE *mntfd = fopen("/etc/mnttab", "r");
        if (! mntfd) {
                LogError("Cannot open /etc/mnttab file\n");
                return NULL;
        }
        struct mnttab mnt;
        while (getmntent(mntfd, &mnt) == 0) {
                if ((realpath(mnt.mnt_special, buf) && IS(buf, dev)) || IS(mnt.mnt_special, dev)) {
                        fclose(mntfd);
                        snprintf(buf, buflen, "%s", mnt.mnt_mountp);
                        return buf;
                }
        }
        fclose(mntfd);
        return NULL;
}


boolean_t filesystem_usage_sysdep(char *mountpoint, Info_T inf) {
        ASSERT(mountpoint);
        ASSERT(inf);
        return (_getDiskUsage(mountpoint, inf) && _getDiskActivity(mountpoint, inf));
}

