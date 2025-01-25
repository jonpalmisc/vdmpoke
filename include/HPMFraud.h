//
//  HPMFraud.h
//
//  Copyright (c) 2024-2025 Jon Palmisciano
//  Copyright (c) 2019 Osy86
//
//  Use of this source code is governed by the Apache 2.0 license; a full copy
//  of the license can be found in the LICENSE.txt file.
//

#pragma once

#include <IOKit/IOTypes.h>

// Forward declaring these to avoid a bigger IOKit include above.
struct IOCFPlugInInterfaceStruct;
struct HPMInterface;

/// HPM client.
///
/// All interactions with HPM are abstracted through this. You are encouraged,
/// if not forbidden, from using the struct members directly. Originally they
/// were void pointers to further-emphasize this.
typedef struct {
    struct IOCFPlugInInterfaceStruct **plugin;
    struct HPMInterface const **interface;
} HPMClient;

/// Open a HPM client with the specified RID.
///
/// Each physical USB-C port has a different RID, allowing the port to use for
/// sending VDMs to be explicitly specified. All ports are capable of sending
/// VDMs, but the target device must receive them on the DFU port.
///
/// On a 14-inch MacBook Pro, the two ports on the left (as you move away from
/// the MagSafe port) have RIDs 0 and 1 respectively; the single port on the
/// right has RID 2. For other products, mappings may vary, but the DFU port
/// always has RID 0.
///
/// \param rid RID of the target HPM instance to match
IOReturn HPMClientOpen(HPMClient *hpm, int32_t rid);

/// Close a HPM client.
void HPMClientClose(HPMClient *hpm);

/// HPM connection type.
typedef enum {
    kHPMConnectionTypeError = -1, ///< Failed to query connection.
    kHPMConnectionTypeNone = 0,   ///< No physical connection.
    kHPMConnectionTypeSource = 1, ///< Source connection; expected state for a physical connection.
    kHPMConnectionTypeSink = 3,   ///< Sink connection; haven't seen this, but it exists.
    kHPMConnectionTypeMask = 3,   ///< Mask to extract connection type.
} HPMConnectionType;

/// Get the current HPM connection state.
HPMConnectionType HPMGetConnectionType(HPMClient const *hpm);

/// HPM operating mode.
typedef enum {
    kHPMModeError = -1, ///< Failed to get mode.
    kHPMModeApp,        ///< Normal application mode.
    kHPMModeDBMA,       ///< DMBa mode.
    kHPMModeUnknown,    ///< Saw unrecognized other mode.
} HPMMode;

/// Get the current HPM mode.
IOReturn HPMGetMode(HPMClient const *hpm, HPMMode *modeOut);

/// Type alias for a buffer suitable for holding a HPM reply.
typedef uint8_t HPMReply[64];

/// Read data from the HPM interface.
///
/// \param chip Target chip
/// \param address Address to read
/// \param flags Additional flags
/// \param[out] reply Buffer to hold reply
/// \param[out] replyLength Size of reply stored to \p reply
IOReturn HPMRead(HPMClient const *hpm, uint64_t chip, uint8_t address,
    uint32_t flags, uint8_t *reply, size_t *replyLength);

/// Known HPM commands.
typedef enum {
    kHPMCommandDBMA = 'DBMa', ///< Enter/exit DBMa mode.
    kHPMCommandGAID = 'Gaid', ///< Reset something?
    kHPMCommandLock = 'LOCK', ///< Unlock ACE?
} HPMCommand;

#define kHPMCommandArg0 ((uint8_t const *)"\x00") // '0' argument shorthand.
#define kHPMCommandArg1 ((uint8_t const *)"\x01") // '1' argument shorthand.

/// Perform a HPM command.
///
/// \param chip Target chip
/// \param command Command to send
/// \param args Command arguments buffer
/// \param argsLength Length of \p args in bytes
/// \param[out] out First byte of the response
IOReturn HPMDoCommand(HPMClient const *hpm, uint64_t chip,
    HPMCommand command, uint8_t const *args, size_t argsLength, uint8_t *out);

/// Send a VDM with an arbitrary body.
///
/// \param chip Target chip
/// \param body VDM body buffer
/// \param bodyLength Length of \p body in bytes
IOReturn HPMSendVDM(HPMClient const *hpm, uint64_t chip, void const *body, size_t bodyLength);

/// Known VDM sequences.
typedef enum {
    kHPMKnownVDMList,     ///< List supported VDM commands.
    kHPMKnownVDMReboot,   ///< Reboot device.
    kHPMKnownVDMDFU,      ///< Send device to DFU.
    kHPMKnownVDMDebugUSB, ///< Pull up Debug USB.
} HPMKnownVDM;

/// Send a known VDM sequence.
IOReturn HPMSendKnownVDM(HPMClient const *hpm, uint64_t chip, HPMKnownVDM knownVDM);

/// Attempt to unlock ACE.
IOReturn HPMUnlockACE(HPMClient const *hpm);
