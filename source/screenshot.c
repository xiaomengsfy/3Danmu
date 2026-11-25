#include "screenshot.h"
#include "numeric_text.h"
#include <string.h>
static void le32(uint8_t *p,uint32_t n) { for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i)); }
bool screenshot_bmp(const uint8_t *fb,unsigned w,unsigned h,uint8_t *out,size_t cap) {
 const size_t size=54+400*240*3;
 if(!fb || !out || w!=400 || h!=240 || cap<size)return false;
 memset(out,0,54);out[0]='B';out[1]='M';le32(out+2,(uint32_t)size);
 le32(out+10,54);le32(out+14,40);le32(out+18,w);le32(out+22,h);
 out[26]=1;out[28]=24;le32(out+34,400*240*3);
 /* 3DS column-major from bottom to top; BMP rows bottom to top. */
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)
  memcpy(out+54+(y*w+x)*3,fb+(x*h+y)*3,3);
 return true;
}
#ifdef __3DS__
#include <3ds.h>
#include <citro3d.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#include "album_image.h"
static uint8_t *pending_fb;
static uint64_t last_capture;
static bool captured;
static Thread writer;
static uint8_t *writer_bmp;
static uint64_t writer_stamp;
static char writer_date[20];
/* Worker result is consumed only after a successful join, never raced. */
static int writer_result,feedback_event;
static const char *writer_message;
static uint64_t feedback_until;
static const char *feedback_text="";
static void screenshot_writer(void *unused);
static void feedback(int result,const char *text) {
 feedback_event=result;feedback_text=text;feedback_until=osGetTime()+3500;
}
static void screenshot_reap(void) {
 /* A wait timeout is an informational (nonnegative) Result on 3DS.
  * Only zero confirms completion; retain ownership and busy state otherwise. */
 if(writer && threadJoin(writer,0)==0) {
  threadFree(writer);writer=NULL;
  feedback(writer_result,writer_message);
 }
}
const char *screenshot_status(void) {
 screenshot_reap();
 if(writer)return "正在保存截图…";
 return osGetTime()<feedback_until?feedback_text:"";
}
int screenshot_take_result(void) {
 screenshot_reap();int result=feedback_event;feedback_event=0;return result;
}
void screenshot_shutdown(void) {
 pending_fb=NULL;
 if(writer) {
  while(threadJoin(writer,UINT64_MAX)!=0)svcSleepThread(1000000);
  threadFree(writer);writer=NULL;
  feedback(writer_result,writer_message);
 }
}
void screenshot_prepare(void) {
 screenshot_reap();uint64_t now=osGetTime();
 if(writer || pending_fb || (captured && now-last_capture<1000))return;
 if(gfxGetScreenFormat(GFX_TOP)!=GSP_BGR8_OES) {
  feedback(-1,"截图失败：不支持当前画面格式");return;
 }
 u16 w,h;uint8_t *fb=gfxGetFramebuffer(GFX_TOP,GFX_LEFT,&w,&h);
 if(!fb || w!=240 || h!=400) {feedback(-1,"截图失败：画面尺寸异常");return;}
 pending_fb=fb;last_capture=now;captured=true;
}
void screenshot_finish(void) {
 if(!pending_fb)return;
 uint8_t *fb=pending_fb;pending_fb=NULL;
 const size_t size=54+400*240*3;uint8_t *bmp=malloc(size);
 if(!bmp){printf("Screenshot: stage=allocate-snapshot bytes=%u\n",(unsigned)size);feedback(-1,"截图失败：可用内存不足");return;}
 bool ok=C3D_FrameBegin(0);
 if(ok) {
  ok=R_SUCCEEDED(GSPGPU_InvalidateDataCache(fb,400*240*3));
  if(ok)ok=screenshot_bmp(fb,400,240,bmp,size);
  C3D_FrameEnd(0);
 }
 if(!ok){free(bmp);feedback(-1,"截图失败：未能读取画面");return;}
 writer_bmp=bmp;writer_stamp=last_capture;writer_result=-1;
 writer_message="截图失败：JPEG编码失败";
 writer_date[0]=0;time_t now=time(NULL);struct tm calendar;
 if(localtime_r(&now,&calendar)&&strftime(writer_date,sizeof writer_date,"%Y:%m:%d %H:%M:%S",&calendar)!=19)writer_date[0]=0;
 writer=threadCreate(screenshot_writer,NULL,128*1024,0x31,-2,false);
 if(!writer){free(writer_bmp);writer_bmp=NULL;printf("Screenshot: stage=thread-create stack=%u\n",128*1024u);feedback(-1,"截图失败：无法启动保存任务");}
}
/* Error text and details are worker-owned until the successful join. */
static bool save_failure(const char *message,const char *stage,int code,const char *path) {
 writer_message=message;
 printf("Screenshot: stage=%s code=%d path=%s\n",stage,code,path?path:"(none)");
 return false;
}
static bool write_all(int fd,const uint8_t *data,size_t size,const char *path) {
 size_t done=0;int error=0;
 while(done<size) {
  ssize_t n=write(fd,data+done,size-done);
  if(n<0&&errno==EINTR)continue;
  if(n<=0){error=n<0?errno:EIO;break;}
  done+=(size_t)n;
 }
 int result=close(fd),close_error=result<0?errno:0;
 if(error)return save_failure("截图失败：SD卡写入失败","write",error,path);
 if(result<0)return save_failure("截图失败：SD卡关闭文件失败","close",close_error,path);
 return true;
}
static bool directory(const char *path) {
 if(mkdir(path,0777)==0||errno==EEXIST)return true;
 return save_failure("截图失败：无法创建相册目录","mkdir",errno,path);
}
static bool album_save(const uint8_t *jpeg,size_t size,char *path,size_t capacity) {
 if(!directory("sdmc:/DCIM")||!directory("sdmc:/DCIM/100NIN03"))return false;
 const char *extensions[]={"JPG","jpg","MPO","mpo"};
 unsigned first=(unsigned)(writer_stamp%9999)+1;
 for(unsigned attempt=0;attempt<256;attempt++) {
  unsigned slot=(first-1+attempt)%9999+1;bool occupied=false;
  for(unsigned e=0;e<4;e++) {
   struct stat existing;snprintf(path,capacity,"sdmc:/DCIM/100NIN03/HNI_%04u.%s",slot,extensions[e]);
   if(stat(path,&existing)==0){occupied=true;break;}
   if(errno!=ENOENT)return save_failure("截图失败：无法读取相册目录","stat",errno,path);
  }
  if(occupied)continue;
  snprintf(path,capacity,"sdmc:/DCIM/100NIN03/HNI_%04u.JPG",slot);
  int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0666);
  if(fd<0){if(errno==EEXIST)continue;return save_failure("截图失败：无法创建相册文件","open",errno,path);}
  if(write_all(fd,jpeg,size,path))return true;
  printf("Screenshot: partial DCIM JPEG retained: %s\n",path);return false;
 }
 return save_failure("截图失败：相册文件名已占满","slots",EEXIST,NULL);
}
static void screenshot_writer(void *unused) {
 (void)unused;uint8_t *bmp=writer_bmp,*jpeg=NULL;size_t jpeg_size=0;char path[128];
 AlbumImageError error;
 bool encoded=album_image_encode_detailed(bmp,54+400*240*3,writer_date[0]?writer_date:NULL,&jpeg,&jpeg_size,&error);
 if(!encoded) {
  writer_message=error.kind==ALBUM_IMAGE_MEMORY?"截图失败：编码内存不足":"截图失败：JPEG编码失败";
  printf("Screenshot encode failed: kind=%d stage=%s code=%d\n",error.kind,error.stage,error.code);
 }
 bool saved=encoded&&album_save(jpeg,jpeg_size,path,sizeof path);
 free(jpeg);free(bmp);writer_bmp=NULL;writer_result=saved?1:-1;
 if(saved){writer_message="截图已保存到相册";printf("Screenshot JPEG saved: %s\n",path);}
 else printf("Screenshot JPEG %s; no fallback file created\n",encoded?"write failed":"encoding failed");
}
#endif
