
#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <fileio.h>
#include <fileXio_rpc.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <sbv_patches.h>
#include <iopcontrol.h>
#include <iopheap.h>
#include <debug.h>
#include <sys/time.h>
#include <time.h>

#include "smbman.h"

#define	IP_ADDR "192.168.0.10"
#define	NETMASK "255.255.255.0"
#define	GATEWAY "192.168.0.1"

extern void poweroff_irx;
extern int  size_poweroff_irx;
extern void ps2dev9_irx;
extern int  size_ps2dev9_irx;
extern void smsutils_irx;
extern int  size_smsutils_irx;
extern void smstcpip_irx;
extern int  size_smstcpip_irx;
extern void smsmap_irx;
extern int  size_smsmap_irx;
extern void smbman_irx;
extern int  size_smbman_irx;
extern void iomanx_irx;
extern int  size_iomanx_irx;
extern void filexio_irx;
extern int  size_filexio_irx;

// for IP config
#define IPCONFIG_MAX_LEN	64
static char g_ipconfig[IPCONFIG_MAX_LEN] __attribute__((aligned(64)));
static int g_ipconfig_len;

static ShareEntry_t sharelist[128] __attribute__((aligned(64))); // Keep this aligned for DMA!

typedef struct {
	u8	unused;
	u8	sec;
	u8	min;
	u8	hour;
	u8	day;
	u8	month;
	u16	year;
} ps2time_t;

#define isYearLeap(year)	(!((year) % 4) && (((year) % 100) || !((year) % 400)))

//-------------------------------------------------------------- 
// I wanted this to be done on IOP, but unfortunately, the compiler
// can't handle div ops on 64 bit numbers.

static ps2time_t *smbtime2ps2time(s64 smbtime, ps2time_t *ps2time)
{
	const int mdtab[2][12] = {  
		{ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
  		{ 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }	// leap year
	};
	register u32 dayclock, dayno;
	s64 UnixSystemTime;
	register int year = 1970;					// year of the Epoch

	memset((void *)ps2time, 0, sizeof(ps2time_t));

	// we add 5x10^6 to the number before scaling it to seconds and subtracting
	// the constant (seconds between 01/01/1601 and the Epoch: 01/01/1970),
	// so that we round in the same way windows does.
	UnixSystemTime = (s64)(((smbtime + 5000000) / 10000000) - 11644473600);

	dayclock = UnixSystemTime % 86400;
	dayno = UnixSystemTime / 86400;

	ps2time->sec = dayclock % 60;
	ps2time->min = (dayclock % 3600) / 60;
	ps2time->hour = dayclock / 3600;
	while (dayno >= (isYearLeap(year) ? 366 : 365)) {
		dayno -= (isYearLeap(year) ? 366 : 365);
		year++;
	}
	ps2time->year = year;
	ps2time->month = 0;
	while (dayno >= mdtab[isYearLeap(year)][ps2time->month]) {
		dayno -= mdtab[isYearLeap(year)][ps2time->month];
		ps2time->month++;
	}
	ps2time->day = dayno + 1;
	ps2time->month++;

	return (ps2time_t *)ps2time;
}

//-------------------------------------------------------------- 
void set_ipconfig(void)
{
	memset(g_ipconfig, 0, IPCONFIG_MAX_LEN);
	g_ipconfig_len = 0;

	strncpy(&g_ipconfig[g_ipconfig_len], IP_ADDR, 15);
	g_ipconfig_len += strlen(IP_ADDR) + 1;
	strncpy(&g_ipconfig[g_ipconfig_len], NETMASK, 15);
	g_ipconfig_len += strlen(NETMASK) + 1;
	strncpy(&g_ipconfig[g_ipconfig_len], GATEWAY, 15);
	g_ipconfig_len += strlen(GATEWAY) + 1;
}

//-------------------------------------------------------------- 
int main(int argc, char *argv[2])
{
	int i, ret, id;
	
	init_scr();
	scr_clear();
	scr_printf("\t smblab\n\n");
	
	SifInitRpc(0);

	scr_printf("\t IOP Reset... ");
	
	while(!SifIopReset("rom0:UDNL rom0:EELOADCNF",0));
  	while(!SifIopSync());;
  	fioExit();
  	SifExitIopHeap();
  	SifLoadFileExit();
  	SifExitRpc();
  	SifExitCmd();
  	
  	SifInitRpc(0);
  	FlushCache(0);
  	FlushCache(2);
 	    		  	
	SifLoadFileInit();
	SifInitIopHeap();
	
	sbv_patch_enable_lmb();
	sbv_patch_disable_prefix_check();
                  	    
	SifLoadModule("rom0:SIO2MAN", 0, 0);
	SifLoadModule("rom0:MCMAN", 0, 0);
	SifLoadModule("rom0:MCSERV", 0, 0);

	scr_printf("OK\n");
        	  
	set_ipconfig();
 
	scr_printf("\t loading modules... ");
 
	id = SifExecModuleBuffer(&iomanx_irx, size_iomanx_irx, 0, NULL, &ret);
	id = SifExecModuleBuffer(&filexio_irx, size_filexio_irx, 0, NULL, &ret);
	id = SifExecModuleBuffer(&poweroff_irx, size_poweroff_irx, 0, NULL, &ret);
	id = SifExecModuleBuffer(&ps2dev9_irx, size_ps2dev9_irx, 0, NULL, &ret);
	id = SifExecModuleBuffer(&smsutils_irx, size_smsutils_irx, 0, NULL, &ret);
	id = SifExecModuleBuffer(&smstcpip_irx, size_smstcpip_irx, 0, NULL, &ret);
	id = SifExecModuleBuffer(&smsmap_irx, size_smsmap_irx, g_ipconfig_len, g_ipconfig, &ret);
	id = SifExecModuleBuffer(&smbman_irx, size_smbman_irx, 0, NULL, &ret);

	scr_printf("OK\n");

	fileXioInit();


	// ----------------------------------------------------------------
	// how to get password hashes:
	// ----------------------------------------------------------------

	smbGetPasswordHashes_in_t passwd;
	smbGetPasswordHashes_out_t passwdhashes;

	strcpy(passwd.password, "mypassw");

	scr_printf("\t GETPASSWORDHASHES... ");
	ret = fileXioDevctl("smb:", SMB_DEVCTL_GETPASSWORDHASHES, (void *)&passwd, sizeof(passwd), (void *)&passwdhashes, sizeof(passwdhashes));
	if (ret == 0) {
		scr_printf("OK\n");
		scr_printf("\t LMhash   = 0x");
		for (i=0; i<16; i++)
			scr_printf("%02X", passwdhashes.LMhash[i]);
		scr_printf("\n");
		scr_printf("\t NTLMhash = 0x");
		for (i=0; i<16; i++)
			scr_printf("%02X", passwdhashes.NTLMhash[i]);
		scr_printf("\n");
	}
	else
		scr_printf("Error %d\n", ret);

	// we now have 32 bytes of hash (16 bytes LM hash + 16 bytes NTLM hash)
	// ----------------------------------------------------------------


	// ----------------------------------------------------------------
	// how to open connection to SMB server:
	// ----------------------------------------------------------------

	smbConnect_in_t connect;

	strcpy(connect.serverIP, "192.168.0.2");
	connect.serverPort = 445;

	scr_printf("\t CONNECT... ");
	ret = fileXioDevctl("smb:", SMB_DEVCTL_CONNECT, (void *)&connect, sizeof(connect), NULL, 0);
	if (ret == 0)
		scr_printf("OK  ");
	else
		scr_printf("Error %d  ", ret);


	// ----------------------------------------------------------------
	// how to send an Echo to SMB server to test if it's alive:
	// ----------------------------------------------------------------
	smbEcho_in_t echo;

	strcpy(echo.echo, "ALIVE ECHO TEST");
	echo.len = strlen("ALIVE ECHO TEST");

	scr_printf("ECHO... ");
	ret = fileXioDevctl("smb:", SMB_DEVCTL_ECHO, (void *)&echo, sizeof(echo), NULL, 0);
	if (ret == 0)
		scr_printf("OK  ");
	else
		scr_printf("Error %d  ", ret);


	// ----------------------------------------------------------------
	// how to LOGON to SMB server:
	// ----------------------------------------------------------------

	smbLogOn_in_t logon;

	strcpy(logon.User, "GUEST");
	//strcpy(logon.User, "jimmikaelkael");
	// we could reuse the generated hash
	//memcpy((void *)logon.Password, (void *)&passwdhashes, sizeof(passwdhashes));
	//logon.PasswordType = HASHED_PASSWORD;
	// or log sending the plaintext password
	//strcpy(logon.Password, "mypassw");
	//logon.PasswordType = PLAINTEXT_PASSWORD;
	// or simply tell we're not sending password
	//logon.PasswordType = NO_PASSWORD;

	scr_printf("LOGON... ");
	ret = fileXioDevctl("smb:", SMB_DEVCTL_LOGON, (void *)&logon, sizeof(logon), NULL, 0);
	if (ret == 0)
		scr_printf("OK\n");
	else
		scr_printf("Error %d\n", ret);


	// ----------------------------------------------------------------
	// how to get the available share list:
	// ----------------------------------------------------------------

	smbGetShareList_in_t getsharelist;

	getsharelist.EE_addr = (void *)&sharelist[0];
	getsharelist.maxent = 128;

	scr_printf("\t GETSHARELIST... ");
	ret = fileXioDevctl("smb:", SMB_DEVCTL_GETSHARELIST, (void *)&getsharelist, sizeof(getsharelist), NULL, 0);
	if (ret >= 0) {
		scr_printf("OK count = %d\n", ret);
		for (i=0; i<ret; i++) {
			scr_printf("\t\t - %s: %s\n", sharelist[i].ShareName, sharelist[i].ShareComment);
		}
	}
	else
		scr_printf("Error %d\n", ret);


	// ----------------------------------------------------------------
	// how to open a share:
	// ----------------------------------------------------------------

	smbOpenShare_in_t openshare;

	strcpy(openshare.ShareName, "PS2SMB");
	// we could reuse the generated hash
	//memcpy((void *)logon.Password, (void *)&passwdhashes, sizeof(passwdhashes));
	//logon.PasswordType = HASHED_PASSWORD;
	// or log sending the plaintext password
	//strcpy(logon.Password, "mypassw");
	//logon.PasswordType = PLAINTEXT_PASSWORD;
	// or simply tell we're not sending password
	//logon.PasswordType = NO_PASSWORD;

	scr_printf("\t OPENSHARE... ");
	ret = fileXioDevctl("smb:", SMB_DEVCTL_OPENSHARE, (void *)&openshare, sizeof(openshare), NULL, 0);
	if (ret == 0)
		scr_printf("OK\n");
	else
		scr_printf("Error %d\n", ret);


	// ----------------------------------------------------------------
	// how to query disk informations: (you must be connected to a share)
	// ----------------------------------------------------------------
	smbQueryDiskInfo_out_t querydiskinfo;

	scr_printf("\t QUERYDISKINFO... ");
	ret = fileXioDevctl("smb:", SMB_DEVCTL_QUERYDISKINFO, NULL, 0, (void *)&querydiskinfo, sizeof(querydiskinfo));
	if (ret == 0) {
		scr_printf("OK\n");
		scr_printf("\t Total Units = %d, BlocksPerUnit = %d\n", querydiskinfo.TotalUnits, querydiskinfo.BlocksPerUnit);
		scr_printf("\t BlockSize = %d, FreeUnits = %d\n", querydiskinfo.BlockSize, querydiskinfo.FreeUnits);
	}
	else
		scr_printf("Error %d\n", ret);


	// ----------------------------------------------------------------
	// getstat test:
	// ----------------------------------------------------------------

	scr_printf("\t IO getstat... ");

	iox_stat_t stats;
	ret = fileXioGetStat("smb:\\dossier", &stats);
	if (ret == 0) {
		scr_printf("OK\n");

		s64 smb_ctime, smb_atime, smb_mtime;
		ps2time_t ctime, atime, mtime;

		memcpy((void *)&smb_ctime, stats.ctime, 8);
		memcpy((void *)&smb_atime, stats.atime, 8);
		memcpy((void *)&smb_mtime, stats.mtime, 8);

		smbtime2ps2time(smb_ctime, (ps2time_t *)&ctime);
		smbtime2ps2time(smb_atime, (ps2time_t *)&atime);
		smbtime2ps2time(smb_mtime, (ps2time_t *)&mtime);

		s64 hisize = stats.hisize;
		hisize = hisize << 32;
		s64 size = hisize | stats.size;

		scr_printf("\t size = %ld, mode = %04x\n", size, stats.mode);

		scr_printf("\t ctime = %04d.%02d.%02d %02d:%02d:%02d.%02d\n",
			ctime.year, ctime.month, ctime.day,
			ctime.hour, ctime.min,   ctime.sec, ctime.unused);
		scr_printf("\t atime = %04d.%02d.%02d %02d:%02d:%02d.%02d\n",
			atime.year, atime.month, atime.day,
			atime.hour, atime.min,   atime.sec, atime.unused);
		scr_printf("\t mtime = %04d.%02d.%02d %02d:%02d:%02d.%02d\n",
			mtime.year, mtime.month, mtime.day,
			mtime.hour, mtime.min,   mtime.sec, mtime.unused);
	}
	else
		scr_printf("Error %d\n", ret);


	// ----------------------------------------------------------------
	// create directory test:
	// ----------------------------------------------------------------
	scr_printf("\t IO mkdir... ");
	ret = mkdir("smb:\\created");
	if (ret == 0)
		scr_printf("OK  ");
	else
		scr_printf("Error %d  ", ret);


	// ----------------------------------------------------------------
	// rename file test:
	// ----------------------------------------------------------------
	/*
	scr_printf("\t IO rename... ");
	ret = fileXioRename("smb:\\rename_me\\rename_me.txt", "smb:\\rename_me\\renamed.txt");
	if (ret == 0)
		scr_printf("OK  ");
	else
		scr_printf("Error %d  ", ret);
	*/


	// ----------------------------------------------------------------
	// rename directory test:
	// ----------------------------------------------------------------
	scr_printf("IO rename... ");
	ret = fileXioRename("smb:\\created", "smb:\\renamed");
	if (ret == 0)
		scr_printf("OK  ");
	else
		scr_printf("Error %d  ", ret);


	// ----------------------------------------------------------------
	// delete file test:
	// ----------------------------------------------------------------
	/*
	scr_printf("IO remove... ");
	ret = remove("smb:\\delete_me\\delete_me.txt");
	if (ret == 0)
		scr_printf("OK  ");
	else
		scr_printf("Error %d  ", ret);
	*/


	// ----------------------------------------------------------------
	// delete directory test:
	// ----------------------------------------------------------------
	scr_printf("IO rmdir... ");
	ret = rmdir("smb:\\renamed");
	if (ret == 0)
		scr_printf("OK\n");
	else
		scr_printf("Error %d\n", ret);


	// ----------------------------------------------------------------
	// open file test:
	// ----------------------------------------------------------------

	int fd = fileXioOpen("smb:\\BFTP.iso", O_RDONLY, 0666);
	if (fd >= 0) {
		// 64bit filesize test
		s64 filesize = fileXioLseek64(fd, 0, SEEK_END);
		u8 *p = (u8 *)&filesize;
		scr_printf("\t filesize = ");
		for (i=0; i<8; i++) {
			scr_printf("%02X ", p[i]);
		}
		scr_printf("\n");

		// 64bit offset read test
		fileXioLseek64(fd, filesize - 2041, SEEK_SET);
		u8 buf[16];
		fileXioRead(fd, buf, 16);
		p = (u8 *)buf;
		scr_printf("\t read = ");
		for (i=0; i<16; i++) {
			scr_printf("%02X", p[i]);
		}
		scr_printf("\n");

		// 64bit write test
		//fileXioLseek64(fd, filesize - 16, SEEK_SET);
		//fileXioWrite(fd, "\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC", 16);
		//fileXioLseek64(fd, filesize - 16, SEEK_SET);
		//fileXioRead(fd, buf, 16);
		//p = (u8 *)buf;
		//scr_printf("\t read = ");
		//for (i=0; i<16; i++) {
		//	scr_printf("%02X", p[i]);
		//}
		//scr_printf("\n");

		fileXioClose(fd);
	}


	// ----------------------------------------------------------------
	// create file test:
	// ----------------------------------------------------------------

	fd = open("smb:\\testfile", O_RDWR | O_CREAT | O_TRUNC);
	if (fd >= 0) {
		write(fd, "test", 4);
		close(fd);
	}

	//while(1);


	// ----------------------------------------------------------------
	// how to close a share:
	// ----------------------------------------------------------------

	scr_printf("\t CLOSESHARE... ");
	ret = fileXioDevctl("smb:", SMB_DEVCTL_CLOSESHARE, NULL, 0, NULL, 0);
	if (ret == 0)
		scr_printf("OK\n");
	else
		scr_printf("Error %d\n", ret);


	// ----------------------------------------------------------------
	// how to LOGOFF from SMB server:
	// ----------------------------------------------------------------

	scr_printf("\t LOGOFF... ");
	ret = fileXioDevctl("smb:", SMB_DEVCTL_LOGOFF, NULL, 0, NULL, 0);
	if (ret == 0)
		scr_printf("OK  ");
	else
		scr_printf("Error %d  ", ret);


	// ----------------------------------------------------------------
	// how to close connection to SMB server:
	// ----------------------------------------------------------------

	scr_printf("DISCONNECT... ");
	ret = fileXioDevctl("smb:", SMB_DEVCTL_DISCONNECT, NULL, 0, NULL, 0);
	if (ret == 0)
		scr_printf("OK");
	else
		scr_printf("Error %d", ret);


   	SleepThread();
   	return 0;
}
