#ifndef _ISOFS_H
#define _ISOFS_H

#include "ioman.h"

// ISO9660 structures and definitions

#define ISOFS_MAX_OPEN_FILES  16
#define ISOFS_MAX_OPEN_DIRS   16
#define ISOFS_MAX_DIR_LEVEL   8
#define ISOFS_MAX_FILES       64
#define ISOFS_MAX_DIRS        128

typedef struct {
    u8 year; // Number of years since 1900
    u8 month; // Month of the year from 1 to 12
    u8 day; // Day of the Month from 1 to 31
    u8 hour; // Hour of the day from 0 to 23
    u8 min; // Minute of the hour from 0 to 59
    u8 sec; // second of the minute from 0 to 59
    u8 gmtOff; // Offset from Greenwich Mean Time in number of 15 minute intervals from -48(West) to +52(East)
} ISODateTime  __attribute__ ((packed));

/*
typedef struct {
    u8 nameLen;
    u8 dummy01;
    u8 firstSect[4];
    u8 parent[2];
    u8 name[0];
} GenericRootPath __attribute__ ((packed));
*/

typedef struct {
    u8 len_di;
    u8 elen_di;
    u8 lbn[4];
    u8 parent[2];
    u8 path_id[0];
} ISOPathRecord __attribute__ ((packed));

typedef struct {
    u8 len_dr;
    u8 extended_record_length;
    u8 lbn_l[4]; // Logical Block Number of start of file, little endian.
    u8 lbn_b[4];  // Logical Block Number of start of file, big endian.
    u8 data_len_l[4]; // Length of the file in bytes, little endian.
    u8 data_len_b[4];  // Length of the file in bytes, big endian.
    ISODateTime recording_date;
    u8 file_flags;
    u8 file_unit_size;
    u8 interleave_gap_size;
    u8 volume_sequence_number[4];
    u8 len_fi;
    u8 field_id[0];
} ISODirRecord  __attribute__ ((packed));

// Internal structures
typedef struct {
    u32 id;
    u32 parent;
    u32 lbn;
    u8 name[32];
} isofs_path_entry __attribute__ ((packed));

typedef struct {
    u8 sec;
    u8 min;
    u8 hour;
    u8 day;
    u8 month;
    u16 year;
} isofs_date_time;

// structure for getting result from ISO_SearchFile. rom0:CDVDMAN compatible
typedef struct {
    u32 lbn;             // 0x00
    u32 size;            // 0x04
    char name[16];       // 0x08
    isofs_date_time date; // 0x18
} isofs_file_entry_old  __attribute__ ((packed));

// structure for getting result from ISO_SearchFile. newer CDVDMAN and ISOFS compatible
typedef struct {
    u32 lbn;             // 0x00
    u32 size;            // 0x04
    char name[16];       // 0x08
    isofs_date_time date; // 0x18
    u32 flags;
} isofs_file_entry  __attribute__ ((packed));

// structure for internal "open file" list
typedef struct {
    u32 lbn;   // 0x00
    u32 pos;   // 0x04
    u32 size;  // 0x08
    u32 speed; // 0x0C
    u32 mode;  // 0x10
} isofs_file_handle;

// structure for internal "open directory" list
typedef struct {
    u32 lbn;       // the location of the directory in the CD
    fio_dirent_t *files;
    u32 num_files; // number of files in the cache
    u32 entry_pos; // position in the file entry table
    u32 off;	   // current offset in the reading process
    u32 rec_size;  // size of the directory we are reading
} isofs_dir_handle;

void ISO_reset(void);

#endif // _ISOFS_H
