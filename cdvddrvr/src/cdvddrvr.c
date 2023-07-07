
// This should be somewhere else..
#define IOP_IRQ_CDVD    2

#define MAJOR_VER       0x00
#define MINOR_VER       0x03

#include "types.h"
#include "defs.h"
#include "irx.h"

#include "loadcore.h"
#include "intrman.h"
#include "sifman.h"
#include "stdio.h"
#include "sysclib.h"
#include "thbase.h"
#include "thsemap.h"

#include "cdvddrvr.h"
#include "mgmecha.h"

#define VERBOSE_LEVEL   0
#define DBG_printf(__level, __args...) if(__level <= VERBOSE_LEVEL) { printf(__args); }

IRX_ID("CDVDDRVR", MAJOR_VER, MINOR_VER);

struct irx_export_table _exp_cdvddrvr;

// Pointer to currently set "CdSearchFile" function handler.
void *(*_CdSearchFilePtr)(void *, char *) = NULL;

typedef struct {
    u8 lastCmd;
    u8 lastError;
    u8 unk02;
    u8 unk03;
    int cmdActive;
    int _lastThreadID;
    int spin_ctrl;
} cdvdCommon;

// Local prototypes
int _cdvdIntrHandler(void *_common);
u8 _addSumMemBlock(u8 *block);

// non-exported functions
int CdSubCommand(u8 *params, int numParams, u8 *results, int numResults);
int CdUnkSCmd04(u8 *buf, u8 *status);
int CdTrayStatus(u8 *status);
int CdSysStandby(void);
int CdSysWakeup(void);
int CdPowerOff(void);
int CdGetSomeVal(u8 *buf);
int CdSetSomeVal(u8 val);
int CdReadModelNumber(char *model);
int CdWriteModelNumber(char *model);
int CdForbidRead(u8 *result);
int CdBootCertify(BootKey *key);
int CdReadConfig(u8 *data);
int CdWriteConfig(u8 *data);

// Global variables
static void *_lastReadBuf;

static int _nCmdSemaID, _sCmdSemaID;
CdRMode_t _St_rmode;
static CdNonBlockCbFunc *_nonBlockCbFunc;
static CdIntrCbFunc *_intrCbFunc0, *_intrCbFunc1;
static int _nonBlockCbCause;
static int _cdReadPattern;

static int _decIsSet = 0;

// Streaming
static int st_bufsize;
static int st_banksize;
static int st_numbanks;
static void *st_buf;
static int st_error;

static int st_curLBN;
static u8 st_banks[512];

static int st_nextLBN;
static int st_gp;
static int st_pp;
static int st_5B94;
static CdRMode_t st_rmode;
static int _stream_mode;

cdvdCommon _cdvdIntrCommon;
static u32 _configBlockAddr = 0;
static int _inited = 0;

// Code

int _start(int argc, char *argv) {
    int t;
    printf("CDVDDRVR v%X.%X\n", MAJOR_VER, MINOR_VER);

    _inited = 0;

    _cdvdIntrCommon.lastCmd = _cdvdIntrCommon.lastError = _cdvdIntrCommon.unk02 = _cdvdIntrCommon.unk03 = 0;
    _lastReadBuf = NULL;
    _cdvdIntrCommon.cmdActive = -1;
    _cdvdIntrCommon._lastThreadID = 0;

    _intrCbFunc0 = _intrCbFunc1 = NULL;
    _nonBlockCbFunc = NULL;
    _nonBlockCbCause = 0;

    if(RegisterLibraryEntries(&_exp_cdvddrvr) != 0)
        return(1);

    CdInit(0);

    printf("CDVDDRVR: initialized!\n");
    return(0);
}

int _exit(void) {
    CdInit(2);
    return(0);
}

int CdInit(int mode) {
    iop_sema_t sp;
    int t;

    if((mode == 0) || (mode == 1)) {
        if(_inited)
            return(-1);

        sp.attr = 1;
        sp.option = 0;
        sp.initial = 1;
        sp.max = 1;

        if((_nCmdSemaID = CreateSema(&sp)) < 0) {
            printf("CDVDDRVR: Error creating semaphores!\n");
            return(-2);
            }

        if((_sCmdSemaID = CreateSema(&sp)) < 0) {
            printf("CDVDDRVR: Error creating semaphores!\n");
            return(-3);
            }

        DisableIntr(IOP_IRQ_CDVD, &t);
        ReleaseIntrHandler(IOP_IRQ_CDVD);

        if((t = RegisterIntrHandler(IOP_IRQ_CDVD, 1, &_cdvdIntrHandler, &_cdvdIntrCommon)) != 0) {
            printf("CDVDDRVR: Error %d registering interrupt handler!\n", t);
            return(t);
            }

        *HWREG_BF8010F0 |= 0x8000; // What's this?

        *DMAC_CDVD_CTRL = 0; // Disable CDVD DMA
        *DMAC_CDVD_SIZE = 0;
        *DMAC_CDVD_BUF = 0;

        EnableIntr(IOP_IRQ_CDVD);

        _inited = 1;

        CdDiskReady(mode);
        }
    else if(mode == 2) { // Exit
        if(!_inited)
            return(-1);

        DisableIntr(IOP_IRQ_CDVD, &t);
        ReleaseIntrHandler(IOP_IRQ_CDVD);

        DeleteSema(_nCmdSemaID);
        DeleteSema(_sCmdSemaID);

        _inited = 0;
        }
    else {
        printf("CDVDDRVR: Invalid mode for CdInit: %d!\n", mode);
        return(-4);
        }

    return(0);
}

// Handler for CDVD interrupt
int _cdvdIntrHandler(void *_common) {
    cdvdCommon *common = (cdvdCommon *) (_common);
    u8 unkvar = 0;

    (*common).lastError = *CDVD_06;

    if(*CDVD_08 & 0x01) {
        if(*CDVD_05 & 1) { // Cmd Error bit?
            (*common).cmdActive = -1;
            }
        else {
            (*common).cmdActive = 1;
            }

        *CDVD_08 = 0x01;
        }
    else {
        (*common).unk03 += 2;
        (*common).cmdActive = 1;
        unkvar = 1;
        *CDVD_08 = 0x02;
        }

    if((*common).spin_ctrl == 1) {
        if(_intrCbFunc0)
            (*_intrCbFunc0)();
        }

    if((*common).spin_ctrl == 2) {
        if(_intrCbFunc1)
            (*_intrCbFunc1)();
        }

    if((_nonBlockCbFunc) && (_nonBlockCbCause != 0) && (unkvar == 0)) {
        (*_nonBlockCbFunc)(_nonBlockCbCause);
        }

    if((_nonBlockCbFunc) && (unkvar != 0)) {
        (*_nonBlockCbFunc)(CdCbInterrupt);
        }

    _nonBlockCbCause = 0x00;
    return(1);
}

int CdDiskReady(int mode) {
    while((mode != 0) && ((*CDVD_05 & 0xC0) != 0x40));

    if((*CDVD_05 & 0xC0) == 0x40)
        return(2);

    return(6);
}

int CdApplyNCmd(u8 cmd, u8 *params, int plen) {
    int i;
    u8 tmp;

    if(PollSema(_nCmdSemaID) == -419) {
        return(-1);
        }

    if((*CDVD_05 & 0xC0) != 0x40) {
        SignalSema(_nCmdSemaID);
        return(-1);
        }

    _cdvdIntrCommon.lastCmd = cmd;
    _cdvdIntrCommon.lastError = 0;
    _cdvdIntrCommon.cmdActive = 0;
    _cdvdIntrCommon._lastThreadID = GetThreadId();

    for(i = 0; i < plen; i++)
        *CDVD_05 = params[i];

    *CDVD_NB_CMD = cmd;
    tmp = *CDVD_NB_CMD;

    SignalSema(_nCmdSemaID);
    return(0);
}

int CdApplySCmd(u8 cmd, u8 *params, u32 plen, u8 *result, int rlen) {
    int r, i;
    u8 tmp[256];
    u8 dummy;

    if(PollSema(_sCmdSemaID) == -419)
        return(-1);

    while((*CDVD_17 & 0xC0) != 0x40) // Clear out all existing results
        tmp[0] = *CDVD_18;

    while(plen--) // Send our params
        *CDVD_17 = *(params++);

    *CDVD_16 = cmd; // Send the cmd
    tmp[0] = *CDVD_16; // read back dummy

    while((*CDVD_17 & 0x80) != 0); // Wait for command to complete

    r = 0; // Keep a tally of the results
    while(((*CDVD_17 & 0x40) == 0) && (r < 256)) // Get the results
        tmp[r++] = *CDVD_18;

    if(r >= 256)
        while((*CDVD_17 & 0x40) == 0) // Clear out any remaining results
            dummy = *CDVD_18;

    if(r >= rlen) // Don't wanna overflow our "result" buffer
        r = rlen;

    for(i = 0; i < r; i++) { // Copy the results to our buffer.
        result[i] = tmp[i];
        }

    SignalSema(_sCmdSemaID);
    return(0);
}

void setDMA(u16 arg0, u16 arg1, void *buf) {
    vu32 dummy;

    *DMAC_CDVD_CTRL = 0; // Disable
    *DMAC_CDVD_BUF = (u32) buf;
    *DMAC_CDVD_SIZE = (u32) (((arg1 & 0xFFFF) << 16) | (arg0 & 0xFFFF));
    *DMAC_CDVD_CTRL = 0x41000200;
    dummy = *DMAC_CDVD_CTRL;
}

// CDVDMAN Library

int CdCheckCmd(void) {
    return(_cdvdIntrCommon.cmdActive);
}

int CdSync(int mode) {
    int rv = 0;
    if(mode == 0) {
        while(CdCheckCmd() == 0);
        }
    else if(mode == 1) {
        rv = (CdCheckCmd() < 1);
        }
    else {
        while(CdCheckCmd() == 0)
            DelayThread(1000);
        }

    return(rv);
}

int CdGetDiscType(void) {
    return(*CDVD_DISK_TYPE);
}

int CdGetStatus(void) {
    return(*CDVD_TRAY_STAT);
}

int CdReadTOC(void *buf) {
    int i, j;
    u8 params[4];

    switch(*CDVD_DISK_TYPE) {
        case CdDiskDVDPS2:
        case CdDiskDVDV:
        case CdDiskDVDV2:
            *CDVD_06 = 0x84;
            i = 4;
            j = 129;
            break;
        case CdDiskCDPS1:
        case CdDiskCDDAPS1:
        case CdDiskCDPS2:
        case CdDiskCDDAPS2:
        case CdDiskCDDA:
            *CDVD_06 = 0x80;
            i = 32;
            j = 8;
            break;
        default:
            return(-1);
            break;
        }

    setDMA(i, j, buf);

    _nonBlockCbCause = CdCbReadTOC;
    params[0] = 0x00;

    while(CdApplyNCmd(CdNCmdReadTOC, params, 1) != 0)
        DelayThread(1000);

    CdSync(0);

    return(0);
}

int CdReadTOC2(void *buf, u8 arg1) {
    u8 params[4];

    *CDVD_06 = 0x8C;

    setDMA(0x0C, 43, buf);

    _nonBlockCbCause = 0;
    params[0] = arg1;
    while(CdApplyNCmd(CdNCmdReadTOC, params, 1) != 0)
        DelayThread(1000);

    return(0);
}

int CdDecSet(u8 rotlEn, u8 xorEn, u8 unkEn, u8 swapEn, u8 rotlNum) {
    u8 ctrl;

    _decIsSet = (rotlEn | xorEn | unkEn | swapEn);

// the high nibble of the "ctrl" byte contains the # of bits to "rotate left" by if enabled.
    ctrl = (rotlNum & 7) << 4;

// Enable XOR with magic number
    if(xorEn != 0)
        ctrl |= 0x01;

// Enable 8-bit "rotate left" rotBits number of bits
    if(rotlEn != 0)
        ctrl |= 0x02;

// unknown
    if(unkEn != 0)
        ctrl |= 0x04;

// Enable nibble swapping(swaps the lower and upper nibbles of each byte)
    if(swapEn != 0)
        ctrl |= 0x08;

    *CDVD_DEC_CTRL = ctrl;

    return(1);
}


// SCmd 0x02
int CdReadSubQ(void *buf, u8 *result) {
    u8 results[16];
    u8 temps[8];
    int i;

    *CDVD_09 = 0x00;

    while(CdApplySCmd(CdSCmdReadSubQ, NULL, 0, results, 0x0B) != 0)
        DelayThread(1000);

    memcpy(buf, &results[1], 10);

    for(i = 0; i < 10; i++) {
        temps[0] = *CDVD_0E;
        temps[1] = *CDVD_0D;
        temps[2] = *CDVD_0C;

        temps[3] = *CDVD_0E;
        temps[4] = *CDVD_0D;
        temps[5] = *CDVD_0C;

        if((temps[0] == temps[3]) && (temps[1] == temps[4]) && (temps[2] == temps[5]))
            break;
        }

    if(i < 10) {
        ((u8 *) (buf))[9] = temps[0];
        ((u8 *) (buf))[8] = temps[1];
        ((u8 *) (buf))[7] = temps[2];
        }

    if(result)
        *result = results[0];

    return(0);
}

// SCmd 0x03
int CdSubCommand(u8 *params, int numParams, u8 *results, int numResults) {
    u8 _results[64];

    while(CdApplySCmd(CdSCmdSubCmd, params, numParams, _results, numResults + 1) != 0)
        DelayThread(1000);

    memcpy(results, &_results[1], numResults);
    return(_results[0]);
}

// SCmd 0x04
// Function unknown, exists in rom0:CDVDMAN
int CdUnkSCmd04(u8 *buf, u8 *status) {
    u8 results[16];

    while(CdApplySCmd(CdSCmdUnk04, NULL, 0, results, 5) != 0)
        DelayThread(1000);

    if(results[0] == 0)
        memcpy(buf, &results[1], 4);

    return(results[0]);
}

// SCmd 0x05
int CdTrayStatus(u8 *status) {
    u8 _results[16];

    do {
        DelayThread(1000);
        while(CdApplySCmd(CdSCmdTrayStatus, NULL, 0, _results, 1) != 0)
            DelayThread(1000);

        } while(_results[0] != 0);

    *status = *CDVD_0B;

    return(0);
}

// SCmd 0x06
int CdTrayReq(u8 arg) {
    u8 result[4];
    u8 params[4];

    do {
        params[0] = arg;
        DelayThread(1000);
        } while(CdApplySCmd(CdSCmdTrayReq, params, 1, result, 1) != 0);

    return(result[0]);
}

// SCmd 0x08
int CdReadRTC_Raw(void *buf) {
    while(CdApplySCmd(CdSCmdReadRTC, NULL, 0, buf, 8) != 0)
        DelayThread(1000);

    return(0);
}

// SCmd 0x08
int CdReadRTC(CdRTCData *rtcData) {
    u8 result[16];

    CdReadRTC_Raw(result);

    if(result[0] == 0x00) {
        (*rtcData).second = M_bcdtoi(result[1]);
        (*rtcData).minute = M_bcdtoi(result[2]);
        (*rtcData).hour = M_bcdtoi(result[3]);
        (*rtcData).day = M_bcdtoi(result[5]);
        (*rtcData).month = M_bcdtoi(result[6]);
        (*rtcData).year = 2000 + M_bcdtoi(result[7]);
        }

    return(result[0]);
}

// SCmd 0x09
int CdWriteRTC(CdRTCData *rtcData) {
    u8 result[4];
    u8 params[8];

    do {
        params[0] = M_itobcd((*rtcData).second);
        params[1] = M_itobcd((*rtcData).minute);
        params[2] = M_itobcd((*rtcData).hour);
        params[3] = M_itobcd((*rtcData).dummy);
        params[4] = M_itobcd((*rtcData).day);
        params[5] = M_itobcd((*rtcData).month);
        params[6] = M_itobcd((*rtcData).year - 2000);

        DelayThread(1000);

        } while(CdApplySCmd(CdSCmdWriteRTC, params, 7, result, 1) != 0);

    return(result[0]);
}

// 0x0A
int CdReadNVM(u16 addr, u16 *data) {
    u8 params[4];
    u8 result[4];

    do {
        params[0] = (addr >> 8) & 0xFF;
        params[1] = (addr >> 0) & 0xFF;
        DelayThread(1000);
        } while(CdApplySCmd(CdSCmdReadNVM, params, 2, result, 3) != 0);

    if(result[0] == 0)
        *data = ((result[1] << 8) | result[2]);

    return(result[0]);
}

// 0x0B
int CdWriteNVM(u16 addr, u16 data) {
    u8 params[4];
    u8 result[4];

    do {
        params[0] = (addr >> 8) & 0xFF;
        params[1] = (addr >> 0) & 0xFF;
        params[2] = (data >> 8) & 0xFF;
        params[3] = (data >> 0) & 0xFF;

        DelayThread(1000);
        } while(CdApplySCmd(CdSCmdWriteNVM, params, 4, result, 1) != 0);

    return(result[0]);
}

// SCmd 0x0C
int CdSetHDMode(int mode) {
    u8 params[4], result[4];

    do {
        params[0] = mode;
        DelayThread(1000);
        } while(CdApplySCmd(CdSCmdSetHDMode, params, 1, result, 1) != 0);

    return(result[0]);
}

// SCmd 0x0D
int CdSysStandby(void) {
    u8 result[4];

    while(CdApplySCmd(CdSCmdSysStandby, NULL, 0, result, 1) != 0)
        DelayThread(1000);

    return(result[0]);
}

// SCmd 0x0E
int CdSysWakeup(void) {
    u8 result[4];

    while(CdApplySCmd(CdSCmdSysWakeup, NULL, 0, result, 1) != 0)
        DelayThread(1000);

    return(result[0]);
}

// SCmd 0x0F
int CdPowerOff(void) {
    u8 result[4];

    while(CdApplySCmd(CdSCmdPowerOff, NULL, 0, result, 1) != 0)
        DelayThread(1000);

    return(result[0]);
}

// SCmd 0x10
int CdGetSomeVal(u8 *buf) {
    u8 results[4];

    while(CdApplySCmd(CdSCmdGetSomeVal, NULL, 0, results, 2) != 0)
        DelayThread(1000);

    if(results[0] == 0)
        buf[0] = results[1];

    return(results[0]);
}

// SCmd 0x11
int CdSetSomeVal(u8 val) {
    u8 results[4];
    u8 params[4];

    do {
        params[0] = val;
        DelayThread(1000);
        } while(CdApplySCmd(CdSCmdSetSomeVal, params, 1, results, 1) != 0);

    return(results[0]);
}

// SCmd 0x12
int CdReadILinkID(u8 *idbuf) {
    u8 results[16];

    while(CdApplySCmd(CdSCmdReadILinkID, NULL, 0, results, 9) != 0)
        DelayThread(1000);

    if(results[0] == 0)
        memcpy(idbuf, &results[1], 8);

    return(results[0]);
}

// SCmd 0x13
int CdWriteILinkID(u8 *idbuf) {
    u8 results[4];

    while(CdApplySCmd(CdSCmdWriteILinkID, idbuf, 8, results, 1) != 0)
        DelayThread(1000);

    return(results[0]);
}

// SCmd 0x14
int CdCtrlADOut(u8 val) {
    u8 results[4];
    u8 params[4];

    do {
        params[0] = val;
        DelayThread(1000);
        } while(CdApplySCmd(CdSCmdCtrlADOut, params, 1, results, 1) != 0);

    return(results[0]);
}

// SCmd 0x15
int CdForbidDVDP(u8 *result) {
    u8 results[4];

    while(CdApplySCmd(CdSCmdForbidDVDP, NULL, 0, results, 1) != 0)
        DelayThread(1000);

    if(result)
        result[0] = results[0];

    return(results[0]);
}

// SCmd 0x16
int CdAutoAdjustCtrl(u8 val) {
    u8 results[4];
    u8 params[4];

    do {
        params[0] = val;
        DelayThread(1000);
        } while(CdApplySCmd(CdSCmdCtrlAutoAdjust, params, 1, results, 1) != 0);

    return(results[0]);
}

// 0x17
int CdReadModelNumber(char *model) {
    u8 params[4], result[32];
    int i;

    for(i = 0; i < 16; i++)
        result[i] = 0;

    do {
        params[0] = 0;
        DelayThread(1000);
        } while(CdApplySCmd(CdSCmdReadModelNumber, params, 1, result, 9) != 0);

    if(result[0] != 0)
        return(-1);

    memcpy(&model[0], &result[1], 8);

    do {
        params[0] = 8;
        DelayThread(1000);
        } while(CdApplySCmd(CdSCmdReadModelNumber, params, 1, result, 9) != 0);

    memcpy(&model[8], &result[1], 8);

    return(result[0]);
}

// 0x18
int CdWriteModelNumber(char *model) {
    u8 params[16], result[4];
    int i;

    do {
        params[0] = 0;

        for(i = 0; ((i < 8) && (model[i] != '\0')); i++)
            params[i + 1] = model[i + 0];

        while(i < 8)
            params[i + 1] = 0x20;

        DelayThread(1000);
        } while(CdApplySCmd(CdSCmdWriteModelNumber, params, 9, result, 1) != 0);

    if(result[0] != 0)
        return(result[0]);

    do {
        params[0] = 8;

        for(i = 0; ((i < 8) && (model[i] != '\0')); i++)
            params[i + 1] = model[i + 8];

        while(i < 8)
            params[i + 1] = 0x20;

        DelayThread(1000);
        } while(CdApplySCmd(CdSCmdWriteModelNumber, params, 9, result, 1) != 0);

    return(result[0]);
}

// SCmd 0x19
int CdForbidRead(u8 *result) {
    u8 results[4];

    while(CdApplySCmd(CdSCmdForbidRead, NULL, 0, results, 1) != 0)
        DelayThread(1000);

    if(result)
        *result = results[0];

    return(0);
}

// SCmd 0x1A
int CdBootCertify(BootKey *key) {
    u8 result[4];

    while(CdApplySCmd(CdSCmdBootCertify, (u8 *) (key), 4, result, 1) != 0)
        DelayThread(1000);

    return(result[0]);
}

// SCmd 0x40
int CdOpenConfig(u8 mode, u8 dev, u8 block) {
    u8 params[4], result[4];

    do {
        params[0] = mode;
        params[1] = dev;
        params[2] = block & 0xFF;

        DelayThread(1000);
        } while(CdApplySCmd(CdSCmdOpenConfig, params, 3, (u8 *) result, 1) != 0);

    if(result[0] == 0)
        _configBlockAddr = block;

    return(result[0]);
}

// SCmd 0x41
int CdReadConfigBlock(u8 *data) {
    u8 result[16];
    u8 sum;

    while(CdApplySCmd(CdSCmdReadConfigBlock, NULL, 0, result, 16) != 0)
        DelayThread(1000);

    if(result[15] != (sum = _addSumMemBlock(result))) {
        return(0x88);
        }

    memcpy(data, result, 15);

    return(0);
}

// SCmd 0x42
int CdWriteConfigBlock(u8 *data) {
    u8 params[16], result[4];

    do {
        memcpy(params, data, 15);
        params[15] = _addSumMemBlock(params);

        DelayThread(1000);
        } while(CdApplySCmd(CdSCmdWriteConfigBlock, params, 16, result, 1) != 0);

    return(result[0]);
}

// SCmd 0x43
int CdCloseConfig(void) {
    u8 results[4];

    while(CdApplySCmd(CdSCmdCloseConfig, NULL, 0, results, 1) != 0)
        DelayThread(1000);

    _configBlockAddr = 0x00;

    return(results[0]);
}

// SubCommand 0x00 - UNTESTED!!
int CdGetMechaconVer(u8 *buf, int *result) {
    u8 params[16];
    u8 results[16];

    params[0] = CdSubCmdGetMechaconVer;

    if(CdSubCommand(params, 1, results, 4) != 0)
        return(-1);

    if(result)
        *result = results[0];

    if(buf) {
        buf[0] = results[1];
        buf[1] = results[2];
        buf[2] = results[3];
        }

    return(0);
}

// SubCommand 0x44 - UNTESTED!!
int CdWriteConsoleID(u8 *id, int *result) {
    u8 params[16];
    u8 results[16];

    params[0] = CdSubCmdWriteConsoleID;
    memcpy(&params[1], id, 8);

    if(CdSubCommand(params, 9, results, 1) != 0)
        return(-1);

    if(result)
        *result = results[0];

    return(0);
}

// SubCommand 0x45 - UNTESTED!!
int CdReadConsoleID(u8 *id, int *result) {
    u8 params[16];
    u8 results[16];

    params[0] = CdSubCmdReadConsoleID;

    if(CdSubCommand(params, 1, results, 9) != 0)
        return(-1);

    if(result)
        *result = results[0];

    memcpy(id, &results[1], 8);

    return(0);
}

int CdNop(void) {
    _nonBlockCbCause = 0;
    return(CdApplyNCmd(CdNCmdNOP, NULL, 0) >= 0);
}

int _CdNCmdUnk01(void) {
    _nonBlockCbCause = 0;
    return(CdApplyNCmd(CdNCmdUnk01, NULL, 0) >= 0);
}

int CdStop(void) {
    _nonBlockCbCause = CdCbStop;
    return(CdApplyNCmd(CdNCmdStop, NULL, 0) >= 0);
}

int CdPause(void) {
    _nonBlockCbCause = CdCbPause;
    return(CdApplyNCmd(CdNCmdPause, NULL, 0) >= 0);
}

int CdPosToInt(CDPOS *pos) {
    return(0);
}

CDPOS *CdIntToPos(int integer, CDPOS *pos) {
    return(NULL);
}

int CdStandby(void) {
    _nonBlockCbCause = CdCbStandby;
    return(CdApplyNCmd(CdNCmdStandby, NULL, 0) >= 0);
}

int CdSeek(u32 lbn) {
    u8 params[4];

    params[0] = ((lbn >> 0) & 0xFF);
    params[1] = ((lbn >> 8) & 0xFF);
    params[2] = ((lbn >> 16) & 0xFF);
    params[3] = ((lbn >> 24) & 0xFF);
    _nonBlockCbCause = CdCbSeek;

    return(CdApplyNCmd(CdNCmdSeek, params, 4) >= 0);
}

void *CdSearchFile(void *fp, char *name) {
    if(_CdSearchFilePtr)
        return(_CdSearchFilePtr(fp, name));

    return(NULL);
}

#if 1
/* rom0:CDVDMAN implementation of CdRead */
int CdRead(u32 lbn, u32 nsects, void *buf, CdRMode_t *rmode) {
    u32 var_a0, var_a1, var_t1, var_t0;
    int isDVD = 0;
    u8 params[16];

    var_a1 = nsects;

    _lastReadBuf = buf;

    switch(*CDVD_DISK_TYPE) {
        case CdDiskDVDPS2:
            isDVD = 1;
            break;
        case CdDiskCDPS1:
        case CdDiskCDDAPS1:
        case CdDiskCDPS2:
        case CdDiskCDDAPS2:
            isDVD = 0;
            break;
        default:
            return(-1);
            break;
        }

    _cdReadPattern = (*rmode).pattern;

    switch((*rmode).pattern) {
        case CdSect2048:
            var_a0 = 0x20;
            var_a1 *= 16;
            var_t1 = 0x80;
            break;
        case CdSect2328:
            var_a0 = 0x06;
            var_a1 *= 61;
            var_t1 = 0x86;
            break;
        case CdSect2340:
            var_a0 = 0x0F;
            var_a1 *= 39;
            var_t1 = 0x8F;
            break;
        default:
            var_a0 = 0x20;
            var_a1 *= 16;
            var_t1 = 0x80;
            break;
        }

    switch((*rmode).speed) {
        case 0:
            if(isDVD)
                var_t0 = 0x03;
            else
                var_t0 = 0x05;
            break;
        case 3:
            var_t0 = 0x02;
            break;
        case 4:
            var_t0 = 0x03;
            break;
        case 5:
            if(isDVD)
                var_t0 = 0x03;
            else
                var_t0 = 0x04;
            break;
        case 10:
            var_t0 = 0x40;
            break;
        case 12:
            if(isDVD)
                var_t0 = 0x04;
            else
                var_t0 = 0x02;
            break;
        case 1:
        case 2:
        case 6:
        case 7:
        case 8:
        case 9:
        case 11:
        default:
            if(isDVD)
                var_t0 = 0x83;
            else
                var_t0 = 0x85;
            break;
        }

    *CDVD_06 = var_t1;

    ((u32 *) (params))[0] = lbn;
    ((u32 *) (params))[1] = nsects;
    ((u8 *) (params + 0x08))[0] = (*rmode).tries;
    ((u8 *) (params + 0x08))[1] = var_t0;
    ((u8 *) (params + 0x08))[2] = (*rmode).pattern;

    setDMA(var_a0, var_a1 & 0xFFFF, buf);

    _nonBlockCbCause = CdCbRead;
    while(CdApplyNCmd(CdNCmdRead, params, 11) != 0)
        DelayThread(1000);

    return(0);
}
#endif

// Untested
int CdReadCDDA(u32 lbn, u32 nsects, void *buf, CdRMode_t *rmode) {
    u32 var_a0, var_a1, var_t1, var_t0;
    u8 params[16];

    var_a1 = nsects;

    switch(*CDVD_DISK_TYPE) {
        case CdDiskCDPS1:
        case CdDiskCDDAPS1:
        case CdDiskCDPS2:
        case CdDiskCDDAPS2:
        case CdDiskCDDA:
            break;
        default:
            return(-1);
            break;
        }

    switch((*rmode).pattern)
    {
        case CddaSect2352:
            var_a0 = 0x0C;
            var_a1 *= 49;
            var_t1 = 0x8C;
            break;
        case CddaSect2368:
            var_a0 = 0x08;
            var_a1 *= 74;
            var_t1 = 0x88;
            break;
        case CddaSect2448:
            var_a0 = 0x0C;
            var_a1 *= 49;
            var_t1 = 0x8C;
            break;
        default:
            var_a0 = 0x0C;
            var_a1 *= 49;
            var_t1 = 0x8C;
            break;
    }

    switch((*rmode).speed) {
        case 0:
            var_t0 = 0x05;
            break;
        case 2:
            var_t0 = 0x01;
            break;
        case 3:
            var_t0 = 0x02;
            break;
        case 4:
            var_t0 = 0x03;
            break;
        case 5:
            var_t0 = 0x04;
            break;
        case 10:
            var_t0 = 0x45;
            break;
        default:
            var_t0 = 0x85;
            break;
        }

    *CDVD_06 = var_t1;

    ((u32 *) (params))[0] = lbn;
    ((u32 *) (params))[1] = nsects;
    ((u8 *) (params + 0x08))[0] = (*rmode).tries;
    ((u8 *) (params + 0x08))[1] = var_t0;
    ((u8 *) (params + 0x08))[2] = (*rmode).pattern;

    setDMA(var_a0, var_a1 & 0xFFFF, buf);

    _nonBlockCbCause = CdCbReadCDDA;
    while(CdApplyNCmd(CdNCmdReadCDDA, params, 11) != 0)
        DelayThread(1000);
    return(0);
}

int CdReadDVDV(u32 lbn, u32 nsects, void *buf, CdRMode_t *rmode) {
    u32 var_a1,  var_t0;
    u8 params[16];

    var_a1 = nsects;

    switch(*CDVD_DISK_TYPE) {
        case CdDiskDVDV:
        case CdDiskDVDV2:
            break;
        default:
            return(-1);
            break;
        }

    switch((*rmode).speed) {
        case 0:
        case 4:
            var_t0 = 0x03;
            break;
        case 2:
            var_t0 = 0x01;
            break;
        case 3:
        case 12:
            var_t0 = 0x04;
            break;
        case 10:
            var_t0 = 0x40;
            break;
        case 11:
            var_t0 = 0x02;
            break;
        default:
            var_t0 = 0x83;
            break;
        }

    *CDVD_06 = 0x8C;

    ((u32 *) (params))[0] = lbn;
    ((u32 *) (params))[1] = nsects;
    ((u8 *) (params + 0x08))[0] = (*rmode).tries;
    ((u8 *) (params + 0x08))[1] = var_t0;
    ((u8 *) (params + 0x08))[2] = 0x00;

    setDMA(0x0C, (nsects * 43) & 0xFFFF, buf);

    _nonBlockCbCause = CdCbReadDVDV;
    while(CdApplyNCmd(CdNCmdReadDVDV, params, 11) != 0)
        DelayThread(1000);

    return(0);
}

int CdReadKey(u8 arg0, u16 arg1, u32 lbn, u8 *buf) {
    int i, j;
    u8 params[8];

    if(arg0 != 0)
        lbn = 0;

    params[0] = arg0;
    params[1] = (arg1 != 0);
    params[2] = ((arg1 >> 8) != 0);

    params[3] = ((lbn >>  0) & 0xFF);
    params[4] = ((lbn >>  8) & 0xFF);
    params[5] = ((lbn >> 16) & 0xFF);
    params[6] = ((lbn >> 24) & 0xFF);

    _nonBlockCbCause = 0;
    while(CdApplyNCmd(CdNCmdReadKey, params, 7) != 0)
        DelayThread(1000);
    CdSync(0);

    for(i = 0; i < 3; i++) {
        if(*CDVD_KEY_FLAGS & (1 << i)) {
            for(j = 0; j < 5; j++)
                buf[(i * 5) + j] = ((vu8 *) (0xBF402020 + (i * 8)))[j] ^ *CDVD_KEY_XOR;
            }
        else {
            for(j = 0; j < 5; j++)
                buf[(i * 5) + j] = 0;
            }
        }

    buf[15] = *CDVD_KEY_FLAGS & 0x07;
    
    return(0);
}

int CdGetLastError(void) {
    return(_cdvdIntrCommon.lastError);
}

CdNonBlockCbFunc *CdSetCallback(CdNonBlockCbFunc *cb) {
    CdNonBlockCbFunc *old = NULL;

    if(CdSync(1) == 0) {
        old = _nonBlockCbFunc;
        _nonBlockCbFunc = cb;
        }

    return(old);
}

int CdBreak(void) {
    _cdvdIntrCommon.lastCmd = _cdvdIntrCommon.lastError = 0;
    _cdvdIntrCommon._lastThreadID = GetThreadId();

    if(_cdvdIntrCommon.cmdActive == 0) {
        _nonBlockCbCause = CdCbBreak;
        *CDVD_07 = 0x01;
        }

    return(1);
}


// 8-bit addition sum of the first 15 bytes of the block data
u8 _addSumMemBlock(u8 *block) {
    int i;
    u32 sum = 0;

    for(i = 0; i < 15; i++)
        sum += block[i];

    return(sum & 0xFF);
}

// Wrapper for reading as many blocks as are available for the currently opened config device.
int CdReadConfig(u8 *data) {
    int bcount;

    for(bcount = 0; bcount < _configBlockAddr; bcount++) {
        if(CdReadConfigBlock(&data[bcount * 15]) != 0)
            break;
        }

    return(bcount);
}

// Wrapper for writing as many blocks as are available for the currently opened config device.
int CdWriteConfig(u8 *data) {
    int bcount;

    for(bcount = 0; bcount < _configBlockAddr; bcount++) {
        if(CdWriteConfigBlock(&data[bcount * 15]) != 0)
            break;
        }

    return(bcount);
}

int CdGetReadPos(void) {
    if(_nonBlockCbCause != 1) {
        return(0);
        }

    return(*DMAC_CDVD_BUF - (u32) _lastReadBuf);
}

// Export 47 of CDVDMAN library, not functionally equiv.
void *CdGetFsvRbuf(void) {
    return(NULL);
}

int CdStSetCB0(CdIntrCbFunc *cb) {
    _intrCbFunc0 = cb;
    return(0);
}

int CdStSetCB1(CdIntrCbFunc *cb) {
    _intrCbFunc1 = cb;
    return(0);
}

int CdSpinCtrl(int mode, int *error) {
    if(error)
        *error = _cdvdIntrCommon.lastError;

    if(mode != -1)
        _cdvdIntrCommon.spin_ctrl = mode;

    return(_cdvdIntrCommon.spin_ctrl);
}

// Called when a streaming Read interrupt occurs
void _CdStIntrReadCB(void) {
    int temp_gp;

// get last streaming error
    CdSpinCtrl(-1, &st_error);

// error occured, stop the spindle and return
    if(st_error == 0x32) { // what is err 0x32?
        CdSpinCtrl(0, &st_error);
        return;
        }


    if(_stream_mode == 0) {
        st_banks[st_gp] = 0x01;

        temp_gp = st_gp;

        if(++st_gp >= st_numbanks)
            st_gp = 0;

        if((st_banks[st_gp] != 0) || (st_pp == st_gp)) {
            st_gp = temp_gp;
            st_banks[st_gp] = 0x00;

            CdRead(st_curLBN, 16, st_buf + (st_gp * st_banksize), &st_rmode);
            return;
            }
        }

    _stream_mode = 0;
    st_curLBN = st_nextLBN;
    CdRead(st_nextLBN, 16, st_buf + (st_gp * st_banksize), &st_rmode);
    st_nextLBN += 16;
}

// Streaming

// Unfinished!!
int CdStCall(int arg0, int nsects, void *arg2, int mode, CdRMode_t *rmode) {
    int i, j;
    int rv = 0;

    DBG_printf(2, "CDVDDRVR: CdStCall %d\n", mode);

    i = 0;

    if(mode == CdStCmdResume) {
        CdSpinCtrl(1, &st_error);
        return(1);
        }

    if(mode == CdStCmdPause) {
        CdSpinCtrl(0, &st_error);
        CdSync(0);
        return(1);
        }

    if(mode == CdStCmdStat) {
        return(1);
        }

    if(mode == CdStCmdInit) {
        CdStSetCB0((CdIntrCbFunc *) &_CdStIntrReadCB);
        st_bufsize = arg0;
        st_buf = arg2;
        st_numbanks = ((nsects * 2048) / 2048);
        st_banksize = ((st_bufsize / st_numbanks) * 2048);
        return(1);
        }

    if(mode == CdStCmdStop) {

        CdSpinCtrl(0, &st_error);

        if(i < st_numbanks) {
            for(j = 0; j < st_numbanks; j++)
                st_banks[j] = 0x00;
            }

        st_5B94 = 0;
        st_pp = 0;
        st_gp = 0;

        CdSync(0);
        return(1);
        }

    if(mode == CdStCmdStart) {
        st_rmode.pattern = (*rmode).pattern;
        st_rmode.tries = (*rmode).tries;
        st_rmode.speed = (*rmode).speed;
        }

    if(mode == CdStCmdSeek) {
        i = 1;
        CdSpinCtrl(0, &st_error);
        nsects = 0;
        mode = i;
        st_5B94 = 0;
        st_pp = 0;
        st_gp = 0;
        CdSync(0);
        }

    if(mode == CdStCmdStart) {
        for(j = 0; j < st_numbanks; j++)
            st_banks[j] = 0x00;

        st_nextLBN = arg0;
        st_pp = st_gp = 0;
        CdSync(0);

// calculate the total number of banks needed to read in the requested number of sectors
        j = (nsects / st_numbanks) + ((nsects % st_numbanks) != 0);

//
        for(st_gp = 0; st_gp < j; st_gp++) {
            CdRead(st_nextLBN, 16, st_buf, &st_rmode);
            CdSync(0);

            st_nextLBN += 16;
            st_banks[st_gp] = 0x01;
            }

        _stream_mode = 1;
        CdSpinCtrl(1, &st_error);
        CdNop();
        }

    if(nsects != 0) {

        }

    return(0);
}

int CdStStart(int lbn, CdRMode_t *rmode) {
    DBG_printf(2, "CDVDDRVR: CdStStart\n");
    return(CdStCall(lbn, 0, NULL, CdStCmdStart, rmode));
}

int CdStRead2(int nsects, void *buf, int mode, int *result) {
    int sects_read;
    int rv, err;

    sects_read = 0;
    do {
        rv = CdStCall(0, nsects - sects_read, (void *) (buf + (sects_read * 2048)), CdStCmdRead, &_St_rmode);

        if((err = (rv >> 16)) != 0)
            break;

        sects_read += (rv & 0xFFFF);
        } while((mode == 1) && (sects_read < nsects));

    if(result)
        *result = err;

    return(sects_read);
}

int CdStRead(int nsects, void *buf, int mode, int *result) {
    int rv;
    int cur_size = 0;

    DBG_printf(2, "CDVDDRVR: CdStRead - nsects %d, buf %p, mode %d, result %p\n", nsects, buf, mode, result);

    if(mode) { // Keep reading until all requested data recieved.
        do {
            rv = CdStCall(0, nsects - cur_size, (void *) (buf + (cur_size * 2048)), CdStCmdRead, &_St_rmode);
            cur_size += (rv & 0xFFFF);
            rv >>= 16;
            } while((cur_size != nsects) && (rv == 0));
        }
    else { // grab only what is currently available.
        rv = CdStCall(0, nsects, buf, CdStCmdRead, &_St_rmode);

        if(result)
            *result = (rv >> 16);

        rv &= 0xFFFF; // return lower 16-bits(# of sectors read)
        }

    return(rv);
}

int CdStStop(void) {
    DBG_printf(2, "CDVDDRVR: CdStStop\n");
    return(CdStCall(0, 0, NULL, CdStCmdStop, NULL));
}

int CdStSeek(int lbn) {
    DBG_printf(2, "CDVDDRVR: CdStSeek %08X\n", lbn);
    return(CdStCall(lbn, 0, NULL, CdStCmdSeek, NULL));
}

int CdStInit(int buf_size, int nbanks, void *buf) {
    DBG_printf(2, "CDVDDRVR: CdStInit\n");
    return(CdStCall(buf_size, nbanks, buf, CdStCmdInit, &_St_rmode));
}

int CdStStat(void) {
    DBG_printf(2, "CDVDDRVR: CdStStat\n");
    return(CdStCall(0, 0, NULL, CdStCmdStat, NULL));
}

int CdStPause(void) {
    DBG_printf(2, "CDVDDRVR: CdStPause\n");
    return(CdStCall(0, 0, NULL, CdStCmdPause, &_St_rmode));
}

int CdStResume(void) {
    DBG_printf(2, "CDVDDRVR: CdStResume\n");
    return(CdStCall(0, 0, NULL, CdStCmdResume, &_St_rmode));
}
