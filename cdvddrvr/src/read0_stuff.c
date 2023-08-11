
/* rom0:XCDVDMAN implementation of CdRead */
int CdRead(u32 lbn, u32 nsects, void *buf, CdRMode_t *rmode)
{
    int isPS2_DVD = 0;

    if(*CDVD_DISK_TYPE == CdDiskDVDPS2)
        isPS2_DVD = 1;

    if((!isPS2_DVD || ((*rmode).speed != 1)) || decIsSet)
    {
        return(CdRead0(lbn, nsects, buf, rmode));
    }

    if(((*CDVD_05 & 0xC0) != 0x40) || (read2_flag != 0))
    {
        printf("dbl error r2f= %d waf= %d\n", read2_flag, waf);
        return(0);
    }

    _lastReadBuf = buf;
}

int CdRead0(u32 lbn, u32 nsects, void *buf, CdRMode_t *rmode) {
    u32 var_a0, var_a1, var_t1, var_t0;
    int isDVD = 0;
    u8 params[16];

    var_a1 = nsects;

    _lastReadBuf = buf;

    switch(*CDVD_DISK_TYPE) {
        case CdDiskDVDPS2:
        case CdDiskDVDV:
        case CdDiskDVDV2:
            isDVD = 1;
            break;
        default:
            isDVD = 0;
            break;
        }

    switch((*rmode).pattern) {
        case CddaSect2352:
            var_a0 = 0x20;
            var_a1 *= 16;
            var_t1 = 0x80;
            break;
        case CddaSect2368:
            var_a0 = 0x06;
            var_a1 *= 61;
            var_t1 = 0x86;
            break;
        case CddaSect2448:
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

    _cdReadPattern = (*rmode).pattern;

    switch((*rmode).speed) {
        case 0:
            var_t0 = 0x04;
            break;
        case 2:
            var_t0 = 0x01;
            break;
        case 3:
            var_t0 = 0x02;
            break;
        case 4:
            if(isDVD)
                var_t0 = 0x02;
            else
                var_t0 = 0x03;
            break;
            break;
        case 5:
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
        case 20:
            if(isDVD)
                var_t0 = 0x05;
            else
                var_t0 = 0x03;
            break;
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
    while(CdApplyNCmd(CdNCmdRead0, params, 11) != 0)
        DelayThread(1000);

    return(0);
}
