//
//  HPMFraud.c
//
//  Copyright (c) 2024-2025 Jon Palmisciano
//  Copyright (c) 2019 Osy86
//
//  Use of this source code is governed by the Apache 2.0 license; a full copy
//  of the license can be found in the LICENSE.txt file.
//

#include "HPMFraud.h"

#include <CoreFoundation/CFNumber.h>
#include <IOKit/IOCFPlugIn.h>

#include <stdio.h>

// Set to 1 below (or override in compile flags) for additional debug output.
#ifndef HPMFRAUD_CONFIG_DEBUG
#define HPMFRAUD_CONFIG_DEBUG 0
#endif

#if HPMFRAUD_CONFIG_DEBUG
#define HPMDebug(...)                                                            \
    do {                                                                         \
        HPMDebugWithContext(__FILE_NAME__, __LINE__, __FUNCTION__, __VA_ARGS__); \
    } while (0)

#else
#define HPMDebug(...) \
    do {              \
    } while (0)
#endif

static void HPMDebugWithContext(char const *file, int line, char const *func, char const *fmt, ...) __printflike(4, 5)
{
    fprintf(stderr, "\x1b[34m%s(%s:%d): ", func, file, line);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\x1b[0m\n");
}

#define IO_TRY(STMT)                      \
    do {                                  \
        IOReturn _try_ret = STMT;         \
        if (_try_ret != kIOReturnSuccess) \
            return _try_ret;              \
    } while (0)

static IOReturn HPMFindService(int32_t targetRID, io_service_t *service)
{
    if (!service)
        return kIOReturnBadArgument;

    io_iterator_t devices = IO_OBJECT_NULL;
    CFMutableDictionaryRef matching = IOServiceMatching("AppleHPM");
    IO_TRY(IOServiceGetMatchingServices(kIOMainPortDefault, matching, &devices));

    io_service_t device = IO_OBJECT_NULL;
    while ((device = IOIteratorNext(devices)) != IO_OBJECT_NULL) {
        CFNumberRef ridNum = IORegistryEntryCreateCFProperty(device, CFSTR("RID"), kCFAllocatorDefault, 0);
        if (!ridNum)
            return kIOReturnError;

        int32_t rid = 0;
        CFNumberGetValue(ridNum, kCFNumberSInt32Type, &rid);
        CFRelease(ridNum);

        if (rid != targetRID) {
            IOObjectRelease(device);
            continue;
        }

        *service = device;
        return kIOReturnSuccess;
    }

    IOObjectRelease(devices);
    return kIOReturnNotFound;
}

#define kHPMPluginID                                                                              \
    CFUUIDGetConstantUUIDWithBytes(kCFAllocatorDefault, 0x12, 0xA1, 0xDC, 0xCF, 0xCF, 0x7A, 0x47, \
        0x75, 0xBE, 0xE5, 0x9C, 0x43, 0x19, 0xF4, 0xCD, 0x2B)
#define kHPMInterfaceID                                                                           \
    CFUUIDGetConstantUUIDWithBytes(kCFAllocatorDefault, 0xC1, 0x3A, 0xCD, 0xD9, 0x20, 0x9E, 0x4B, \
        0x01, 0xB7, 0xBE, 0xE0, 0x5C, 0xD8, 0x83, 0xC7, 0xB1)

typedef struct HPMInterface HPMInterface;
struct HPMInterface {
    IUNKNOWN_C_GUTS;
    uint64_t unused;

    IOReturn (*Read)(HPMInterface const **, uint64_t chip, uint8_t address,
        void const *buffer, size_t length, uint32_t flags, uint64_t *readLength);
    IOReturn (*Write)(HPMInterface const **, uint64_t chip, uint8_t address,
        void const *buffer, size_t length, uint32_t flags);
    IOReturn (*Command)(HPMInterface const **, uint64_t chip, uint32_t command, uint32_t flags);

    IOReturn (*SendVDM)(HPMInterface const **, uint64_t device, int arg, void const *buffer, size_t length, uint32_t flags);
};

IOReturn HPMClientOpen(HPMClient *hpm, int32_t rid)
{
    io_service_t service = IO_OBJECT_NULL;
    IO_TRY(HPMFindService(rid, &service));

    SInt32 score = 0;
    IOCFPlugInInterface **plugin = NULL;
    IO_TRY(IOCreatePlugInInterfaceForService(service, kHPMPluginID, kIOCFPlugInInterfaceID, &plugin, &score));

    HPMInterface const **interface;
    HRESULT res = (*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(kHPMInterfaceID), (LPVOID *)&interface);
    if (res != S_OK)
        return kIOReturnError;

    hpm->plugin = plugin;
    hpm->interface = interface;
    return kIOReturnSuccess;
}

void HPMClientClose(HPMClient *hpm)
{
    IODestroyPlugInInterface(hpm->plugin);
    hpm->plugin = NULL;
    hpm->interface = NULL;
}

HPMConnectionType HPMGetConnectionType(HPMClient const *hpm)
{
    size_t length = 0;
    HPMReply reply;
    IOReturn ret = HPMRead(hpm, 0, 0x3f, 0, reply, &length);
    if (ret != kIOReturnSuccess || !length) {
        HPMDebug("Failed to get connection type. (%#x, %#zx)", ret, length);
        return kHPMConnectionTypeError;
    }

    return reply[0] & kHPMConnectionTypeMask;
}

IOReturn HPMGetMode(HPMClient const *hpm, HPMMode *modeOut)
{
    size_t length = 0;
    HPMReply reply;
    IO_TRY(HPMRead(hpm, 0, 0x3, 0, reply, &length));
    if (length < 4)
        return kIOReturnUnderrun;

    if (memcmp(reply, "APP", 3) == 0)
        *modeOut = kHPMModeApp;
    else if (memcmp(reply, "DBMa", 4) == 0)
        *modeOut = kHPMModeDBMA;
    else
        *modeOut = kHPMModeUnknown;

    return kIOReturnSuccess;
}

IOReturn HPMRead(HPMClient const *hpm, uint64_t chip, uint8_t address,
    uint32_t flags, uint8_t *reply, size_t *replyLength)
{
    HPMDebug("chip=%#llx, address=%#x, flags=%#x", chip, address, flags);

    uint64_t length = 0;
    IO_TRY((*hpm->interface)->Read(hpm->interface, chip, address, reply, sizeof(HPMReply), flags, &length));

    *replyLength = length;
    return kIOReturnSuccess;
}

IOReturn HPMDoCommand(HPMClient const *hpm, uint64_t chip,
    HPMCommand command, uint8_t const *args, size_t argsLength, uint8_t *out)
{
    HPMDebug("chip=%#llx, command=%#x", chip, command);

    if (args && argsLength) {
        IOReturn ret = (*hpm->interface)->Write(hpm->interface, chip, 9, args, argsLength, 0);
        if (ret != kIOReturnSuccess) {
            HPMDebug("Failed to write arguments. (%#x)", ret);
            return ret;
        }
    }

    IOReturn ret = (*hpm->interface)->Command(hpm->interface, chip, command, 0);
    if (ret != kIOReturnSuccess) {
        HPMDebug("Failed to issue command. (%#x)", ret);
        return ret;
    }

    size_t length = 0;
    HPMReply reply;
    ret = HPMRead(hpm, chip, 9, 0, reply, &length);
    if (ret != kIOReturnSuccess || !length) {
        HPMDebug("Failed to read command reply. (%#x, %#zx)", ret, length);
        return ret;
    }

    if (out)
        *out = reply[0] & 0xf;
    return kIOReturnSuccess;
}

typedef uint8_t VDMBuffer[128];

IOReturn HPMSendVDM(HPMClient const *hpm, uint64_t chip, void const *body, size_t bodyLength)
{
#if HPMFRAUD_CONFIG_DEBUG
    char previewBuf[256] = { 0 };
    char *previewHead = previewBuf;

    uint32_t *words = (uint32_t *)body;
    size_t numWords = bodyLength / sizeof(uint32_t);
    for (size_t i = 0; i < numWords; ++i) {
        previewHead += sprintf(previewHead, "%#x", words[i]);
        if (i != numWords - 1)
            previewHead += sprintf(previewHead, ", ");
    }

    HPMDebug("chip=%#llx, body=[%s]", chip, previewBuf);
#endif

    return (*hpm->interface)->SendVDM(hpm->interface, chip, 3, body, bodyLength, 0);
}

/// VDM main commands.
typedef enum {
    kVDMCommandList = 0x5ac8010,   ///< Get supported actions.
    kVDMCommandInfo = 0x5ac8011,   ///< Get info for an action.
    kVDMCommandAction = 0x5ac8012, ///< Perform an action.
} VDMCommand;

/// VDM actions used with the "perform" command.
typedef enum {
    kVDMActionReboot = 0x105,    ///< Reboot the device.
    kVDMActionDFU = 0x106,       ///< Go to DFU mode.
    kVDMActionDebugUSB = 0x4606, ///< Pull up Debug USB.
} VDMAction;

/// VDM action flags.
typedef enum {
    kVDMFlagsLine1 = (1 << 17),    ///< Map line 1.
    kVDMFlagsGraceful = (1 << 23), ///< Exit conflicting modes if possible.
    kVDMFlagsPersist = (1 << 24),  ///< Persist through soft reset.
    kVDMFlagsExit = (1 << 25),     ///< Exit mode (instead of enter).
} VDMFlags;

IOReturn HPMSendKnownVDM(HPMClient const *hpm, uint64_t chip, HPMKnownVDM knownVDM)
{
    HPMDebug("chip=%#llx, knownVDM=%d", chip, knownVDM);

    switch (knownVDM) {
    case kHPMKnownVDMList: {
        static uint32_t s_body_list[] = { kVDMCommandList };
        return HPMSendVDM(hpm, chip, s_body_list, sizeof(s_body_list));
    }
    case kHPMKnownVDMReboot: {
        static uint32_t s_body_reboot[] = {
            kVDMCommandAction,
            kVDMActionReboot,
            0x80000000,
        };
        return HPMSendVDM(hpm, chip, s_body_reboot, sizeof(s_body_reboot));
    }
    case kHPMKnownVDMDFU: {
        static uint32_t s_body_debug_dfu[] = {
            kVDMCommandAction,
            kVDMActionDFU,
            0x80010000,
        };
        return HPMSendVDM(hpm, chip, s_body_debug_dfu, sizeof(s_body_debug_dfu));
    }
    case kHPMKnownVDMDebugUSB: {
        static uint32_t s_body_debug[] = {
            kVDMCommandAction,
            kVDMFlagsGraceful | kVDMFlagsLine1 | kVDMActionDebugUSB,
        };
        return HPMSendVDM(hpm, chip, s_body_debug, sizeof(s_body_debug));
    }
    default:
        return kIOReturnError;
    }
}

static uint8_t const *HPMGetACEUnlockKey(void)
{
    // Avoid calling into IOKit multiple times.
    static uint32_t sKey = 0;
    if (sKey)
        return (uint8_t const *)&sKey;

    CFMutableDictionaryRef matching = IOServiceMatching("IOPlatformExpertDevice");
    io_service_t service = IOServiceGetMatchingService(kIOMainPortDefault, matching);
    if (!service) {
        HPMDebug("Failed to get platform expert service.");
        return NULL;
    }

    io_name_t name;
    if (IORegistryEntryGetName(service, name) != kIOReturnSuccess) {
        HPMDebug("Failed to get registry entry name.");
        return NULL;
    }

    IOObjectRelease(service);

    sKey = (name[0] << 24) | (name[1] << 16) | (name[2] << 8) | name[3];
    return (uint8_t const *)&sKey;
}

IOReturn HPMUnlockACE(HPMClient const *hpm)
{
    IOReturn ret = HPMDoCommand(hpm, 0, kHPMCommandLock, HPMGetACEUnlockKey(), 4, NULL);
    if (ret == kIOReturnSuccess)
        return ret;

    // Sometimes the attmpet above doesn't work right away; try figuratively
    // taking the game cartridge out and blowing air on it...
    IO_TRY(HPMDoCommand(hpm, 0, kHPMCommandGAID, NULL, 0, NULL));
    return HPMDoCommand(hpm, 0, kHPMCommandLock, HPMGetACEUnlockKey(), 4, NULL);
}
