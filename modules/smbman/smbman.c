/*
  Copyright 2009, jimmikaelkael
  Licenced under Academic Free License version 3.0
  Review OpenUsbLd README & LICENSE files for further details.
*/

#include <loadcore.h>
#include <stdio.h>
#include <sysclib.h>
#include <thbase.h>
#include <intrman.h>
#include <sifman.h>
#include <sifrpc.h>
#include <ioman.h>
#include <thsemap.h>
#include <errno.h>
#include <io_common.h>

#include "tcp.h"
#include "ops.h"

#define MODNAME "smbman"
IRX_ID(MODNAME, 1, 1);

int smbman_initdev(void);

// smb driver ops functions prototypes
int smb_dummy(void);
int smb_init(iop_device_t *iop_dev);
int smb_deinit(iop_device_t *dev);
int smb_open(iop_file_t *f, char *filename, int mode, int flags);
int smb_close(iop_file_t *f);
int smb_lseek(iop_file_t *f, u32 pos, int where);
int smb_read(iop_file_t *f, void *buf, u32 size);

int smbman_io_sema;

// driver ops func tab
void *smbman_ops[17] = {
	(void*)smb_init,
	(void*)smb_deinit,
	(void*)smb_dummy,
	(void*)smb_open,
	(void*)smb_close,
	(void*)smb_read,
	(void*)smb_dummy,
	(void*)smb_lseek,
	(void*)smb_dummy,
	(void*)smb_dummy,
	(void*)smb_dummy,
	(void*)smb_dummy,
	(void*)smb_dummy,
	(void*)smb_dummy,
	(void*)smb_dummy,
	(void*)smb_dummy,
	(void*)smb_dummy
};

// driver descriptor
static iop_device_t smbdev = {
	"smb", 
	IOP_DT_FS,
	1,
	"SMB ",
	(struct _iop_device_ops *)&smbman_ops
};

typedef struct {  // size = 11		
	u8  status;   // 0
	u16 smb_fid;  // 1
	u32 position; // 3 
	u32 filesize; // 7
} FHANDLE;

#define MAX_FDHANDLES 		128
FHANDLE smbman_fdhandles[MAX_FDHANDLES];

//-------------------------------------------------------------------------
int _start(int argc, char** argv)
{	
	iop_sema_t smp;
			
	//printf("smbman v1.0 - jimmikaelkael\n");

	SifInitRpc(0);
		
	// Install new device driver
	if (smbman_initdev())
		return MODULE_NO_RESIDENT_END;
		
	smp.attr = 1;
	smp.initial = 1;
	smp.max = 1;
	smp.option = 0;
	smbman_io_sema = CreateSema(&smp);
		
    // Next open the Connection with SMB server
    smbConnect("192.168.0.2", 445);
    	
    // Then open a session and a tree connect on the share resource 	
    smbLogin("GUEST", "", "\\\\192.168.0.2\\PUBLIC");
    								
	return MODULE_RESIDENT_END;
}

//-------------------------------------------------------------- 
int smbConnect(char *SMBServerIP, int SMBServerPort)
{
	int r;

	WaitSema(smbman_io_sema);	
		
	r = tcp_ConnectSMBClient(SMBServerIP, SMBServerPort);

	SignalSema(smbman_io_sema);
	
	return r;
}

//-------------------------------------------------------------- 
int smbLogin(char *User, char *Password, char *Share)
{
	int r;

	WaitSema(smbman_io_sema);	
	
	r = tcp_SessionSetup(User, Password, Share);

	SignalSema(smbman_io_sema);
	
	return r;
}

//-------------------------------------------------------------- 
int smbDisconnect(void)
{
	int r;

	WaitSema(smbman_io_sema);	
	
	//printf("smbman: smbDisconnect\n");		
	r = tcp_DisconnectSMBClient();

	SignalSema(smbman_io_sema);
	
	return r;
}

//-------------------------------------------------------------- 
int smbEchoSMBServer(u8 *msg, int size)
{
	int r;

	WaitSema(smbman_io_sema);	
		
	r = tcp_EchoSMBServer(msg, size);

	SignalSema(smbman_io_sema);
	
	return r;
}

//-------------------------------------------------------------- 
int smbOpen(int unit, char *filename, int flags)
{
	int i, r, fd;
	u32 filesize;
	u16 smb_fid;
	FHANDLE *fh;
	
	//printf("smbman: smbOpen unit%d filename %s flags %x\n", unit, filename, flags);
	
	for (fd = 0; fd < MAX_FDHANDLES; fd++) {
		fh = (FHANDLE *)&smbman_fdhandles[fd];
		if (fh->status == 0)
			break;
	}
	
	if (fd == MAX_FDHANDLES)
		return -7;
			
	memset((void *)fh, 0, sizeof (FHANDLE));	
		
	// chop ";1" pattern
	for (i = 0; filename[i] != 0; i++) {
		if ((filename[i] == ';') && (filename[i + 1] == '1')) {
			filename[i] = '\0';
		}
	}
	
	r = tcp_Open(filename, &smb_fid, &filesize);
	if (r != 0)
		return r;
		
	fh->smb_fid = smb_fid;
	fh->filesize = filesize;	
	fh->status = 1;
	
	return fd;	
}

//-------------------------------------------------------------- 
int smbClose(int fd)
{
	FHANDLE *fh;
	int r;
	
	//printf("smbman: smbClose fd %d\n", fd);

	if (!((u32)fd < MAX_FDHANDLES))
		return -5;
			
	fh = (FHANDLE *)&smbman_fdhandles[fd];	
	if (!fh->status) 
		return -5;	
	
	r = tcp_Close(fh->smb_fid);	
	if (r != 0)
		return r;	
		
	fh->status = 0;
			
	return 0;	
}

//-------------------------------------------------------------- 
int smbCloseAll(void)
{
	int fd = 0;
	int rv = 0;
	int rc;
	
	//printf("smbman: smbCloseAll\n");

	do {
		if (smbman_fdhandles[fd].status) {
			rc = smbClose(fd);
			if (rc < rv)
				rv = rc;   
		}
		fd++;
		
	} while (fd < MAX_FDHANDLES);

	return rv;
}

//-------------------------------------------------------------- 
int smbRead(int fd, void *buf, u32 nbytes)
{
	int r;
	FHANDLE *fh;
	u32 rpos, size;
	//char mbuf[128];
		
	if (!((u32)fd < MAX_FDHANDLES))
		return -5;
			
	fh = (FHANDLE *)&smbman_fdhandles[fd];	
	if (!fh->status) 
		return -5;

	//sprintf(mbuf, "smbman: smbRead fd %d nbytes %d fh->position=%d fh->filesize=%d\n", fd, (int)nbytes, fh->position, fh->filesize);
	//smbEchoSMBServer(mbuf, strlen(mbuf));
		
	if (fh->position >= fh->filesize)
		return 0;
		
	if (nbytes >= (fh->filesize - fh->position))
		nbytes = fh->filesize - fh->position;

	rpos = 0;	
	if (nbytes) {
		do {
			if (nbytes > 0xffff)
				size = 0xffff;
			else	
				size = nbytes;

			r = tcp_Read(fh->smb_fid, (void *)(buf + rpos), fh->position, (u16)size);
			if (r < 0) {
   				fh->status = 0;
    			return r;
			}
			
			size = r;		
			rpos += size;
			nbytes -= size;
			fh->position += size;
			
		} while (nbytes);	
	}	
	
	//sprintf(mbuf, "smbman: smbRead ret=%d\n", rpos);
	//smbEchoSMBServer(mbuf, strlen(mbuf));	
										
    return rpos;
}

//-------------------------------------------------------------- 
int smbSeek(int fd, u32 offset, int origin)
{
	int r;
	FHANDLE *fh;
	
	//printf("smbman: smbSeek fd %d offset %d origin %d\n", fd, (int)offset, origin);
	
	if (!((u32)fd < MAX_FDHANDLES))
		return -5;
			
	fh = (FHANDLE *)&smbman_fdhandles[fd];	
	if (!fh->status) 
		return -5;
	
	switch (origin) {
		default:		
		case SEEK_CUR:
			r = fh->position + offset;
			break;
		case SEEK_SET:	
			r = offset;
			break;			
		case SEEK_END:	
			r = fh->filesize + offset;		
			break;			
	}

	return fh->position = (r < 0) ? 0 : r;
}

//-------------------------------------------------------------- 
int smbGetDir(char *dirname, int maxent, void *info)
{
	int r;
	
	//printf("smbman: smbGetDir dirname %s maxent %d\n", dirname, maxent);

	WaitSema(smbman_io_sema);	

	r = tcp_GetDir(dirname, maxent, (smb_FindFirst2_Entry *)info);
	
	SignalSema(smbman_io_sema);	
	
	return r;	
}

//-------------------------------------------------------------- 
int smb_dummy(void)
{
	return 0;
}

//-------------------------------------------------------------- 
int smb_init(iop_device_t *dev)
{
	return 0;
}

//-------------------------------------------------------------- 
int smbman_initdev(void)
{
	DelDrv(smbdev.name);
	if (AddDrv(&smbdev)) {
		smbCloseAll();
		return 1;
	}
	
	return 0;
}

//-------------------------------------------------------------- 
int smb_deinit(iop_device_t *dev)
{
	//DeleteSema(smbman_io_sema);
	smbCloseAll();
	smbDisconnect();
	
	return 0;
}

//-------------------------------------------------------------- 
int smb_open(iop_file_t *f, char *filename, int mode, int flags)
{
	register int r;
	
	WaitSema(smbman_io_sema);
	
	r = smbOpen(f->unit, filename, mode);
	if (r >= 0)
		f->privdata = (void*)r;

	SignalSema(smbman_io_sema);
	
	return r;
}

//-------------------------------------------------------------- 
int smb_close(iop_file_t *f)
{
	register int r;

	WaitSema(smbman_io_sema);
	r = smbClose((int)f->privdata);
	SignalSema(smbman_io_sema);

	return r;
}

//-------------------------------------------------------------- 
int smb_lseek(iop_file_t *f, u32 pos, int where)
{
	register int r;

	WaitSema(smbman_io_sema);
	r = smbSeek((int)f->privdata, pos, where);
	SignalSema(smbman_io_sema);
	
	return r;
}

//-------------------------------------------------------------- 
int smb_read(iop_file_t *f, void *buf, u32 size)
{
	register int r;

	WaitSema(smbman_io_sema);
	r = smbRead((int)f->privdata, buf, size);
	SignalSema(smbman_io_sema);
	
	return r;
}
