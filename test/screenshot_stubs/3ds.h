#include <stdint.h>
#include <stdbool.h>
typedef uint64_t u64;typedef uint16_t u16;typedef unsigned char u8;
typedef void *Thread;
#define GFX_TOP 0
#define GFX_LEFT 0
#define GSP_BGR8_OES 1
#define R_SUCCEEDED(x) ((x)>=0)
#define R_FAILED(x) ((x)<0)
uint64_t osGetTime(void);int gfxGetScreenFormat(int);u8 *gfxGetFramebuffer(int,int,u16*,u16*);
int GSPGPU_InvalidateDataCache(void*,unsigned);
Thread threadCreate(void (*)(void*),void*,unsigned,int,int,bool);
int threadJoin(Thread,u64);void threadFree(Thread);

void svcSleepThread(long long);
