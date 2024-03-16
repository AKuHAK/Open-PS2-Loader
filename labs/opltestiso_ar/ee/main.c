// ps2sdk include files
#include <debug.h>
#include <loadfile.h>
#include <smem.h>
#include <smod.h>
#include <sifcmd.h>
#include <sifrpc.h>
#include <iopheap.h>
#include <iopcontrol.h>
#include <kernel.h>
#include <sbv_patches.h>
#include <libcdvd-common.h>

// posix (newlib) include files
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <stdlib.h>

// This function is defined as weak in ps2sdkc, so how
// we are not using time zone, so we can safe some KB
void _ps2sdk_timezone_update() {}

DISABLE_PATCHED_FUNCTIONS(); // Disable the patched functionalities
// DISABLE_EXTRA_TIMERS_FUNCTIONS(); // Disable the extra functionalities for timers

// #define PRINTF printf
#define PRINTF scr_printf

// Blocks sizes to test
#define FILE_SIZE             (10 * 1024 * 1024)
#define FILE_BLOCK_SIZE_MIN   (2 * 1024)
#define FILE_BLOCK_SIZE_MAX   (256 * 1024)
#define STREAM_BLOCK_SIZE_MIN (2 * 1024)
#define STREAM_BLOCK_SIZE_MAX (32 * 1024)
#define STREAM_BUFMAX         80 // 80 sectors = 160KiB
#define STREAM_BANKMAX        5  // max 5 ringbuffers inside buffer ?

#define FILE_RANDOM "cdrom:\\RANDOM.BIN"
#define FILE_ZERO   "cdrom:\\ZERO.BIN"

//--------------------------------------------------------------
void print_speed(clock_t clk_start, clock_t clk_end, u32 fd_size, u32 lsn)
{
    unsigned int msec = (int)((clk_end - clk_start) / (CLOCKS_PER_SEC / 1000));
    PRINTF("\t\t- Read %04dKiB in %04dms, lsn=%08d, speed=%04dKB/s\n", fd_size / 1024, msec, lsn, fd_size / msec);
}

//--------------------------------------------------------------
void test_sceCdRead(uint32_t lsn, unsigned int block_size, unsigned int total_size, u8 spindlctrl)
{
    void *iopbuffer = SifAllocIopHeap(STREAM_BUFMAX * 2048);
    void *eebuffer = malloc(block_size);
    unsigned int sectors = block_size / 2048;
    unsigned int size_left = total_size;
    uint32_t cur_lsn = lsn;

    clock_t clk_start, clk_end, clk_diff, clk_1;
    sceCdRMode mode = {1, spindlctrl, SCECdSecS2048, 0};
    u32 error;

    int rv = sceCdDiskReady(0);
    if (rv < 0) {
        PRINTF("ERROR: sceCdDiskReady, rv=%d\n", rv);
    }

    sceCdSync(0);
    clk_diff = 0;
    clk_start = clock();
    while (size_left) {
        rv = sceCdRead(cur_lsn, sectors, eebuffer, &mode);
        // clk_1 = clock();
        if (rv == 0) {
            sceCdSync(0);
            error = sceCdGetError();
        }
        if (error != SCECdErNO) {
            PRINTF("\t\t- ERROR %d\n", error);
            break;
        }
        size_left -= sectors * 2048;
        cur_lsn += sectors;
        // clk_diff += clock() - clk_1;
    }
    clk_end = clock() - clk_diff;

    if (error == SCECdErNO)
        print_speed(clk_start, clk_end, total_size - size_left, lsn);

    free(eebuffer);
    SifFreeIopHeap(iopbuffer);
}

//--------------------------------------------------------------
void test_sceCdStRead(uint32_t lsn, unsigned int block_size, unsigned int total_size, u8 spindlctrl)
{
    void *iopbuffer = SifAllocIopHeap(STREAM_BUFMAX * 2048);
    void *eebuffer = malloc(block_size);
    unsigned int sectors = block_size / 2048;
    unsigned int size_left = total_size;

    clock_t clk_start, clk_end, clk_diff, clk_1;
    sceCdRMode mode = {1, spindlctrl, SCECdSecS2048, 0};
    u32 error;

    int rv = sceCdStInit(STREAM_BUFMAX, STREAM_BANKMAX, iopbuffer);
    if (rv == 0) {
        PRINTF("ERROR: sceCdStInit, rv=%d\n", rv);
    }

    sceCdStStart(lsn, &mode);
    clk_diff = 0;
    clk_start = clock();
    while (size_left) {
        rv = sceCdStRead(sectors, eebuffer, STMBLK, &error);
        // clk_1 = clock();
        if (error != SCECdErNO) {
            PRINTF("\t\t- ERROR %d\n", error);
            break;
        }
        if (rv != sectors) {
            PRINTF("\t\t- sceCdStRead = %d error = %d\n", rv, error);
            //    break;
        }
        size_left -= rv * 2048;
        // clk_diff += clock() - clk_1;
    }
    clk_end = clock() - clk_diff;
    sceCdStStop();

    if (error == SCECdErNO)
        print_speed(clk_start, clk_end, total_size - size_left, lsn);

    free(eebuffer);
    SifFreeIopHeap(iopbuffer);
}

//--------------------------------------------------------------
void print_header()
{
    PRINTF("\n\n\n");
    PRINTF("\t\tOPL Accurate Read tester v1.0\n");
}

//--------------------------------------------------------------
void print_done()
{
    int i;

    PRINTF("\t\tDone. Next test in ");
    for (i = 6; i > 0; i--) {
        PRINTF("%d ", i);
        sleep(1);
    }
}

//--------------------------------------------------------------
int main()
{
    init_scr();
    scr_clear();
    print_header();

    SifExitIopHeap();
    SifLoadFileExit();
    SifExitRpc();

    SifInitRpc(0);
    // while (!SifIopReset("rom0:UDNL cdrom0:\\MODULES\\IOPRP271.IMG;1", 0))
    while (!SifIopReset("rom0:UDNL", 0))
        ;
    while (!SifIopSync())
        ;

    SifInitRpc(0);
    SifLoadFileInit();
    SifInitIopHeap();

    sceCdInit(SCECdINIT);
    int disktype = sceCdGetDiskType();
    int sector_step = 32768;
    if ((disktype == SCECdPS2DVD) || (disktype == SCECdDVDV)) {
        PRINTF("\t\tDVD detected\n");
        sector_step = 262144;
    } else
        PRINTF("\t\tCD detected\n");

    // speed test sectors
    // PRINTF("\t\tStreaming from 10 places located at 0 to 100%% of DVD:\n");
    u8 spindlctrl;
    spindlctrl = 0;
    PRINTF("\t\test sceCdRead\n");
    for (int i = 0; i < 10; i++) {
        test_sceCdRead(i * sector_step, 16 * 1024, FILE_SIZE, spindlctrl);
    }

    PRINTF("\t\test sceCdStRead\n");
    for (int i = 0; i < 10; i++) {
        test_sceCdStRead(i * sector_step, 16 * 1024, FILE_SIZE, spindlctrl);
    }

    // for (spindlctrl = 0; spindlctrl < 2; spindlctrl++) {
    //     PRINTF("\t\tspindlctrl = %d\n", spindlctrl);
    //     for (int i = 0; i < 10; i++) {
    //         test_sceCdRead(i * sector_step, 16 * 1024, FILE_SIZE, spindlctrl);
    //     }
    // }
    print_done();

    while (1) {}
    return 0;
}
