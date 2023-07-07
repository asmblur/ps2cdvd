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

// MagicGate Mechacon stuff

enum {
    CdSCmdSetEncData = 0x8D, // 0x8D
    CdSCmdGetDecData, // 0x8E
    CdSCmdPollOpComplete, // 0x8F
    CdSCmdSetEncHeaderParams, // 0x90
    CdSCmdGetDecHeaderLen, // 0x91
    CdSCmdSetEncSectLen, // 0x92
    CdSCmdGetDecSectLen, // 0x93
};

#define ICV_FLAG_ENCRYPT    2
#define ICV_FLAG_DECRYPT    3

typedef struct tag_BlockInformation{
    unsigned int    blockLength,
            encryptionFlag,
            icvFlag,
            icvNth;
} BlockInformation;

typedef struct tag_BlockInformationTable{
    unsigned int     offset;
    unsigned char    numBlocks, pad[3];
    BlockInformation bi[63];
    unsigned int    pad_[2];
} BlockInformationTable;    //sizeof()==1024

// Send a block of data to the mechacon for decryption
int MG_setEncData(u8 *buf, int size) {
    u8 results[4];

    if(size > 16)
        return(-1);

    while(CdApplySCmd(CdSCmdSetEncData, buf, size, results, 1) != 0)
        DelayThread(1000);

    return(results[0]);
}

// Read decrypted data back from the mechacon.
int MG_setDecData(u8 *buf, int size) {
    if(size > 16)
        return(-1);

    while(CdApplySCmd(CdSCmdGetDecData, NULL, 0, buf, size) != 0)
        DelayThread(1000);

    return(0);
}

// Wait for last mechacon command to complete.
int MG_waitComplete(void) {
    u8 results[4];

    do {
        DelayThread(1000);
        while(CdApplySCmd(CdSCmdPollOpComplete, NULL, 0, results, 1) != 0) {
            DelayThread(1000);
            }
        } while(results[0] == 1);

    return(0);
}

// Start write of header
int MG_writeHeader_start(u8 arg0, u8 arg1, u8 arg2, u16 headerLen) {
    u8 results[16], params[5];

    params[0] = arg0;
    params[1] = (headerLen & 0xFF);
    params[2] = ((headerLen >> 8) & 0xFF);
    params[3] = arg1;
    params[4] = arg2;

    while(CdApplySCmd(CdSCmdSetEncHeaderParams, params, 5, results, 1) != 0)
        DelayThread(1000);

    return(results[0]);
}

// Start read back of header.
int MG_getDecHeaderLen(void *buf) {
    u8 results[4];

    while(CdApplySCmd(CdSCmdGetDecHeaderLen, NULL, 0, results, 3) != 0)
        DelayThread(1000);

    if(results[0] != 0)
        return(-1);

    *(u16 *) buf = (results[1] + (results[2] << 8));

    return(0);
}

// Start write of section
int MG_setEncSectionLen(u16 secLen) {
    u8 results[4], params[2];

    params[0] = (secLen & 0xFF);
    params[1] = ((secLen >> 8) & 0xFF);

    while(CdApplySCmd(CdSCmdSetEncSectLen, params, 2, results, 1) != 0)
        DelayThread(1000);

    return(results[0]);
}

// Start read back of section
int MG_getDecSectionLen(u16 secLen) {
    u8 results[4], params[2];

    params[0] = (secLen & 0xFF);
    params[1] = ((secLen >> 8) & 0xFF);

    while(CdApplySCmd(CdSCmdGetDecSectLen, params, 2, results, 1) != 0)
        DelayThread(1000);

    return(results[0]);
}

int MG_setHeader(u32 arg0, u32 arg1, u32 arg2, void *buf) {
    void *ePtr;
    int headerLen, rv, bytesLeft;

    headerLen = MG_getHeaderLength(buf);

    if(MG_writeHeader_start(arg0 & 0xFF, arg1, arg2, headerLen) != 0) {
        printf("LIBMG: writeHeader_start failed!\n");
        return(-1);
        }

    if(headerLen > 0) {
        ePtr = buf;

        bytesLeft = headerLen;

        while(bytesLeft > 0) {
            if(bytesLeft >= 16) {
                rv = MG_setEncData(ePtr, 16);
                ePtr += 16;
                bytesLeft -= 16;
                }
            else {
                rv = MG_setEncData(ePtr, bytesLeft);
                ePtr += bytesLeft;
                bytesLeft = 0;
                }

            if(rv != 0) {
                printf("LIBMG: MG_setEncData failed!\n");
                return(-1);
                }
            }
        }

    if(MG_waitComplete() != 0) {
        printf("LIBMG: waitComplete failed!\n");
        return(-1);
        }

    return(0);
}


int MG_readHeader(void *buf) {
    void *dPtr;
    int headerLen = 0, rv, bytesLeft;

    if(MG_getDecHeaderLen(&headerLen) != 0)
        return(-1);

    if(headerLen != 0) {
        dPtr = buf;

        bytesLeft = headerLen;

        while(bytesLeft > 0) {
            if(bytesLeft >= 16) {
                rv = MG_setDecData(dPtr, 16);
                dPtr += 16;
                bytesLeft -= 16;
                }
            else {
                rv = MG_setDecData(dPtr, bytesLeft);
                dPtr += bytesLeft;
                bytesLeft = 0;
                }

            if(rv != 0) {
                printf("MG_readHeader: readData failed!\n");
                return(-1);
                }
            }
        }

    return(headerLen);
}

int MG_diskDecryptHeader(void *encBuf, struct MG_BIT_struct *headerBuf, int *result) {
    int rv;

    if(MG_setHeader(0x00, 0x00, 0x00, encBuf) != 0) {
        printf("LIBMG: setHeader failed\n");
        return(-1);
        }

    if((rv = MG_readHeader(headerBuf)) <= 0) {
        printf("LIBMG: Cannot read back header!\n");
        return(-1);
        }

    if(result != NULL)
        *result = rv;

    return(0);
}

int calcKBitOffset(void *buf) {
    int off = 0x20, i;

    for(i = 0; i < *(u16 *) (buf + 0x1A); i++)
        off += 0x10;

    if((*(u32 *) (buf + 0x18) & 1) != 0)
        off += (1 + *(u8 *) (buf + off));

    if((*(u32 *) (buf + 0x18) & 0xF000) == 0)
        off += 8;

    return(off + 0x20);
}

u16 MG_getHeaderLength(void *encHeader) {
    return(*(u16 *) (encHeader + 0x14));
}

/*
########################################################################
########################################################################
############                                                ############
############             ELF section decryption             ############
############                                                ############
########################################################################
########################################################################
*/

int MG_diskSetSection(void *buf, int size) {
    void *ePtr;
    int rv, bytesLeft;

    if((rv = MG_setEncSectionLen(size & 0xFFFF)) != 0) {
        printf("LIBMG: writeSect_start failed: 0x%02X!\n", rv);
        return(-1);
        }

    if(size > 0) {
        ePtr = buf;

        bytesLeft = size;

        while(bytesLeft > 0) {
            if(bytesLeft >= 16) {
                rv = MG_setEncData(ePtr, 16);
                ePtr += 16;
                bytesLeft -= 16;
                }
            else {
                rv = MG_setEncData(ePtr, bytesLeft);
                ePtr += bytesLeft;
                bytesLeft = 0;
                }

            if(rv != 0) {
                printf("secr_set_sec: fail write_data\n");
                return(-1);
                }
            }
        }

    if(MG_waitComplete() != 0)
        return(-1);

    return(0);
}

int MG_diskReadSection(void *buf, int size) {
    void *dPtr;
    int bytesLeft, rv;

    if(MG_getDecSectionLen(size) != 0)
        return(-1);

    if(size != 0) {
        dPtr = buf;

        bytesLeft = size;

        while(bytesLeft > 0) {
            if(bytesLeft >= 16) {
                rv = MG_setDecData(dPtr, 16);
                dPtr += 16;
                bytesLeft -= 16;
                }
            else {
                rv = MG_setDecData(dPtr, bytesLeft);
                dPtr += bytesLeft;
                bytesLeft = 0;
                }

            if(rv != 0) {
                printf("secr_read_sec: fail read_data\n");
                return(-1);
                }
            }
        }

    return(size);
}

int MG_diskDecryptSection(void *encBuf, void *decBuf, int size) {
    if(MG_diskSetSection(encBuf, size) != 0)
        return(-1);

    if(MG_diskReadSection(decBuf, size) <= 0)
        return(-1);

    return(0);
}

/*
########################################################################
########################################################################
*/

void *MG_diskBootFile(void *buf) {
    u32 kBIToff;
    int i, secOff;
    struct MG_BIT_struct *pBit;
    u8 *bBuf;

    bBuf = (u8 *) buf;

    kBIToff = calcKBitOffset(bBuf);
    DBG_printf(1, "KBIT at start + 0x%08X\n", kBIToff);

    pBit = (struct MG_BIT_struct *) (bBuf + kBIToff);

    if(MG_diskDecryptHeader(bBuf, pBit, NULL) != 0) {
        printf("LIBMG: Failed to decrypt header!\n");
        return(NULL);
        }

    secOff = (*pBit).secStart;

    for(i = 0; i < (*pBit).numSecs; i++) {
        if(((*pBit).secs[i].flags & 0x03) != 0) {
            if(MG_diskDecryptSection((void *) &bBuf[secOff], (void *) &bBuf[secOff], (*pBit).secs[i].size) != 0) {
                printf("LIBMG: diskBootFile failed\n");
                return(NULL);
                }
            }

        secOff += (*pBit).secs[i].size;
        }

    return((void *) (bBuf + MG_getHeaderLength(bBuf)));
}
