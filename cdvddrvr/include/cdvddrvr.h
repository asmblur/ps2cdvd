/*
 * cdvddrvr.h - CDVDLIB definitions and imports.
 *
 * Copyright (c) 2004 Herben (herben@napalm-x.com)
 *
 */

#ifndef IOP_CDVDLIB_H
#define IOP_CDVDLIB_H

#include "types.h"
#include "irx.h"

#include "sys/fcntl.h"

#include <types.h>

#define M_bcdtoi(__num) (((((__num) >> 4) & 0x0F) * 10) + ((__num) & 0x0F))
#define M_itobcd(__num) ((((__num) / 10) << 4) + ((__num) % 10))

// Parameters for "Tray Request" system command.
#define TRAY_REQ_OPEN   0
#define TRAY_REQ_CLOSE  1

// Data patterns for "pattern" field of CdRMode_t
#define CdSect2048      0
#define CdSect2328      1
#define CdSect2340      2

#define CddaSect2352    0
#define CddaSect2368    1
#define CddaSect2448    2

// CDVD "S" Commands
enum {
    CdSCmdReadSubQ = 0x02, // 0x02
    CdSCmdSubCmd, // 0x03
    CdSCmdUnk04, // 0x04
    CdSCmdTrayStatus, // 0x05
    CdSCmdTrayReq, // 0x06
    CdSCmdUnk07, // 0x07
    CdSCmdReadRTC, // 0x08
    CdSCmdWriteRTC, // 0x09
    CdSCmdReadNVM, // 0x0A
    CdSCmdWriteNVM, // 0x0B
    CdSCmdSetHDMode, // 0x0C
    CdSCmdSysStandby, // 0x0D
    CdSCmdSysWakeup, // 0x0E
    CdSCmdPowerOff, // 0x0F
    CdSCmdGetSomeVal, // 0x10
    CdSCmdSetSomeVal, // 0x11
    CdSCmdReadILinkID, // 0x12
    CdSCmdWriteILinkID, // 0x13
    CdSCmdCtrlADOut, //0x14
    CdSCmdForbidDVDP, // 0x15
    CdSCmdCtrlAutoAdjust, // 0x16
    CdSCmdReadModelNumber, // 0x17
    CdSCmdWriteModelNumber, // 0x18
    CdSCmdForbidRead, // 0x19
    CdSCmdBootCertify, // 0x1A
    CdSCmdCancelPOffReady, // 0x1B
    CdSCmdSetBlueCtrl, // 0x1C
    CdSCmdUnk1D, // 0x1D
    CdSCmdOpenConfig = 0x40, // 0x40
    CdSCmdReadConfigBlock, // 0x41
    CdSCmdWriteConfigBlock, // 0x42
    CdSCmdCloseConfig, // 0x43
    CdSCmdMG80 = 0x80, // 0x80
    CdSCmdMG81, // 0x81
    CdSCmdMG82, // 0x82
    CdSCmdMG83, // 0x83
    CdSCmdMG84, // 0x84
    CdSCmdMG8C = 0x8C, // 0x8C
    CdSCmdMG90 = 0x90, // 0x90
};

enum {
    CdNCmdNOP = 0x00, // 0x00
    CdNCmdUnk01, // 0x01
    CdNCmdStandby, // 0x02
    CdNCmdStop, // 0x03
    CdNCmdPause, // 0x04
    CdNCmdSeek, // 0x05
    CdNCmdRead, // 0x06
    CdNCmdReadCDDA, // 0x07
    CdNCmdReadDVDV, // 0x08
    CdNCmdReadTOC, // 0x09
    CdNCmdUnk0A, // 0x0A
    CdNCmdUnk0B, // 0x0B
    CdNCmdReadKey, // 0x0C
};

// CDVD "sub-commands"
enum {
    CdSubCmdGetMechaconVer = 0x00, // 0x00
    CdSubCmdWriteConsoleID = 0x44, // 0x44
    CdSubCmdReadConsoleID = 0x45, // 0x45
};

// CDVD streaming commands
enum {
    CdStCmdStart = 1, // 0x01
    CdStCmdRead, // 0x02
    CdStCmdStop, // 0x03
    CdStCmdSeek, // 0x04
    CdStCmdInit, // 0x05
    CdStCmdStat, // 0x06
    CdStCmdPause, // 0x07
    CdStCmdResume, // 0x08
};


// Disc types returned by "Disk Type" register
enum {
    CdDiskNone = 0x00,
    CdDiskDetect, // 0x01
    CdDiskDetectCD, // 0x02
    CdDiskDetectDVD, // 0x03
    CdDiskDetectUnk = 0x05,
    CdDiskCDPS1 = 0x10,
    CdDiskCDDAPS1 = 0x11,
    CdDiskCDPS2 = 0x12,
    CdDiskCDDAPS2 = 0x13,
    CdDiskDVDPS2 = 0x14,
    CdDiskDVDV2 = 0xFC,
    CdDiskCDDA = 0xFD,
    CdDiskDVDV = 0xFE,
    CdDiskIllegal = 0xFF };

// Callback causes for CDVD interrupt handler.
enum {
    CdCbRead = 1,
    CdCbReadCDDA, // 2
    CdCbReadTOC, // 3
    CdCbSeek , // 4
    CdCbStandby, // 5
    CdCbStop, // 6
    CdCbPause, // 7
    CdCbBreak, // 8
    CdCbReadDVDV, // 9
    CdCbInterrupt, // 10
};

// DMA control registers for CDVD DMA channel.

// Note: This register is r/w, and is updated by the DMAC as the transfer progresses.
#define DMAC_CDVD_BUF   ((vu32 *) (0xBF8010B0))

#define DMAC_CDVD_SIZE  ((vu32 *) (0xBF8010B4))

// CDVDMAN writes 0x41000200 to this to start DMA transfers
#define DMAC_CDVD_CTRL  ((vu32 *) (0xBF8010B8))

#define HWREG_BF8010F0  ((vu32 *) (0xBF8010F0))

// CDVD hardware register definitions.
#define CDVD_00         ((vu8 *) (0xBF402000))
#define CDVD_01         ((vu8 *) (0xBF402001))
#define CDVD_02         ((vu8 *) (0xBF402002))
#define CDVD_03         ((vu8 *) (0xBF402003))

// Write: Non-blocking command number
// Read:
#define CDVD_NB_CMD     ((vu8 *) (0xBF402004))

// Write:
//
// Read:
//
#define CDVD_05         ((vu8 *) (0xBF402005))

#define CDVD_06         ((vu8 *) (0xBF402006))

// 0x01 is written to this register to cause a "break" to occur, along with an interrupt.
#define CDVD_07         ((vu8 *) (0xBF402007))

#define CDVD_08         ((vu8 *) (0xBF402008))
#define CDVD_09         ((vu8 *) (0xBF402009))
#define CDVD_TRAY_STAT  ((vu8 *) (0xBF40200A))
#define CDVD_0B         ((vu8 *) (0xBF40200B))
#define CDVD_0C         ((vu8 *) (0xBF40200C))
#define CDVD_0D         ((vu8 *) (0xBF40200D))
#define CDVD_0E         ((vu8 *) (0xBF40200E))
#define CDVD_DISK_TYPE  ((vu8 *) (0xBF40200F))
#define CDVD_10         ((vu8 *) (0xBF402010))
#define CDVD_11         ((vu8 *) (0xBF402011))
#define CDVD_12         ((vu8 *) (0xBF402012))
#define CDVD_13         ((vu8 *) (0xBF402013))
#define CDVD_14         ((vu8 *) (0xBF402014))
#define CDVD_15         ((vu8 *) (0xBF402015))
#define CDVD_16         ((vu8 *) (0xBF402016))
#define CDVD_17         ((vu8 *) (0xBF402017))
#define CDVD_18         ((vu8 *) (0xBF402018))

// Key Data block 0
#define CDVD_KEY_DATA0  ((vu8 *) (0xBF402020))
#define CDVD_KEY_DATA1  ((vu8 *) (0xBF402021))
#define CDVD_KEY_DATA2  ((vu8 *) (0xBF402022))
#define CDVD_KEY_DATA3  ((vu8 *) (0xBF402023))
// Note: the value in this register is used as the XOR key when enabled in the DEC_CTRL register.
#define CDVD_KEY_DATA4  ((vu8 *) (0xBF402024))

// Key Data block 1
#define CDVD_KEY_DATA5  ((vu8 *) (0xBF402028))
#define CDVD_KEY_DATA6  ((vu8 *) (0xBF402029))
#define CDVD_KEY_DATA7  ((vu8 *) (0xBF40202A))
#define CDVD_KEY_DATA8  ((vu8 *) (0xBF40202B))
#define CDVD_KEY_DATA9  ((vu8 *) (0xBF40202C))

// Key Data Block 2
#define CDVD_KEY_DATAA  ((vu8 *) (0xBF402030))
#define CDVD_KEY_DATAB  ((vu8 *) (0xBF402031))
#define CDVD_KEY_DATAC  ((vu8 *) (0xBF402032))
#define CDVD_KEY_DATAD  ((vu8 *) (0xBF402033))
#define CDVD_KEY_DATAE  ((vu8 *) (0xBF402034))

// Flags indicating the format of the key data.
// Bits 0-2 set indicate that the corresponding 5-byte key data block is valid.
// Bits 3-7 are unknown, but may be used to determine additional encryption measures used for DSP<->mechacon communication.
//
#define CDVD_KEY_FLAGS  ((vu8 *) (0xBF402038))

// Key Data Block XOR key
// XOR'd with each byte of each valid Key Data Block to decrypt their values.
#define CDVD_KEY_XOR    ((vu8 *) (0xBF402039))

// Decryption control, enable/disable decryption algorithms on data being read from disc using 5th byte of current
//      key as the XOR key.
// Bit 0 is Data XOR enable, this causes data being read from the disc to be XOR'd with the value of the CDVD_KEY_DATA4
//      register.
// Bit 1 is Rotate Left enable, this causes data being read to be rotated left by a specified number of bits(see Bits 4-6).
// Bit 2 doesn't seem to do anything..
// Bit 3 is Swap Nibble enable, this causes data being read to have the the high and low nibbles of each byte
//      to be swapped.
// Bits 4-7 is the number of bits the data being read will be shifted left byte, if enabled.
#define CDVD_DEC_CTRL   ((vu8 *) (0xBF40203A))

// bitmasks for CDVD_DEC_CTRL
#define CDVD_DEC_XOR_EN     (1 << 0)
#define CDVD_DEC_ROTL_EN    (1 << 1)
#define CDVD_DEC_UNK_EN     (1 << 2)
#define CDVD_DEC_SWAP_EN    (1 << 3)

// CDVD Structures
typedef struct {
    u8 second; // 0 - 60
    u8 minute; // 0 - 60
    u8 hour; // 0 - 24
    u8 dummy; // garbage?
    u8 day; // 0-31, provided it's a valid day for the given month.
    u8 month; // 1-12
    u16 year; // 2000 - 2099
} CdRTCData;

typedef struct {
    u8 tries; // number of retries before considered an error.
    u8 speed; // spindle speed
    u8 pattern; // data pattern
    u8 dummy; // padding?
} CdRMode_t;

// drive head position structure for "pos" functions.
typedef struct {
    u8 minute;
    u8 second;
    u8 lbn;
    u8 track;
} CDPOS;

typedef void (*CdIntrCbFunc)(void);
typedef void (*CdNonBlockCbFunc)(u8);

typedef struct DiscCertifyKey_s
{
    u8 major;   // 0x00
    u8 minor;   // 0x01
    u8 region;  // 0x02
    u8 type;    // 0x03
} BootKey __attribute__ ((packed));

#define cdvddrvr_IMPORTS_start DECLARE_IMPORT_TABLE(cdvddrvr, 0, 2)
#define cdvddrvr_IMPORTS_end END_IMPORT_TABLE

int CdInit(int mode);
#define I_CdInit DECLARE_IMPORT(4, CdInit)

int CdStandby(void);
#define I_CdStandby DECLARE_IMPORT(5, CdStandby)

int CdRead(u32 lbn, u32 nsects, void *buf, CdRMode_t *rmode);
#define I_CdRead DECLARE_IMPORT(6, CdRead)

int CdSeek(u32 lbn);
#define I_CdSeek DECLARE_IMPORT(7, CdSeek)

int CdGetLastError(void);
#define I_CdGetLastError DECLARE_IMPORT(8, CdGetLastError)

int CdReadTOC(void *buf);
#define I_CdReadTOC DECLARE_IMPORT(9, CdReadTOC)

void *CdSearchFile(void *fp, char *name);
#define I_CdSearchFile DECLARE_IMPORT(10, CdSearchFile)

int CdSync(int mode);
#define I_CdSync DECLARE_IMPORT(11, CdSync)

int CdGetDiscType(void);
#define I_CdGetDiscType DECLARE_IMPORT(12, CdGetDiscType)

int CdDiskReady(int mode);
#define I_CdDiskReady DECLARE_IMPORT(13, CdDiskReady)

int CdTrayReq(u8 arg);
#define I_CdTrayReq DECLARE_IMPORT(14, CdTrayReq)

int CdStop(void);
#define I_CdStop DECLARE_IMPORT(15, CdStop)

int CdPosToInt(CDPOS *pos);
#define I_CdPosToInt DECLARE_IMPORT(16, CdPosToInt)

CDPOS *CdIntToPos(int integer, CDPOS *pos);
#define I_CdIntToPos DECLARE_IMPORT(17, CdIntToPos)

int CdReadTOC2(void *buf, u8 arg1);
#define I_CdReadTOC2 DECLARE_IMPORT(19, CdReadTOC2)

int CdReadDVDV(u32 lbn, u32 nsects, void *buf, CdRMode_t *rmode);
#define I_CdReadDVDV DECLARE_IMPORT(20, CdReadDVDV)

int CdCheckCmd(void);
#define I_CdCheckCmd DECLARE_IMPORT(21, CdCheckCmd)

int CdReadILinkID(u8 *idbuf);
#define I_CdReadILinkID DECLARE_IMPORT(22, CdReadILinkID)

int CdWriteILinkID(u8 *idbuf);
#define I_CdWriteILinkID DECLARE_IMPORT(23, CdWriteILinkID)

int CdReadRTC(CdRTCData *rtcData);
#define I_CdReadRTC DECLARE_IMPORT(24, CdReadRTC)

int CdWriteRTC(CdRTCData *rtcData);
#define I_CdWriteRTC DECLARE_IMPORT(25, CdWriteRTC)

int CdReadNVM(u16 addr, u16 *data);
#define I_CdReadNVM DECLARE_IMPORT(26, CdReadNVM)

int CdWriteNVM(u16 addr, u16 data);
#define I_CdWriteNVM DECLARE_IMPORT(27, CdWriteNVM)

int CdGetStatus(void);
#define I_CdGetStatus DECLARE_IMPORT(28, CdGetStatus)

int CdApplySCmd(u8 cmd, u8 *params, u32 plen, u8 *result, int rlen);
#define I_CdApplySCmd DECLARE_IMPORT(29, CdApplySCmd)

int CdSetHDMode(int mode);
#define I_CdSetHDMode DECLARE_IMPORT(30, CdSetHDMode)

int CdOpenConfig(u8 mode, u8 dev, u8 block);
#define I_CdOpenConfig DECLARE_IMPORT(31, CdOpenConfig)

int CdCloseConfig(void);
#define I_CdCloseConfig DECLARE_IMPORT(32, CdCloseConfig)

int CdReadConfigBlock(u8 *data);
#define I_CdReadConfigBlock DECLARE_IMPORT(33, CdReadConfigBlock)

int CdWriteConfigBlock(u8 *data);
#define I_CdWriteConfigBlock DECLARE_IMPORT(34, CdWriteConfigBlock)

int CdReadKey(u8 arg0, u16 arg1, u32 lbn, u8 *buf);
#define I_CdReadKey DECLARE_IMPORT(35, CdReadKey)

int CdDecSet(u8 rotlEn, u8 xorEn, u8 unkEn, u8 swapEn, u8 rotlNum);
#define I_CdDecSet DECLARE_IMPORT(36, CdDecSet)

CdNonBlockCbFunc *CdSetCallback(CdNonBlockCbFunc *cb);
#define I_CdSetCallback DECLARE_IMPORT(37, CdSetCallback)

int CdPause(void);
#define I_CdPause DECLARE_IMPORT(38, CdPause)

int CdBreak(void);
#define I_CdBreak DECLARE_IMPORT(39, CdBreak)

int CdReadCDDA(u32 lbn, u32 nsects, void *buf, CdRMode_t *rmode);
#define I_CdReadCDDA DECLARE_IMPORT(40, CdReadCDDA)

int CdReadConsoleID(u8 *id, int *res);
#define I_CdReadConsoleID DECLARE_IMPORT(41, CdReadConsoleID)

int CdWriteConsoleID(u8 *id, int *res);
#define I_CdWriteConsoleID DECLARE_IMPORT(42, CdWriteConsoleID)

int CdGetMechaconVer(u8 *buf, int *result);
#define I_CdGetMechaconVer DECLARE_IMPORT(43, CdGetMechaconVer)

int CdGetReadPos(void);
#define I_CdGetReadPos DECLARE_IMPORT(44, CdGetReadPos)

int CdCtrlADOut(u8 val);
#define I_CdCtrlADOut DECLARE_IMPORT(45, CdCtrlADOut)

int CdNop(void);
#define I_CdNop DECLARE_IMPORT(46, CdNop)

void *CdGetFsvRbuf(void);
#define I_CdGetFsvRbuf DECLARE_IMPORT(47, CdGetFsvRbuf)

int CdStSetCB0(CdIntrCbFunc *cb);
#define I_CdStSetCB0 DECLARE_IMPORT(48, CdStSetCB0)

int CdStSetCB1(CdIntrCbFunc *cb);
#define I_CdStSetCB1 DECLARE_IMPORT(49, CdStSetCB1)

int CdSpinCtrl(int mode, int *error);
#define I_CdSpinCtrl DECLARE_IMPORT(50, CdSpinCtrl)

int CdReadRTC_Raw(void *buf);
#define I_CdReadRTC_Raw DECLARE_IMPORT(51, CdReadRTC_Raw)

int CdForbidDVDP(u8 *result);
#define I_CdForbidDVDP DECLARE_IMPORT(52, CdForbidDVDP)

int CdReadSubQ(void *buf, u8 *result);
#define I_CdReadSubQ DECLARE_IMPORT(53, CdReadSubQ)

int CdApplyNCmd(u8 cmd, u8 *params, int plen);
#define I_CdApplyNCmd DECLARE_IMPORT(54, CdApplyNCmd)

int CdAutoAdjustCtrl(u8 val);
#define I_CdAutoAdjustCtrl DECLARE_IMPORT(55, CdAutoAdjustCtrl)

int CdStInit(int buf_size, int nbanks, void *buf);
#define I_CdStInit DECLARE_IMPORT(56, CdStInit)

int CdStRead(int nsects, void *buf, int mode, int *result);
#define I_CdStRead DECLARE_IMPORT(57, CdStRead)

int CdStSeek(int lbn);
#define I_CdStSeek DECLARE_IMPORT(58, CdStSeek)

int CdStStart(int lbn, CdRMode_t *rmode);
#define I_CdStStart DECLARE_IMPORT(59, CdStStart)

int CdStStat(void);
#define I_CdStStat DECLARE_IMPORT(60, CdStStat)

int CdStStop(void);
#define I_CdStStop DECLARE_IMPORT(61, CdStStop)

// End of rom0:CDVDMAN compatible exports

#endif /* IOP_CDVDLIB_H */
