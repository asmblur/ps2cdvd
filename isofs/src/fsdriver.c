/*

ISO9660 file-system driver for CDVDDRVR

*/

#include "types.h"
#include "defs.h"
#include "irx.h"
#include "sys/stat.h"

#include "stdio.h"
#include "sysclib.h"
#include "sysmem.h"
#include "thbase.h"
#include "thsemap.h"
#include "ioman.h"

#include "io_common.h"

#include "../../cdvddrvr/include/cdvddrvr.h"

#include "isofs.h"
#include "fsdriver.h"
#include "common.h"

// External prototypes
extern void ISO_reset(void);
extern isofs_file_entry *ISO_SearchFile(isofs_file_entry *fp, const char *name);
extern int _cdReadSectors(int nsects, int lbn, void *buf, CdRMode_t *rmode);

// Local Prototypes
int FS_init(iop_device_t *);
int FS_deinit(iop_device_t *);
int FS_open(iop_file_t *, const char *, int);
int FS_close(iop_file_t *);
int FS_read(iop_file_t *, void *, int);
int FS_lseek(iop_file_t *, int, int);
int FS_ioctl(iop_file_t *, unsigned long, void *);
int FS_dopen(iop_file_t *, const char *);
int FS_dclose(iop_file_t *);
int FS_dread(iop_file_t *, void *);
int FS_nullDev(void);

// Local Variables
iop_device_ops_t FS_ops = {
    (void *) FS_init,
    (void *) FS_deinit,
    (void *) FS_nullDev,
    (void *) FS_open,
    (void *) FS_close,
    (void *) FS_read,
    (void *) FS_nullDev,
    (void *) FS_lseek,
    (void *) FS_ioctl,
    (void *) FS_nullDev,
    (void *) FS_nullDev,
    (void *) FS_nullDev,
    (void *) FS_dopen,
    (void *) FS_dclose,
    (void *) FS_dread,
    (void *) FS_nullDev,
    (void *) FS_nullDev,
};

iop_device_t fsdriver = {
    FS_NAME,
    IOP_DT_FS,
    0x10,
    FS_DESC,
    &FS_ops
};

isofs_file_handle isofs_file_handles[ISOFS_MAX_OPEN_FILES];
isofs_dir_handle isofs_dir_handles[ISOFS_MAX_OPEN_DIRS];

int _lastFioOpenMode = 0;
int _isofsSemaID = -1;
static int _initialized = 0;

#define M_malloc(_a) AllocSysMemory(0, _a, NULL)
#define M_free(_a) FreeSysMemory(_a)

int FS_init(iop_device_t *device)
{
    iop_sema_t sp;
    int i;

    DBG_printf(3, "ISOFS: file i/o init!\n");

    ISO_reset(); // reset the ISO9660 filesystem

    _lastFioOpenMode = 0;

    for(i = 0; i < ISOFS_MAX_OPEN_FILES; i++)
    {
        isofs_file_handles[i].lbn = 0;
    }

    for(i = 0; i < ISOFS_MAX_OPEN_DIRS; i++)
    {
        isofs_dir_handles[i].lbn = 0;
        isofs_dir_handles[i].files = NULL;
    }

    sp.attr = 1;
    sp.option = 0;
    sp.initial = 1;
    sp.max = 1;

    if((_isofsSemaID = CreateSema(&sp)) < 0) {
        DBG_printf(1, "ISOFS: Error creating semaphore: %d\n", _isofsSemaID);
        return(-1);
        }

    _initialized = 1;

    return(0);
}

int FS_deinit(iop_device_t *device)
{
    DBG_printf(3, "ISOFS: file i/o deinit!\n");

    if(_initialized)
    {
        _lastFioOpenMode = 0;
        DeleteSema(_isofsSemaID);
        ISO_reset();
        _initialized = 0;
    }

    return(1);
}


int _alloc_fileno(void)
{
    int fno;

    for(fno = 0; fno < ISOFS_MAX_OPEN_FILES; fno++)
    {
        if(isofs_file_handles[fno].lbn == 0)
            return(fno);
    }

    return(-1);
}

int _is_valid_fileno(int fno)
{
    isofs_file_handle *fh;

    if((fno < 0) || (fno >= ISOFS_MAX_OPEN_FILES))
    {
        return(0);
    }

    fh = &isofs_file_handles[fno];

    if(((*fh).lbn == 0) || ((*fh).size == 0))
        return(0);

    return(1);
}

int _free_fileno(int fno)
{
    isofs_file_handle *fh;

    if(!_is_valid_fileno(fno))
    {
        return(-1);
    }

    fh = &isofs_file_handles[fno];

    (*fh).lbn = (*fh).pos = (*fh).size = (*fh).mode = 0;

    return(0);
}

int _alloc_dirno(void)
{
    int dno;

    for(dno = 0; dno < ISOFS_MAX_OPEN_DIRS; dno++)
    {
        if(isofs_dir_handles[dno].lbn == 0)
        {
            return(dno);
        }
    }

    return(-1);
}

int _is_valid_dirno(int dno)
{
    isofs_dir_handle *dh;

    if((dno < 0) || (dno >= ISOFS_MAX_OPEN_DIRS))
    {
        return(0);
    }

    dh = &isofs_dir_handles[dno];

    if((*dh).lbn == 0)
        return(0);

    return(1);
}

int _free_dirno(int dno)
{
    isofs_dir_handle *dh;

    if(!_is_valid_dirno(dno))
    {
        return(-1);
    }

    dh = &isofs_dir_handles[dno];

    if((*dh).files)
    {
        M_free((*dh).files);
        (*dh).files = NULL;
    }

    (*dh).lbn = (*dh).num_files = 0;
    (*dh).entry_pos = -1;

    return(0);
}

int FS_open(iop_file_t *file, const char *name, int mode)
{
    int fno;
    isofs_file_handle *fh;
    isofs_file_entry fep;

    DBG_printf(3, "ISOFS: file i/o open(\"%s\", %d)!\n", name, mode);

    WaitSema(_isofsSemaID);

    while(_cdvdWaitReady(100) != 1)
    {
        if((*CDVD_DISK_TYPE == 0x00) || (*CDVD_DISK_TYPE == 0xFF))
        {
            DBG_printf(1, "ISOFS: device not ready!\n");
            SignalSema(_isofsSemaID);
            return(-1);
        }
    }

    if((fno = _alloc_fileno()) < 0)
    {
        DBG_printf(1, "ISOFS: too many open files!\n");
        SignalSema(_isofsSemaID);
        return(-1);
    }

    (u32) (*file).privdata = fno;

    fh = &isofs_file_handles[fno];

    _lastFioOpenMode = mode;

    if(ISO_SearchFile(&fep, name) == NULL)
    {
        DBG_printf(1, "ISOFS: fioOpen - Unable to locate file '%s'!\n", name);
        SignalSema(_isofsSemaID);
        return(-1);
    }

    _lastFioOpenMode = 0;

    (*fh).lbn = fep.lbn;
    (*fh).pos = 0;
    (*fh).size = fep.size;
    (*fh).speed = mode;
    (*fh).mode = (*file).mode = 1;

    SignalSema(_isofsSemaID);

    return(0);
}

int FS_close(iop_file_t *file)
{
    DBG_printf(3, "ISOFS: file i/o close!\n");

    WaitSema(_isofsSemaID);

    if(_free_fileno((int) (*file).privdata) != 0)
    {
        SignalSema(_isofsSemaID);
        return(-1);
    }

    (*file).mode = 0;

    SignalSema(_isofsSemaID);
    return(0);
}

int FS_read(iop_file_t *file, void *buf, int length)
{
    isofs_file_handle *fh;
    CdRMode_t rmode = { 16, 1, 0, 0 };
    u32 lbn;
    int fno;
    int toRead, bytesLeft;
    static u8 scache[2048];
    static u32 s_lbn = -1;

    DBG_printf(3, "ISOFS: file i/o read!\n");

    WaitSema(_isofsSemaID);

    fno = (int) (*file).privdata;
    if(!_is_valid_fileno(fno))
    {
        SignalSema(_isofsSemaID);
        return(-1);
    }

    fh = &isofs_file_handles[fno];

    if(((*fh).speed >= 0) && ((*fh).speed <= 5))
    {
        rmode.speed = (*fh).speed;
    }

    if(((*fh).pos + length) > (*fh).size)
    { // don't allow trying to read past EOF
        length = ((*fh).size - (*fh).pos);
    }

    bytesLeft = length;
    while(bytesLeft > 0)
    {
        lbn = ((*fh).lbn + ((*fh).pos / 2048));

        if(((*fh).pos % 2048) || (bytesLeft < 2048))
        {
            if((*fh).pos % 2048)
            { // not aligned on sector boundry
                toRead = (2048 - ((*fh).pos % 2048));

                if(toRead > bytesLeft)
                    toRead = bytesLeft;
            }
            else
            {
                toRead = bytesLeft;
            }

            if(s_lbn != lbn)
            {
                if(_cdReadSectors(1, lbn, scache, &rmode) != 1)
                {
                    DBG_printf(1, "ISOFS: Read error!\n");
                    SignalSema(_isofsSemaID);
                    return(-1);
                }

                s_lbn = lbn;
            }

            memcpy(&((u8 *) (buf))[length - bytesLeft], &scache[(*fh).pos % 2048], toRead);
        }
        else
        { // Bytes to read >= 2048 and file position is on 2048 boundry

            toRead = bytesLeft / 2048;  // number of sectors to read(not counting extra)
            if(_cdReadSectors(toRead, lbn, &((u8 *) (buf))[length - bytesLeft], &rmode) != toRead)
            {
                DBG_printf(1, "ISOFS: Read error!\n");
                SignalSema(_isofsSemaID);
                return(-1);
            }
            toRead *= 2048; // change to bytes
        }

        bytesLeft -= toRead;
        (*fh).pos += toRead;
    }

    SignalSema(_isofsSemaID);
    return(length - bytesLeft);
}

int FS_lseek(iop_file_t *file, int off, int mode)
{
    isofs_file_handle *fh;
    int fno;
    int newoff;

    DBG_printf(3, "ISOFS: file i/o lseek!\n");

    WaitSema(_isofsSemaID);

    fno = (int) (*file).privdata;
    if(!_is_valid_fileno(fno))
    {
        SignalSema(_isofsSemaID);
        return(-1);
    }

    fh = &isofs_file_handles[fno];

    switch(mode)
    {
        case SEEK_SET:
            newoff = off;
            break;
        case SEEK_CUR:
            newoff = (*fh).pos + off;
            break;
        case SEEK_END:
            newoff = (*fh).size + off;
            break;
        default:
            newoff = -1;
            break;
    }

    if(newoff < 0)
    {
        newoff = -1;
    }
    else
    {
        (*fh).pos = newoff;
    }

    SignalSema(_isofsSemaID);
    return(newoff);
}

int FS_ioctl(iop_file_t *file, unsigned long funcNo, void *buf)
{
    DBG_printf(3, "ISOFS: file i/o ioctl!\n");
    return(-1);
}

int FS_dopen(iop_file_t *file, const char *name)
{
    int dno;
    isofs_dir_handle *dh;
    isofs_file_entry fep;

    DBG_printf(3, "ISOFS-FS_dopen: start!\n");

    WaitSema(_isofsSemaID);

    while(_cdvdWaitReady(100) != 1)
    {
        if((*CDVD_DISK_TYPE == 0x00) || (*CDVD_DISK_TYPE == 0xFF))
        {
            DBG_printf(1, "ISOFS: device not ready!\n");
            SignalSema(_isofsSemaID);
            return(-1);
        }
    }

    _lastFioOpenMode = O_RDONLY;

    if(ISO_SearchFile(&fep, name) == NULL)
    {
        DBG_printf(1, "ISOFS-FS_dopen: Unable to locate directory '%s'!\n", name);
        SignalSema(_isofsSemaID);
        return(-1); // errno!
    }

    _lastFioOpenMode = 0;

    if((dno = _alloc_dirno()) < 0)
    {
        DBG_printf(1, "ISOFS-FS_dopen: too many open directories!\n");
        SignalSema(_isofsSemaID);
        return(-1); // errno!
    }

    (u32) (*file).privdata = dno;

    dh = &isofs_dir_handles[dno];

    (*dh).lbn = fep.lbn;
    (*dh).num_files = 0;
    (*dh).entry_pos = -1;
    (*dh).files = NULL;
    (*dh).off = 0;
    (*dh).rec_size = 0;

    if((fep.flags & 2) == 0)
    { // fail if file is not a directory
        _free_dirno(dno);
        DBG_printf(1, "ISOFS-FS_dopen: File '%s' is not a directory!\n", name);
        SignalSema(_isofsSemaID);
        return(-1); // errno!
    }

    if(((*dh).files = (fio_dirent_t *) M_malloc(sizeof(fio_dirent_t) * ISOFS_MAX_FILES)) == NULL)
    {
        _free_dirno(dno);
        DBG_printf(1, "ISOFS-FS_dopen: Unable to allocate directory entry buffer!\n");
        SignalSema(_isofsSemaID);
        return(-1); // errno!
    }

    SignalSema(_isofsSemaID);
    return(0);
}

int FS_dclose(iop_file_t *file)
{
    DBG_printf(3, "ISOFS: directory close!\n");

    WaitSema(_isofsSemaID);

    if(_free_dirno((int) (*file).privdata) != 0)
    {
        SignalSema(_isofsSemaID);
        return(-1);
    }

    (*file).mode = 0;
    SignalSema(_isofsSemaID);
    return(0);
}

int _cdCacheDir(isofs_dir_handle *dh)
{
    static int s_lbn = -1;
    static u8 scache[2048];
    ISODirRecord *iso_dir;
    fio_dirent_t *de;
    CdRMode_t rmode = { 16, 1, 0, 0 };
    int lbn;

    DBG_printf(3, "ISOFS-_cdCacheDir: start!\n");

    if(((*dh).entry_pos == -1) ||
          // not already cached, cache it!
       (((*dh).entry_pos == ISOFS_MAX_FILES) && ((*dh).off < (*dh).rec_size)))
          // cache exhausted, let's refill it!
    {
        (*dh).entry_pos = 0;
        (*dh).num_files = 0;

        do
        {
            lbn = (*dh).lbn + ((*dh).off / 2048);

            if(lbn != s_lbn)
            {
                s_lbn = lbn;

                if(_cdReadSectors(1, s_lbn, scache, &rmode) != 1)
                {
                    return(-1); // errno!
                }
            }

            do
            {
                iso_dir = (ISODirRecord *)(scache + ((*dh).off % 2048));

                if((*iso_dir).len_dr > 0)
                {
                    de = &(*dh).files[(*dh).num_files];

                    if(((*iso_dir).len_fi == 1) && ((*iso_dir).field_id[0] == 0))
                    {
        			// Maybe should check if (*dh).rec_size isn't already set up... would mean that
        			// we have another '.' entry here, which should be fucked anyway, but, well...
                        (*dh).rec_size = ((*iso_dir).data_len_l[0] | ((*iso_dir).data_len_l[1] << 8) | ((*iso_dir).data_len_l[2] << 16) | ((*iso_dir).data_len_l[3] << 24));
                        strcpy((*de).name, ".");
                    }
                    else if(((*iso_dir).len_fi == 1) && ((*iso_dir).field_id[0] == 1))
                    {
                        strcpy((*de).name, "..");
                    }
                    else
                    {
                        memcpy((*de).name, (*iso_dir).field_id, (*iso_dir).len_fi);
                        (*de).name[(*iso_dir).len_fi] = '\0';

                        if((*iso_dir).len_fi >= 2)
                        {
                            if(((*de).name[(*iso_dir).len_fi - 2] == ';') && ((*de).name[(*iso_dir).len_fi - 1] == '1'))
                            {
                                (*de).name[(*iso_dir).len_fi - 2] = '\0'; // strip off ";1"
                            }
                        }
                    }
                    
                    (*de).stat.mode = FIO_SO_IROTH | FIO_SO_IWOTH | FIO_SO_IXOTH; //FIO_S_IRUSR | FIO_S_IXUSR | FIO_S_IRGRP | FIO_S_IXGRP | FIO_S_IROTH | FIO_S_IXOTH;
                    (*de).stat.mode |= (((*iso_dir).file_flags & 2) != 0) ? FIO_SO_IFDIR : FIO_SO_IFREG;
                    (*de).stat.attr = 0;
                    (*de).stat.size = ((*iso_dir).data_len_l[0] | ((*iso_dir).data_len_l[1] << 8) | ((*iso_dir).data_len_l[2] << 16) | ((*iso_dir).data_len_l[3] << 24));
                    (*de).stat.hisize = 0;

                    // year is always wicked... let's keep some code for later on :)

                    (*de).stat.ctime[0] = 0; // unused?
                    (*de).stat.ctime[1] = (*iso_dir).recording_date.sec;
                    (*de).stat.ctime[2] = (*iso_dir).recording_date.min;
                    (*de).stat.ctime[3] = (*iso_dir).recording_date.hour;
                    (*de).stat.ctime[4] = (*iso_dir).recording_date.day;
                    (*de).stat.ctime[5] = (*iso_dir).recording_date.month;
                    (*de).stat.ctime[6] = ((*iso_dir).recording_date.year + 1900) & 0xFF;
                    (*de).stat.ctime[7] = (((*iso_dir).recording_date.year + 1900) >> 8) & 0xFF;

                    memcpy((*de).stat.atime, (*de).stat.ctime, sizeof((*de).stat.atime));
                    memcpy((*de).stat.mtime, (*de).stat.ctime, sizeof((*de).stat.mtime));

                    (*dh).off += (*iso_dir).len_dr; // prepare for next record.

                    if((++(*dh).num_files) == ISOFS_MAX_FILES)
		    {
			// cache is stuffed (*burps*), let's exit for now.
			return(0);
		    }
                }
                else
                {
                    (*dh).off++;
                }

            } while((*dh).off % 2048);

        } while((*dh).off < (*dh).rec_size);
    }

    return(0);
}

int FS_dread(iop_file_t *file, void *_buf)
{
    isofs_dir_handle *dh;
    int dno, rv = 0;

    DBG_printf(3, "ISOFS-FS_dread: start!\n");

    WaitSema(_isofsSemaID);

    dno = (int) (*file).privdata;
    if(!_is_valid_dirno(dno))
    {
        DBG_printf(1, "ISOFS-FS_dread: bad dno %d!\n", dno);
        SignalSema(_isofsSemaID);
        return(-1); // have a correct errno here ?
    }

    dh = &isofs_dir_handles[dno];
    if((rv = _cdCacheDir(dh)) < 0)
    {
        DBG_printf(1, "ISOFS-FS_dread: error caching dir!\n");
        SignalSema(_isofsSemaID);
        return(-1); // have a correct errno here ?
    }

    if((*dh).entry_pos >= (*dh).num_files)
    { // no more files
        SignalSema(_isofsSemaID);
        return(0);
    }

    memcpy(_buf, &(*dh).files[(*dh).entry_pos++], sizeof(fio_dirent_t));

    SignalSema(_isofsSemaID);
    return(sizeof(fio_dirent_t));
}

int FS_nullDev(void) {
    DBG_printf(1, "ISOFS: Unsupported file i/o call!\n");
    return(-1);
}
