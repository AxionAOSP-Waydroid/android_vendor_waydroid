#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/sysmacros.h>
#include <linux/limits.h>
#include "android-uid.h"
#include "main.h"

void create_binder_device(int binder_control_fd, char *name) {
    char device_path[PATH_MAX], symlink_path[PATH_MAX];
    struct device_node device;

    snprintf(device_path, PATH_MAX, "/dev/binderfs/%s", name);
    snprintf(symlink_path, PATH_MAX, "/dev/%s", name);
    strncpy(device.name, name, NAME_MAX);

    if (ioctl(binder_control_fd, BINDER_CTL_ADD, &device) < 0) {
        fprintf(stderr, LOG_PREFIX "Failed to allocate new binder device: %s\n", strerror(errno));
        exit(errno);
    }

    // remove existing binder symlink if exist
    if (access(symlink_path, F_OK) == 0) {
        if (remove(symlink_path) != 0) {
            fprintf(stderr, LOG_PREFIX "Failed to remove %s: %s\n", symlink_path, strerror(errno));
            exit(errno);
        }
    }

    // setup permission
    chmod(device_path, 0666);

    // create symlink in /dev for allocated device
    symlink(device_path, symlink_path);
}

void remove_conflicting_devices() {
    char full_path[PATH_MAX];

    for (int i = 0; i < sizeof(conflicting_devices) / sizeof(char *); i++) {
        snprintf(full_path, PATH_MAX, "/dev/%s", conflicting_devices[i]);

        if (access(full_path, F_OK) == 0 && remove(full_path) != 0) {
            fprintf(stderr, LOG_PREFIX "Failed to remove %s: %s\n", full_path, strerror(errno));
            exit(errno);
        }
    }
}

void cleanup_directory(char *dir) {
    DIR *dir_p;
    struct dirent *ent;
    char full_path[PATH_MAX];

    dir_p = opendir(dir);

    while ((ent = readdir(dir_p)) != NULL) {
        if (strncmp(ent->d_name, ".", NAME_MAX) == 0 || strncmp(ent->d_name, "..", NAME_MAX) == 0)
            continue;

        snprintf(full_path, PATH_MAX, "%s/%s", dir, ent->d_name);

        if (remove(full_path) != 0) {
            fprintf(stderr, LOG_PREFIX "remove() failed: %s\n", strerror(errno));
            exit(errno);
        }
    }
}

int main(int argc, char **argv) {
    bool is_docker = false;
    char render_node[PATH_MAX];
    int render_node_id = 128,
        opt,
        binder_fd;

    while ((opt = getopt(argc, argv, "dr::")) != -1) {
        switch (opt) {
        case 'd':
            is_docker = true;
            break;
        case 'r':
            render_node_id = atoi(optarg);
            break;
        default:
            fprintf(stderr, LOG_PREFIX "Usage: %s [-d] [-r DRM_RENDER_NODE_ID]\n", argv[0]);
            exit(EXIT_FAILURE);
            break;
        }
    }

    snprintf(render_node, PATH_MAX, "/dev/dri/renderD%i", render_node_id);
    fprintf(stderr, LOG_PREFIX "Using DRM device %s\n", render_node);

    umask(0);

    // add missing devices
    char full_path[PATH_MAX];

    for (int i = 0; i < ARRAY_LENGTH(necessary_devices); i++) {
        snprintf(full_path, PATH_MAX, "/dev/%s", necessary_devices[i].name);

        if (access(full_path, F_OK) != 0 &&
            mknod(full_path, S_IFCHR | 0666, makedev(necessary_devices[i].major, necessary_devices[i].minor)) != 0)
        {

            fprintf(stderr, LOG_PREFIX "Failed to create %s device: %s\n", necessary_devices[i].name, strerror(errno));
            return errno;
        }
    }

    // fix directory permissions
    chmod("/dev", S_ISVTX | 0777);
    chmod("/dev/tty", 0666);
    chmod("/dev/fuse", 0600);

    chmod("/dev/uhid", 0660);
    chown("/dev/uhid", AID_UHID, AID_UHID);

    chmod("/dev/tun", 0660);
    chown("/dev/tun", AID_SYSTEM, AID_VPN);

    chmod("/dev/dma_heap/system", 0444);
    chown("/dev/dma_heap/system", AID_SYSTEM, AID_SYSTEM);

    // remount /dev with nosuid for security
    if (mount("none", "/dev", "tmpfs", MS_REMOUNT | MS_NOSUID, NULL) != 0) {
        fprintf(stderr, LOG_PREFIX "Failed to remount /dev: %s\n", strerror(errno));
        return errno;
    }

    // create render device if doesn't exist
    if (access(render_node, F_OK) != 0) {
        mkdir("/dev/dri", 0755);
        mknod(render_node, S_IFCHR | 0666, makedev(226, render_node_id));
        chown(render_node, AID_ROOT, AID_GRAPHICS);
    }

    // remove devices that will created by android init instead
    remove_conflicting_devices();

    // setup binderfs mount and create binder devices
    if (access("/dev/binderfs", F_OK) != 0 && mkdir("/dev/binderfs", 0755) != 0) {
        fprintf(stderr, LOG_PREFIX "Failed to create binderfs directory: %s\n", strerror(errno));
        return errno;
    }

    if (mount("binder", "/dev/binderfs", "binder", 0, "stats=global") != 0) {
        fprintf(stderr, LOG_PREFIX "Failed to mount binderfs: %s\n", strerror(errno));
        return errno;
    }

    if ((binder_fd = open("/dev/binderfs/binder-control", O_RDONLY | O_CLOEXEC)) == -1) {
        fprintf(stderr, LOG_PREFIX "Failed to open binder-control: %s\n", strerror(errno));
        return errno;
    }

    for (int i = 0; i < ARRAY_LENGTH(binder_devices); i++)
        create_binder_device(binder_fd, binder_devices[i]);

    // remove /dev/input/*, otherwise waydroid will try to access input devices from host
    if (access("/dev/input", F_OK) == 0) {
        cleanup_directory("/dev/input");

        if (rmdir("/dev/input") != 0) {
            fprintf(stderr, LOG_PREFIX "Failed to remove /dev/input: %s\n", strerror(errno));
            return errno;
        }
    }

    // remount /sys as read-only to prevent unexpected issues
    if (mount("sysfs", "/sys", "sysfs", MS_REMOUNT | MS_RDONLY, NULL) != 0) {
        fprintf(stderr, LOG_PREFIX "Failed to remount sysfs: %s\n", strerror(errno));
        return errno;
    }

    if (is_docker) {
        // bind mount /system/etc to /etc, as docker will override etc dir
        if (mount("/system/etc", "/etc", (char *)NULL, MS_BIND, NULL) != 0) {
            fprintf(stderr, LOG_PREFIX "/system/etc -> /etc: mount() failed: %s\n", strerror(errno));
            return errno;
        }

        // generate ipconfig.txt for networking
        setup_ipconfig_txt();
    }

    fprintf(stderr, LOG_PREFIX "Passing control to Android init\n");
    execl("/init", "/init", NULL);
}
