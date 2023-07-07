int isOldMecha = 0;

u32 layer1_lsn = 0;
u32 layer1_psn = 0;

int isMediaDVD(void)
{
    return(1);
}

u32 _LsnDualChange(u32 lsn)
{
    int layer = 2;
    u32 change_lsn = lsn;

    if(isMediaDVD() && GetDualInfo())
    {
        if(!dual_emu_on)
        {
            if(otp_or_ptp != 0)
            {
                if(lsn >= layer1_lsn)
                {
                    change_lsn -= 0x10;
                }
            }
        }
        else
        {
            if((otp_or_ptp == 0) && (lsn >= emu_layer1_lsn))
            {
                layer = 1;
            }
            if((otp_or_ptp != 0) && (lsn >= emu_layer1_lsn))
            {
                layer = 0;
            }

            if(layer == 2)
            {
                if(emu_otp_or_ptp != 0)
                {
                    change_lsn -= emu_layer1_lsn;
                }
            }
            else
            {
            }



        }

        Kprintf("LsnDualChange: lsn = 0x%08X(%d), change_lsn = 0x%08X(%d)\n", lsn, lsn, change_lsn, change_lsn);
    }

    return(change_lsn);
}


u8 _getCdSpeed(u8 spin_ctrl, int is_dvd, u32 lsn)
{
    u8 speed;

    switch(spin_ctrl) {
        case 0:
            if(isDVD)
                speed = 0x02;
            else
                speed = 0x04;
            break;
        case 1:
            if(isDVD)
            {
                if(isOldMecha)
                {
                    speed = 0x83;
                }
                else
                {
                    if(dual_type == 0)
                    {
                        if(lsn < 0x00128000)
                        {
                            speed = 0x83;
                        }
                        else
                        {
                            speed = 0x82;
                        }
                    }
                    else if(dual_type == 1) // ptp
                    {
                        if(lsn >= layer1_lsn)
                        {
                            lsn -= layer1_lsn;
                        }

                        if(lsn < 0x00165000)
                        {
                            speed = 0x83;
                        }
                        else
                        {
                            speed = 0x82;
                        }
                    }
                    else if(dual_type == 2) // otp
                    {
                        if(lsn >= layer1_lsn)
                        {
                            lsn -= layer1_lsn;
                        }

                        if(lsn < 0x00165000)
                        {
                            speed = 0x83;
                        }
                        else
                        {
                            speed = 0x82;
                        }
                    }
                }
            }
            else
            {
                speed = 0x85;
            }
            break;
        case 2:
        case 14:
            speed = 0x01;
            break;
        case 3:
            speed = 0x02;
            break;
        case 4:
            if(isDVD)
                speed = 0x02;
            else
                speed = 0x83;
            break;
        case 5:
            if(isDVD)
                speed = 0x03;
            else
                speed = 0x04;
            break;
            break;
        case 10:
            speed = 0x40;
            break;
        case 12:
            if(isDVD)
                speed = 0x04;
            else
                speed = 0x02;
            break;
        case 15:
            speed = 0x82;
            break;
        case 16:
            if(isDVD)
                speed = 0x82;
            else
                speed = 0x83;
            break;
        case 17:
            speed = 0x84;
            break;
        case 18:
            if(isDVD)
                speed = 0x01;
            else
                speed = 0x83;
            break;
        case 20:
            if(isDVD)
                speed = 0x03;
            else
                speed = 0x05;
            break;
        case 6:
        case 7:
        case 8:
        case 9:
        case 11:
        case 13:
        case 19:
        default:
            if(isDVD)
                speed = 0x83;
            else
                speed = 0x85;
            break;
        }

    return(speed);
}


int sceCdReadVideo(u32 lsn, u32 nsects, void *buf, CdRMode_t *rmode, int block_size, void *cb)
{
    int rv = 0;

    if(*CDVD_DISK_TYPE != CdDiskPS2DVD)
    {
        return(0);
    }

    if((mmode != 2) && (mmode != 0xFF))
    {
        return(0);
    }
    
    if(verbose)
    {
        Kprintf("RV read: sec %d num %d spin %d trycnt %d  addr %08x\n", lsn, nsects, rmode->spin_ctrl, rmode->tries, (u32) buf);
    }
    
    _discType = isMediaDVD();
    
    

    return(rv);
}
