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
#include <strings.h>
#endif

#ifdef HAVE_KSTAT_H
#include <kstat.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_SYS_STATVFS_H
#include <sys/statvfs.h>
#endif

#ifdef HAVE_SYS_MNTENT_H
#include <sys/mntent.h>
#endif

#ifdef HAVE_SYS_MNTTAB_H
#include <sys/mnttab.h>
#endif

#ifdef HAVE_LIBZFS_H
#include <libzfs.h>
#endif

#ifdef HAVE_NVPAIR_H
#include <sys/nvpair.h>
#endif

#ifdef HAVE_FS_ZFS_H
#include <sys/fs/zfs.h>
#endif

#include "monit.h"

// libmonit
#include "system/Time.h"
#include "io/File.h"


/* ------------------------------------------------------------- Definitions */


#define PATHTOINST "/etc/path_to_inst"


static struct {
        int generation;     // Increment each time the mount table is changed
        uint64_t timestamp; // /etc/mnttab timestamp [ms] (changed on mount/unmount)
} _statistics = {};


/* ----------------------------------------------------------------- Private */


static boolean_t _getDummyDiskActivity(void *_inf) {
        return true;
}


static boolean_t _getZfsDiskActivity(void *_inf) {
        Info_T inf = _inf;
        boolean_t rv = false;
        libzfs_handle_t *z = libzfs_init();
        libzfs_print_on_error(z, 1);
        zpool_handle_t *zp = zpool_open_canfail(z, inf->priv.filesystem.object.key);
        if (zp) {
                nvlist_t *zpoolConfig = zpool_get_config(zp, NULL);
                nvlist_t *zpoolVdevTree = NULL;
                if (nvlist_lookup_nvlist(zpoolConfig, ZPOOL_CONFIG_VDEV_TREE, &zpoolVdevTree) == 0) {
                        vdev_stat_t *zpoolStatistics = NULL;
                        uint_t zpoolStatisticsCount = 0;
                        if (nvlist_lookup_uint64_array(zpoolVdevTree, ZPOOL_CONFIG_VDEV_STATS, (uint64_t **)&zpoolStatistics, &zpoolStatisticsCount) == 0) {
                                //FIXME: if the zpool state has error, trigger the fs event, can also report number of read/write/checksum errors (see vdev_stat_t in /usr/include/sys/fs/zfs.h)
                                DEBUG("ZFS pool '%s' state: %s\n", inf->priv.filesystem.object.key, zpool_state_to_name(zpoolStatistics->vs_state, zpoolStatistics->vs_aux));
                                uint64_t now = Time_milli();
                                Statistics_update(&(inf->priv.filesystem.read.bytes), now, zpoolStatistics->vs_bytes[ZIO_TYPE_READ]);
                                Statistics_update(&(inf->priv.filesystem.write.bytes), now, zpoolStatistics->vs_bytes[ZIO_TYPE_WRITE]);
                                Statistics_update(&(inf->priv.filesystem.read.operations),  now, zpoolStatistics->vs_ops[ZIO_TYPE_READ]);
                                Statistics_update(&(inf->priv.filesystem.write.operations), now, zpoolStatistics->vs_ops[ZIO_TYPE_WRITE]);
                                rv = true;
                        }
                }
                zpool_close(zp);
        }
        libzfs_fini(z);
        return rv;
}


static boolean_t _getKstatDiskActivity(void *_inf) {
        Info_T inf = _inf;
        boolean_t rv = false;
        kstat_ctl_t *kctl = kstat_open();
        if (kctl) {
                kstat_t *kstat;
                for (kstat = kctl->kc_chain; kstat; kstat = kstat->ks_next) {
                        if (kstat->ks_type == KSTAT_TYPE_IO && kstat->ks_instance == inf->priv.filesystem.object.instance && IS(kstat->ks_module, inf->priv.filesystem.object.module) && IS(kstat->ks_name, inf->priv.filesystem.object.key)) {
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
                                        rv = true;
                                }
                        }
                }
                kstat_close(kctl);
        }
        return rv;
}


static boolean_t _getDiskUsage(void *_inf) {
        Info_T inf = _inf;
        struct statvfs usage;
        if (statvfs(inf->priv.filesystem.object.mountpoint, &usage) != 0) {
                LogError("Error getting usage statistics for filesystem '%s' -- %s\n", inf->priv.filesystem.object.mountpoint, STRERROR);
                return false;
        }
        int size = usage.f_frsize ? (usage.f_bsize / usage.f_frsize) : 1;
        inf->priv.filesystem.f_bsize = usage.f_bsize;
        inf->priv.filesystem.f_blocks = usage.f_blocks / size;
        inf->priv.filesystem.f_blocksfree = usage.f_bavail / size;
        inf->priv.filesystem.f_blocksfreetotal = usage.f_bfree  / size;
        inf->priv.filesystem.f_files = usage.f_files;
        inf->priv.filesystem.f_filesfree = usage.f_ffree;
        inf->priv.filesystem._flags = inf->priv.filesystem.flags;
        inf->priv.filesystem.flags = usage.f_flag;
        return true;
}


static boolean_t _compareMountpoint(const char *mountpoint, struct extmnttab *mnt) {
        return IS(mountpoint, mnt->mnt_mountp);
}


static boolean_t _compareDevice(const char *device, struct extmnttab *mnt) {
        char target[PATH_MAX] = {};
        return (IS(device, mnt->mnt_special) || (realpath(mnt->mnt_special, target) && IS(device, target)));
}


static boolean_t _setDevice(Info_T inf, const char *path, boolean_t (*compare)(const char *path, struct extmnttab *mnt)) {
        FILE *f = fopen(MNTTAB, "r");
        if (! f) {
                LogError("Cannot open %s\n", MNTTAB);
                return false;
        }
        resetmnttab(f);
        struct extmnttab mnt;
        boolean_t rv = false;
        inf->priv.filesystem.object.generation = _statistics.generation;
        while (getextmntent(f, &mnt, sizeof(struct extmnttab)) == 0) {
                if (compare(path, &mnt)) {
                        strncpy(inf->priv.filesystem.object.device, mnt.mnt_special, sizeof(inf->priv.filesystem.object.device) - 1);
                        strncpy(inf->priv.filesystem.object.mountpoint, mnt.mnt_mountp, sizeof(inf->priv.filesystem.object.mountpoint) - 1);
                        strncpy(inf->priv.filesystem.object.type, mnt.mnt_fstype, sizeof(inf->priv.filesystem.object.type) - 1);
                        inf->priv.filesystem.object.getDiskUsage = _getDiskUsage; // The disk usage method is common for all filesystem types
                        if (Str_startsWith(mnt.mnt_fstype, MNTTYPE_NFS)) {
                                strncpy(inf->priv.filesystem.object.module, "nfs", sizeof(inf->priv.filesystem.object.module) - 1);
                                snprintf(inf->priv.filesystem.object.key, sizeof(inf->priv.filesystem.object.key), "nfs%d", mnt.mnt_minor);
                                inf->priv.filesystem.object.instance = mnt.mnt_minor;
                                inf->priv.filesystem.object.getDiskActivity = _getKstatDiskActivity;
                                rv = true;
                        } else if (IS(mnt.mnt_fstype, MNTTYPE_ZFS)) {
                                strncpy(inf->priv.filesystem.object.module, "zfs", sizeof(inf->priv.filesystem.object.module) - 1);
                                char *slash = strchr(mnt.mnt_special, '/');
                                strncpy(inf->priv.filesystem.object.key, mnt.mnt_special, slash ? slash - mnt.mnt_special : sizeof(inf->priv.filesystem.object.key) - 1);
                                inf->priv.filesystem.object.getDiskActivity = _getZfsDiskActivity;
                                rv = true;
                        } else if (IS(mnt.mnt_fstype, MNTTYPE_UFS)) {
                                char special[PATH_MAX];
                                if (! realpath(mnt.mnt_special, special)) {
                                        // If the file doesn't exist it's a virtual filesystem -> ENOENT doesn't mean error
                                        if (errno != ENOENT && errno != ENOTDIR)
                                                LogError("Lookup for '%s' filesystem failed -- %s\n", path, STRERROR);
                                } else if (! Str_startsWith(special, "/devices/")) {
                                        LogError("Lookup for '%s' filesystem -- invalid device %s\n", path, special);
                                } else {
                                        // Strip "/devices" prefix and :X partition postfix: /devices/pci@0,0/pci15ad,1976@10/sd@0,0:a -> /pci@0,0/pci15ad,1976@10/sd@0,0
                                        int speclen = strlen(special);
                                        int devlen = strlen("/devices");
                                        int len = speclen - devlen - 2;
                                        inf->priv.filesystem.object.partition = *(special + speclen - 1);
                                        memmove(special, special + devlen, len);
                                        special[len] = 0;
                                        char line[PATH_MAX] = {};
                                        FILE *pti = fopen(PATHTOINST, "r");
                                        if (! pti) {
                                                LogError("Cannot open %s\n", PATHTOINST);
                                        } else {
                                                while (fgets(line, sizeof(line), pti)) {
                                                        char path[1024] = {};
                                                        if (sscanf(line, "\"%1023[^\"]\" %d \"%255[^\"]\"", path, &(inf->priv.filesystem.object.instance), inf->priv.filesystem.object.module) == 3) {
                                                                if (IS(path, special)) {
                                                                        if (IS(inf->priv.filesystem.object.module, "cmdk")) {
                                                                                // the "common disk driver" has no "partition" iostat class, only whole "disk" (at least on Solaris 10)
                                                                                snprintf(inf->priv.filesystem.object.key, sizeof(inf->priv.filesystem.object.key), "%s%d", inf->priv.filesystem.object.module, inf->priv.filesystem.object.instance);
                                                                        } else {
                                                                                // use partition for other drivers
                                                                                snprintf(inf->priv.filesystem.object.key, sizeof(inf->priv.filesystem.object.key), "%s%d,%c", inf->priv.filesystem.object.module, inf->priv.filesystem.object.instance, inf->priv.filesystem.object.partition);
                                                                        }
                                                                        inf->priv.filesystem.object.getDiskActivity = _getKstatDiskActivity;
                                                                        rv = true;
                                                                        break;
                                                                }
                                                        }
                                                }
                                                fclose(pti);
                                        }
                                }
                        } else {
                                inf->priv.filesystem.object.getDiskActivity = _getDummyDiskActivity;
                                rv = true;
                        }
                        fclose(f);
                        inf->priv.filesystem.object.mounted = rv;
                        return rv;
                }
        }
        LogError("Lookup for '%s' filesystem failed  -- not found in %s\n", path, MNTTAB);
        fclose(f);
        inf->priv.filesystem.object.mounted = false;
        return false;
}


static boolean_t _getDevice(Info_T inf, const char *path, boolean_t (*compare)(const char *path, struct extmnttab *mnt)) {
        struct stat sb;
        if (stat(MNTTAB, &sb) != 0 || _statistics.timestamp != (uint64_t)((double)sb.st_mtim.tv_sec * 1000. + (double)sb.st_mtim.tv_nsec / 1000000.)) {
                DEBUG("Mount notification: change detected\n");
                _statistics.timestamp = (double)sb.st_mtim.tv_sec * 1000. + (double)sb.st_mtim.tv_nsec / 1000000.;
                _statistics.generation++; // Increment, so all other filesystems can see the generation has changed
        }
        if (inf->priv.filesystem.object.generation != _statistics.generation) {
                _setDevice(inf, path, compare); // The mount table has changed => refresh
        }
        if (inf->priv.filesystem.object.mounted) {
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

