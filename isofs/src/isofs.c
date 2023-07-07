/*

ISO9660 file-system driver for CDVDDRVR

*/

#include "types.h"
#include "defs.h"
#include "irx.h"
#include "sys/stat.h"

#include "stdio.h"
#include "sysclib.h"
#include "thbase.h"
#include "thsemap.h"
#include "ioman.h"

#include "io_common.h"

#include "isofs.h"
#include "fsdriver.h"
#include "common.h"

#include "../../cdvddrvr/include/cdvddrvr.h"

// should maybe change to a global variable afterward
#define use_compat_mode 0

IRX_ID("ISOFS", MAJOR_VER, MINOR_VER);

extern iop_device_t fsdriver;

isofs_path_entry isofs_paths[ISOFS_MAX_DIRS];
isofs_file_entry isofs_files[ISOFS_MAX_FILES];

volatile int _pvdLoaded = 0;
volatile enum {
    MODE_NOT_PROBED = 0,
    MODE_CD_1 = 1,
    MODE_CD_2 = 2,
    MODE_DVD = 3
} _CDProbedMode = 0;

int _currentCachedPathID = 0;

extern int _lastFioOpenMode;

// Temporary buffer used for various things.
u8 _isofsTempBuf[2340];

// Temporary buffer used for cd sector conversion (may conflict with the previous one)
u8 _isofsModeConvBuf[2340];

int _cdvdWaitReady(int timeout) {
    int i;

    if(timeout == -1)
    {
        while((*CDVD_05 & 0xC0) != 0x40)
            DelayThread(1000);

        return(1);
    }

    for(i = 0; i < timeout; i++)
    {
        if((*CDVD_05 & 0xC0) == 0x40) { return(1); }
        DelayThread(1000);
    }

    return(0);
}

int _cdReadSectors(int nsects, int lbn, void *buf, CdRMode_t *rmode)
{
    int err;

    DBG_printf(3, "ISOFS-_cdReadSectors: start! %d %d\n", nsects, lbn);

    if(!_cdvdWaitReady(100))
    {
        DBG_printf(1, "ISOFS-_cdReadSectors: timeout waiting for drive ready!\n");
        return(0);
    }

    if ((_CDProbedMode == MODE_CD_1) && (rmode->pattern == CdSect2048) && (use_compat_mode))
    {
    	// We have to split the request here.
    	int i;

    	rmode->pattern = CdSect2340;

    	for (i = 0; i < nsects; i++)
    	{
    	    if (CdRead(lbn + i, 1, _isofsModeConvBuf, rmode) != 0)
    	    {
        		// Aaiiee, something bad happened...
        		return (0);
    	    }

                while(CdCheckCmd() == 0)
        	    {
                    DelayThread(1000);
                }

                if((err = CdGetLastError()) != 0)
        	    {
            		// Aaiiee, something bad happened... (bis)
            		return (0);
        	    }

    	    // allright, we have our raw sector. Let's fill the buffer with it now.
    	    memcpy(((u8 *)buf) + 2048 * i, &_isofsModeConvBuf[4], 2048);
    	}

    	rmode->pattern = CdSect2048;
    }
    else
    {
        if(CdRead(lbn, nsects, buf, rmode) != 0)
        {
            return(0);
        }

        while(CdCheckCmd() == 0)
        {
            DelayThread(1000);
        }

        if((err = CdGetLastError()) != 0)
        {
            DBG_printf(1, "ISOFS: Error %d reading %d sectors from %d!\n", err, nsects, lbn);
            return(0);
        }
    }

    return(nsects);
}

int _cdProbeAndCachePathTable(void) {
    int i;
    u32 lptLBN;
    ISOPathRecord *pr;
    int isDVD = 1;
    CdRMode_t rmode = { 16, 2, CdSect2048, 0 };
    u8 * pvd_sector;

    if(_lastFioOpenMode != O_WRONLY)
    {
        rmode.speed = 1;
    }

    ISO_reset();

    switch(*CDVD_DISK_TYPE)
    {
        case CdDiskCDPS1:
        case CdDiskCDDAPS1:
        case CdDiskCDPS2:
        case CdDiskCDDAPS2:
        case CdDiskCDDA:
            isDVD = 0;
            break;
        default:
            break;
    }

    if (!isDVD)
    {
        rmode.pattern = CdSect2340;
    }

    // Read in Primary Volume Descriptor
    if(_cdReadSectors(1, 16, _isofsTempBuf, &rmode) != 1)
    {
        DBG_printf(1, "ISOFS: Error reading in PVD!\n");
        return(0);
    }

    if (isDVD) {
        _CDProbedMode = MODE_DVD;
        pvd_sector = _isofsTempBuf;
    }
    else
    {
        switch (_isofsTempBuf[3]) {
        case 0:
            DBG_printf(1, "ISOFS: Invalid disc format! PVD is mode 0!\n");
            return(0);
        case 1:
            _CDProbedMode = MODE_CD_1;
            pvd_sector = _isofsTempBuf + 4;
            break;
        case 2:
            _CDProbedMode = MODE_CD_2;
            pvd_sector = _isofsTempBuf + 12;
            break;
        default:
            DBG_printf(1, "ISOFS: Invalid disc format! PVD is mode %i!\n", _isofsTempBuf[3]);
            return(0);
        }
        rmode.pattern = CdSect2048;
    }

    if(strncmp((char *) (&pvd_sector[1]), "CD001", 5) != 0)
    {
        DBG_printf(1, "ISOFS: Invalid disc format!\n");
        return(0);
    }

    _pvdLoaded = 1;

    if(isDVD)
    { // For DVD medias, it's fixed at LBN 257
        lptLBN = 257;
    }
    else
    { // For CD medias, get the "L" Path Table LBN from the PVD
        memcpy(&lptLBN, &pvd_sector[0x8C], 4);
    }

    if(_cdReadSectors(1, lptLBN, _isofsTempBuf, &rmode) != 1)
    {
        DBG_printf(1, "ISOFS: Error reading in L Path Table!\n");
        return(0);
    }

    pr = (ISOPathRecord *) _isofsTempBuf;
    for(i = 0; i < ISOFS_MAX_DIRS; i++)
    { // walk the path table, caching the entries
        if((*pr).len_di == 0)
        {
            break;
        }

        memcpy(&(isofs_paths[i].lbn), &((*pr).lbn), 4);

        memcpy(&(isofs_paths[i].name), &((*pr).path_id), (*pr).len_di);
        isofs_paths[i].name[(*pr).len_di] = '\0';

        isofs_paths[i].parent = (u32) (*pr).parent[0];
        isofs_paths[i].id = i + 1;

        if((*pr).len_di & 1)
        {
            ((u32) (pr)) += 9 + (*pr).len_di;
        }
        else
        {
            ((u32) (pr)) += 8 + (*pr).len_di;
        }

        if(((u32) (pr)) >= ((u32) _isofsTempBuf + 2048))
        {
            break;
        }
    }

    if(i < ISOFS_MAX_DIRS)
    {
        isofs_paths[i].parent = 0;
    }

    DBG_printf(2, "ISOFS: %d directories found!\n", i);

    return(1);
}

int _cdCachePath(int path_id)
{
    ISODirRecord *dr;
    int fileCount = 0;
    CdRMode_t rmode = { 16, 1, 0, 0 };

    if(_lastFioOpenMode == O_WRONLY)
    {
        rmode.speed = 2;
    }

    if((path_id > ISOFS_MAX_DIRS) || (path_id <= 0))
    {
        printf("_cdCachePath: bad ID %d!\n", path_id);
        return(0);
    }

    if(path_id != _currentCachedPathID)
    {
        if(_cdReadSectors(1, isofs_paths[path_id - 1].lbn, _isofsTempBuf, &rmode) != 1)
        {
            DBG_printf(1, "_cdCachePath: dir %d not found!\n", path_id);
            return(0);
        }

        for(dr = (ISODirRecord *) _isofsTempBuf, fileCount = 0; (((u32) dr) < (u32) (_isofsTempBuf + 2048)) && (fileCount < ISOFS_MAX_FILES);)
        {
            if((*dr).len_dr == 0)
            {
                break;
            }

            memcpy(&(isofs_files[fileCount].lbn), (*dr).lbn_l, sizeof(u32));
            memcpy(&(isofs_files[fileCount].size), (*dr).data_len_l, sizeof(u32));

            isofs_files[fileCount].date.year = (*dr).recording_date.year + 1900;
            isofs_files[fileCount].date.month = (*dr).recording_date.month;
            isofs_files[fileCount].date.day = (*dr).recording_date.day;

            isofs_files[fileCount].date.hour = (*dr).recording_date.hour;
            isofs_files[fileCount].date.min = (*dr).recording_date.min;
            isofs_files[fileCount].date.sec = (*dr).recording_date.sec;

            isofs_files[fileCount].flags = (u32) (*dr).file_flags;

            if(fileCount == 0)
            {
                strcpy(isofs_files[fileCount].name, ".");
            }
            else if(fileCount == 1)
            {
                strcpy(isofs_files[fileCount].name, "..");
            }
            else
            {
                memcpy(isofs_files[fileCount].name, (*dr).field_id, (*dr).len_fi);

                if((*dr).len_fi > 2)
                {
                    if((isofs_files[fileCount].name[(*dr).len_fi - 2] == ';')
                        && (isofs_files[fileCount].name[(*dr).len_fi - 1] == '1'))
                    { // strip off the ';1'
                        isofs_files[fileCount].name[(*dr).len_fi - 2] = '\0';
                    }
                }

                isofs_files[fileCount].name[(*dr).len_fi] = '\0';

//                printf("file name2: %s\n", isofs_files[fileCount].name);
            }

            fileCount++;
            dr = (ISODirRecord *) ((u32) dr + (*dr).len_dr);
        }

        if(fileCount < ISOFS_MAX_FILES)
        {
            isofs_files[fileCount].name[0] = '\0';
        }

        _currentCachedPathID = path_id;

        DBG_printf(2, "_cdCachePath: %d files found!\n", fileCount);
    }

    return(1);
}

int _isNewMedia(void) {
    int isnew = 0;
    static int _lastTrayStatus = 1;

    if((*CDVD_0B & 1) || (_lastTrayStatus))
    {
        isnew = 1;

        while(CdApplySCmd(0x05, NULL, 0, NULL, 0) != 0)
        {
            DelayThread(1000);
        }

        _lastTrayStatus = *CDVD_0B & 1;
    }

    return(isnew);
}

isofs_file_entry *ISO_SearchFile(isofs_file_entry *fp, const char *name)
{
    char tempName[16];
    int level;
    int i, id;

    DBG_printf(3, "ISO_SearchFile(\"%s\");\n", name);

    if(_isNewMedia())
    {
        DBG_printf(3, "Probing disc mode and cacheing Path Table...\n");
        if(_cdProbeAndCachePathTable() == 0)
        {
            return(NULL);
        }
    }

    if(((name[0] != '\\') && (name[0] != '/')) && (name[0] != '\0'))
    {
        return(NULL);
    }

    id = 0x01;
    for(level = 0; level < ISOFS_MAX_DIR_LEVEL; level++)
    { // walk the path to the file
        DBG_printf(3, "at dirlevel %d, id = %d\n", level, id);

        for(i = 0; *name != '\0'; i++, name++)
        {
            if((*name == '\\') || (*name == '/'))
            {
                name++;
                break;
            }

            tempName[i] = *name;
        }
        tempName[i] = '\0';

        if(*name == '\0')
        {
            break;
        }

        // locate directory based on it's parent ID
        for(i = 0; ((i < ISOFS_MAX_DIRS) && (isofs_paths[i].parent != 0)); i++)
        {
            if(isofs_paths[i].parent == id)
            {
//                printf("compare %s and %s\n", isofs_paths[i].name, tempName);
                if(strcmp(isofs_paths[i].name, tempName) == 0)
                {
                    break;
                }
            }
        }

        id = i + 1;

        if((i >= ISOFS_MAX_DIRS) || (isofs_paths[i].parent == 0))
        {
            DBG_printf(1, "Couldn't find dir '%s'!\n", tempName);
            return(NULL);
        }
    }

    if(level >= ISOFS_MAX_DIR_LEVEL)
    {
        return(NULL);
    }

    if(_cdCachePath(id) == 0)
    {
        return(NULL);
    }

    if(tempName[0] == '\0')
    {
        tempName[0] = '.';
        tempName[1] = '\0';
    }

    i = strlen(tempName);

    if((i > 2) && (tempName[i - 2] == ';') && (tempName[i - 1] == '1'))
    {
        tempName[i - 2] = '\0'; // strip off ";1"
    }

    for(i = 0; i < ISOFS_MAX_FILES; i++)
    {
        if(isofs_files[i].name[0] == '\0')
        {
            DBG_printf(1, "ISO_SearchFile: File '%s' not found!\n", tempName);
            return(NULL);
        }

//        printf("compare file %s and %s\n", isofs_files[i].name, tempName);
        if(strncmp(isofs_files[i].name, tempName, 12) == 0)
        {
            break;
        }
    }

    if(i >= ISOFS_MAX_FILES)
    {
        DBG_printf(1, "ISO_SearchFile: File '%s' not found!\n", tempName);
        return(NULL);
    }

    memcpy((void *) (fp), &isofs_files[i], sizeof(isofs_file_entry));

    return(&isofs_files[i]);
}

void ISO_reset(void)
{
    _currentCachedPathID = 0;
    _pvdLoaded = 0;
    _CDProbedMode = MODE_NOT_PROBED;
}

int _start(void)
{
    printf("ISOFS v%X.%X\n", MAJOR_VER, MINOR_VER);
    CdInit(0);
    DelDrv(FS_NAME);
    AddDrv(&fsdriver);
    return(0);
}
