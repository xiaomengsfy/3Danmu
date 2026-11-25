#define __3DS__
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include "album_image.h"
static struct tm *localtime_r(const time_t*t,struct tm*out){struct tm*p=localtime(t);if(p)*out=*p;return p?out:NULL;}
static int opens,creates,frees,gpu_done,invalidated,complete,live;
static int fail_alloc,fail_create,fail_encode,write_mode,fail_close,fail_dir,fail_stat,fail_open,all_slots,fail_memory;
static int active_worker,fs_calls,closes;
static int pending_join_result=0x09401BFE,shutdown_waits;
static uint64_t now=10000;static unsigned char framebuffer[400*240*3];
static void (*run_worker)(void*);static void*worker_arg;
static void*checked_alloc(size_t n){if(fail_alloc){fail_alloc=0;return NULL;}size_t*p=malloc(n+32);assert(p);p[0]=n;p[1]=0x12345678;memset((char*)p+16+n,0x5a,16);live++;return(char*)p+16;}
static void checked_free(void*v){if(!v)return;size_t*p=(size_t*)((char*)v-16);assert(p[1]==0x12345678);for(size_t i=0;i<16;i++)assert(((unsigned char*)v)[p[0]+i]==0x5a);p[1]=0;live--;free(p);}
int fake_open(const char*p,int flags,...){(void)p;assert(active_worker&&(flags&O_EXCL));opens++;fs_calls++;if(fail_open){errno=EROFS;return -1;}return 1;}
int fake_stat(const char*p,struct stat*s){(void)s;assert(active_worker);fs_calls++;if(all_slots||strstr(p,"HNI_0002.MPO"))return 0;errno=fail_stat?EACCES:ENOENT;return -1;}
int fake_mkdir(const char*p,mode_t m){(void)p;(void)m;assert(active_worker);fs_calls++;if(fail_dir){errno=EACCES;return -1;}return 0;}
ssize_t fake_write(int fd,const void*p,size_t n){(void)fd;(void)p;assert(active_worker);fs_calls++;if(write_mode==1){errno=ENOSPC;return -1;}if(write_mode==2){write_mode=3;errno=EINTR;return -1;}return write_mode==3&&n>7?7:(ssize_t)n;}
int fake_close(int fd){(void)fd;assert(active_worker);closes++;if(fail_close){errno=EIO;return -1;}return 0;}
bool album_image_encode(const uint8_t*b,size_t n,const char*d,uint8_t**out,size_t*len){(void)b;(void)n;(void)d;if(fail_encode){*out=NULL;*len=0;return false;}*out=checked_alloc(32);assert(*out);(*out)[0]=255;(*out)[1]=216;(*out)[2]=255;(*out)[3]=217;*len=32;return true;}
bool album_image_encode_detailed(const uint8_t*b,size_t n,const char*d,uint8_t**out,size_t*len,AlbumImageError*error){*error=(AlbumImageError){fail_memory?ALBUM_IMAGE_MEMORY:ALBUM_IMAGE_CODEC,"test-encoder",fail_memory?-12:-22};return album_image_encode(b,n,d,out,len);}
#define stat(p,s) fake_stat(p,s)
#define open fake_open
#define mkdir fake_mkdir
#define write fake_write
#define close fake_close
#define malloc checked_alloc
#define free checked_free
#include "screenshot.c"
#undef malloc
#undef free
uint64_t osGetTime(void){return now;}
int gfxGetScreenFormat(int s){(void)s;return GSP_BGR8_OES;}
u8*gfxGetFramebuffer(int a,int b,u16*w,u16*h){(void)a;(void)b;*w=240;*h=400;return framebuffer;}
bool C3D_FrameBegin(unsigned f){(void)f;gpu_done=1;return true;}
void C3D_FrameEnd(unsigned f){(void)f;}
int GSPGPU_InvalidateDataCache(void*p,unsigned n){assert(gpu_done&&p==framebuffer&&n==400*240*3);invalidated++;return 0;}
Thread threadCreate(void(*fn)(void*),void*arg,unsigned stack,int pri,int core,bool detached){assert(stack>=64*1024);(void)pri;(void)core;assert(!detached);creates++;if(fail_create)return NULL;run_worker=fn;worker_arg=arg;return(void*)1;}
int threadJoin(Thread t,u64 timeout){
 (void)t;
 if(timeout && !complete){
  if(shutdown_waits++==0)return 0x09401BFE;
  active_worker=1;run_worker(worker_arg);active_worker=0;complete=1;
 }
 return complete?0:pending_join_result;
}
void svcSleepThread(long long n){(void)n;}
void threadFree(Thread t){(void)t;assert(complete);frees++;}
static void start(void){now+=2000;complete=0;int oldfs=fs_calls;screenshot_prepare();screenshot_finish();assert(fs_calls==oldfs);}
static void finish(void){assert(writer);active_worker=1;run_worker(worker_arg);active_worker=0;assert(screenshot_take_result()==0);complete=1;}
int main(int argc,char**argv){(void)argc;(void)argv;
 assert(R_SUCCEEDED(0x09401BFE));
 fail_encode=1;start();assert(screenshot_take_result()==0);assert(writer);assert(strstr(screenshot_status(),"正在保存"));finish();assert(screenshot_take_result()==-1);assert(strstr(screenshot_status(),"编码"));assert(!strstr(screenshot_status(),"SD"));assert(live==0);
 fail_memory=1;start();finish();assert(screenshot_take_result()==-1&&strstr(screenshot_status(),"内存")&&live==0);fail_memory=0;
 fail_encode=0;
 for(int i=0;i<40;i++){pending_join_result=i%3==0?0x09401BFE:i%3==1?1:-1;write_mode=i%4==1?1:i%4==2?2:0;fail_close=i%4==3;int oldclose=closes;start();int oldcreates=creates;screenshot_prepare();screenshot_finish();assert(creates==oldcreates);for(int poll=0;poll<10;poll++){assert(screenshot_take_result()==0);assert(writer);assert(strstr(screenshot_status(),"正在保存"));}finish();int result=screenshot_take_result();assert(result==((i%4==0||i%4==2)?1:-1));assert(strstr(screenshot_status(),result>0?"截图已保存到相册":"截图失败"));assert(screenshot_take_result()==0);assert(closes==oldclose+1&&live==0);}
 write_mode=0;fail_close=0;fail_create=1;start();assert(screenshot_take_result()==-1&&live==0&&!writer);fail_create=0;
 fail_alloc=1;start();assert(screenshot_take_result()==-1&&live==0&&!writer);
 fail_dir=1;start();finish();assert(screenshot_take_result()==-1&&live==0);fail_dir=0;
 fail_stat=1;start();finish();assert(screenshot_take_result()==-1&&live==0);fail_stat=0;
 fail_open=1;start();finish();assert(screenshot_take_result()==-1&&live==0);fail_open=0;
 all_slots=1;int oldopens=opens;start();finish();assert(screenshot_take_result()==-1&&live==0&&opens==oldopens);all_slots=0;
 start();screenshot_shutdown();assert(shutdown_waits==2&&!writer&&live==0&&screenshot_take_result()==1);
 puts("PASS positive-timeout/info/negative-error pending waits preserve worker and busy; capture40 repeated success/encoding/allocation/thread/write-EINTR/short-write/close/directory/stat/shutdown ownership");
}
