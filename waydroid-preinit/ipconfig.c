#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <endian.h>
#include <errno.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include "android-uid.h"

#define IPCONFIG_TXT "/data/misc/ethernet/ipconfig.txt"

void write_u16(int fd, uint16_t data) {
    uint16_t packedData = htobe16(data);
    write(fd, &packedData, sizeof(packedData));
}

void write_u32(int fd, uint32_t data) {
    uint32_t packedData = htobe32(data);
    write(fd, &packedData, sizeof(packedData));
}

void write_str(int fd, char *str) {
    uint32_t strLength = strlen(str);

    write_u16(fd, strLength);
    write(fd, str, strLength);
}

void setup_ipconfig_txt() {
    int fd;

    if (access("/data/misc", F_OK) != 0) {
        mkdir("/data/misc", S_ISVTX | 0770);
        chown("/data/misc", AID_SYSTEM, AID_MISC);
    }

    if (access("/data/misc/ethernet", F_OK) != 0) {
        mkdir("/data/misc/ethernet", 0770);
        chown("/data/misc/ethernet", AID_SYSTEM, AID_SYSTEM);
    }

    if ((fd = open(IPCONFIG_TXT, O_CREAT | O_WRONLY, 0644)) == -1) {
        fprintf(stderr, IPCONFIG_TXT ": open() failed: %s\n", strerror(errno));
        exit(errno);
    }

    write_u32(fd, 3); // ipconfig.txt version 3

    write_str(fd, "ipAssignment");
    write_str(fd, "STATIC");

    write_str(fd, "linkAddress");
    write_str(fd, "172.17.0.2");
    write_u32(fd, 16);

    write_str(fd, "gateway");
    write_u32(fd, 1); // Default route (dest)
    write_str(fd, "0.0.0.0");
    write_u32(fd, 0);
    write_u32(fd, 1); // Have a gateway.
    write_str(fd, "172.17.0.1");

    write_str(fd, "dns");
    write_str(fd, "1.1.1.1");

    write_str(fd, "proxySettings");
    write_str(fd, "NONE");

    write_str(fd, "id");
    write_str(fd, "eth0");

    write_str(fd, "eos");

    close(fd);

    // change ownership and setup permission
    chown(IPCONFIG_TXT, AID_SYSTEM, AID_SYSTEM);
    chmod(IPCONFIG_TXT, 0644);
}
