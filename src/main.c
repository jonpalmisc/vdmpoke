//
//  vdmpoke.c
//
//  Copyright (c) 2024-2025 Jon Palmisciano
//
//  Use of this source code is governed by the Apache 2.0 license; a full copy
//  of the license can be found in the LICENSE.txt file.
//

#include "HPMFraud.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#define fatalf(...)                   \
    do {                              \
        fprintf(stderr, __VA_ARGS__); \
        exit(1);                      \
    } while (0)

typedef enum {
    CMD_HELP,
    CMD_REBOOT,
    CMD_DFU,
    CMD_DEBUG,
    CMD_CUSTOM,
} cmd_t;

typedef struct {
    char const *prog;
    cmd_t cmd;
    int rid;
    int num_rest;
    char const *rest[8];
} args_t;

int args_parse_int(char const *str, uint64_t *out)
{
    char *end = NULL;
    uint64_t value = strtoul(str, &end, 0);
    if (errno == ERANGE || end == str || *end != 0)
        return 0;

    *out = value;
    return 1;
}

void args_parse(args_t *args, int argc, char **argv)
{
    args->prog = argv[0];
    args->cmd = CMD_HELP;
    args->rid = 0;
    args->num_rest = 0;

    // Silence the default 'getopt' output. We will produce our own error
    // messages as needed.
    opterr = 0;

    int opt_char = 0;
    while ((opt_char = getopt(argc, argv, "r:")) != -1) {
        switch (opt_char) {
        case 'r': {
            uint64_t rid;
            if (args_parse_int(optarg, &rid))
                args->rid = (int)rid;

            break;
        }
        default:
            break;
        }
    }

    if (optind == argc)
        return;

    char const *cmd = argv[optind];
    if (strcmp(cmd, "reboot") == 0)
        args->cmd = CMD_REBOOT;
    else if (strcmp(cmd, "dfu") == 0)
        args->cmd = CMD_DFU;
    else if (strcmp(cmd, "debug") == 0)
        args->cmd = CMD_DEBUG;
    else if (strcmp(cmd, "custom") == 0)
        args->cmd = CMD_CUSTOM;

    for (int i = optind; i < argc; ++i) {
        if (i >= 8)
            break;

        args->rest[i - 2] = argv[i];
        args->num_rest++;
    }
}

void args_help(args_t const *args)
{
    printf("Usage: %s [-r <rid>] <command> [...]\n\n", args->prog);

    puts("Commands:");
    puts("  reboot                Reboot the connected device");
    puts("  dfu                   Send the connected device to DFU mode");
    puts("  debug                 Pull up Debug USB mode on the connected device");
    puts("  custom <word>...      Send a custom VDM");
    puts("  help                  Show this usage info\n");

    puts("Options:");
    puts("  -r <rid>              HPM RID (port number) to match against\n");

    puts("Note:\n  This tool must run with root permissions to perform any useful operations,");
    puts("  which is enforced by AppleHPMUserClient.");
}

static void cli_enter_dbma_mode(HPMClient *hpm)
{
    HPMMode mode;
    IOReturn ret = HPMGetMode(hpm, &mode);
    if (ret != kIOReturnSuccess)
        fatalf("Failed to get HPM mode. (%#x)\n", ret);
    if (mode == kHPMModeDBMA)
        return;

    ret = HPMUnlockACE(hpm);
    if (ret != kIOReturnSuccess)
        fatalf("Failed to unlock ACE. (%#x)\n", ret);

    ret = HPMDoCommand(hpm, 0, kHPMCommandDBMA, kHPMCommandArg1, 1, NULL);
    if (ret != kIOReturnSuccess)
        fatalf("Failed to request DBMa mode. (%#x)\n", ret);

    ret = HPMGetMode(hpm, &mode);
    if (ret != kIOReturnSuccess)
        fatalf("Failed to get HPM mode. (%#x)\n", ret);
    if (mode != kHPMModeDBMA)
        fatalf("Failed to switch to DBMa mode.\n");
}

static void cli_exit_dbma_mode(HPMClient *hpm)
{
    HPMMode mode;
    IOReturn ret = HPMDoCommand(hpm, 0, kHPMCommandDBMA, kHPMCommandArg0, 1, NULL);
    if (ret != kIOReturnSuccess)
        fatalf("Failed to request app mode. (%#X)\n", ret);

    ret = HPMGetMode(hpm, &mode);
    if (ret != kIOReturnSuccess)
        fatalf("Failed to get HPM mode. (%#x)\n", ret);
    if (mode == kHPMModeDBMA)
        fatalf("Failed to switch to app mode.\n");
}

int main(int argc, char **argv)
{
    args_t args;
    args_parse(&args, argc, argv);
    if (args.cmd == CMD_HELP) {
        args_help(&args);
        return 1;
    }

    // Attempting to open a AppleHPMUserClient later will fail if the tool is
    // not running as root, or has the USB-C entitlement; the former is easier.
    if (geteuid() != 0)
        fatalf("Error: Tool must run with root permissions! See help command for more info.\n");

    HPMClient hpm;
    IOReturn ret = HPMClientOpen(&hpm, args.rid);
    if (ret != kIOReturnSuccess)
        fatalf("Failed to open HPM client for RID %d. (%#x)\n", args.rid, ret);

    HPMConnectionType connType = HPMGetConnectionType(&hpm);
    if (connType == kHPMConnectionTypeError)
        fatalf("Failed to get connection type.\n");
    if (connType == kHPMConnectionTypeNone)
        fatalf("No connection found; is a device connected to port %d?\n", args.rid);

    cli_enter_dbma_mode(&hpm);

    switch (args.cmd) {
    case CMD_REBOOT:
        ret = HPMSendKnownVDM(&hpm, 0, kHPMKnownVDMReboot);
        break;
    case CMD_DFU:
        ret = HPMSendKnownVDM(&hpm, 0, kHPMKnownVDMDFU);
        break;
    case CMD_DEBUG:
        ret = HPMSendKnownVDM(&hpm, 0, kHPMKnownVDMDebugUSB);
        break;
    case CMD_CUSTOM: {
        int num_words = 0;
        uint32_t words[8] = { 0 };
        for (int i = 0; i < args.num_rest; ++i) {
            uint32_t word = (uint32_t)strtol(args.rest[i], NULL, 16);
            words[i] = word;
            ++num_words;
        }

        ret = HPMSendVDM(&hpm, 0, words, num_words);
        break;
    }
    default:
        __builtin_unreachable();
    }

    if (ret != kIOReturnSuccess)
        fatalf("Failed to send VDM. (%#x)\n", ret);

    cli_exit_dbma_mode(&hpm);
    HPMClientClose(&hpm);

    return 0;
}
