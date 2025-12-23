#pragma once
#include <stdint.h>

extern void setup_ipconfig_txt();

struct device_node {
    char name[NAME_MAX + 1];
    uint32_t major;
    uint32_t minor;
};

#define LOG_PREFIX "[waydroid-preinit] "
#define BINDER_CTL_ADD _IOWR('b', 1, struct device_node)
#define ARRAY_LENGTH(x) sizeof(x) / sizeof(x[0])

struct device_node necessary_devices[] = {
    { "zero", 1, 5 },
    { "null", 1, 3 },
    { "full", 1, 7 },
    { "tty", 5, 0 },
    { "fuse", 10, 229 },
    { "uhid", 10, 239 },
    { "tun", 10, 200 }
};

char *binder_devices[] = {
    "binder",
    "hwbinder",
    "vndbinder"
};

char *conflicting_devices[] = {
    "kmsg",
    "random",
    "urandom"
};
