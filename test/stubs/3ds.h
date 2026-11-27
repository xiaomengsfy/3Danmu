#ifndef TEST_3DS_H
#define TEST_3DS_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t Result;
typedef int LightLock;
typedef struct { int method; } httpcContext;
typedef enum { HTTPC_METHOD_GET, HTTPC_METHOD_POST } HTTPC_RequestMethod;
#define R_FAILED(x) ((x) < 0)
#define R_SUCCEEDED(x) ((x) >= 0)
#define HTTPC_RESULTCODE_DOWNLOADPENDING ((Result)0xd840a02b)
#define HTTPC_KEEPALIVE_ENABLED 1
#define HTTPC_KEEPALIVE_DISABLED 0
#define SSLCOPT_DisableVerify 1
void LightLock_Init(LightLock *);
void LightLock_Lock(LightLock *);
void LightLock_Unlock(LightLock *);
Result httpcInit(u32);
void httpcExit(void);
Result httpcOpenContext(httpcContext *, HTTPC_RequestMethod, const char *, u32);
Result httpcCloseContext(httpcContext *);
Result httpcCancelConnection(httpcContext *);
Result httpcSetSSLOpt(httpcContext *,u32);
Result httpcSetKeepAlive(httpcContext *,u32);
Result httpcAddRequestHeaderField(httpcContext *,const char *,const char *);
Result httpcAddPostDataRaw(httpcContext *,const u32 *,u32);
Result httpcBeginRequest(httpcContext *);
Result httpcGetResponseStatusCodeTimeout(httpcContext *,u32 *,u64);
Result httpcGetResponseHeader(httpcContext *,const char *,char *,u32);
Result httpcDownloadData(httpcContext *,u8 *,u32,u32 *);
Result httpcReceiveDataTimeout(httpcContext *,u8 *,u32,u64);
Result httpcGetDownloadSizeState(httpcContext *,u32 *,u32 *);
void *linearAlloc(size_t);
void linearFree(void *);
u64 osGetTime(void);
void ui_printf(const char *, ...);
void ui_trace(const char *, ...);
void ui_log_ascii(const char *,const char *,int);
typedef struct { unsigned short px,py; } touchPosition;
#ifndef KEY_LEFT
#define KEY_LEFT (1u<<8)
#endif
#ifndef KEY_RIGHT
#define KEY_RIGHT (1u<<9)
#endif
#define KEY_A (1u<<0)
#define KEY_B (1u<<1)
#define KEY_L (1u<<2)
#define KEY_R (1u<<3)
#define KEY_Y (1u<<4)
#define KEY_DOWN (1u<<6)
#define KEY_UP (1u<<7)
#define KEY_TOUCH (1u<<5)
bool aptMainLoop(void);
bool aptShouldClose(void);
void hidScanInput(void);
u32 hidKeysDown(void);
u32 hidKeysHeld(void);
u32 hidKeysUp(void);
void hidTouchRead(touchPosition *);
#endif
