#include "album_image.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
static void put16(uint8_t*p,unsigned n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void put32(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static uint32_t get32(const uint8_t*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
bool album_image_canvas(const uint8_t*bmp,size_t length,uint8_t*bgr,size_t cap){
 if(!bmp||!bgr||length<54+400*240*3||cap<640*480*3||bmp[0]!='B'||bmp[1]!='M'||get32(bmp+10)!=54||get32(bmp+18)!=400||get32(bmp+22)!=240||bmp[28]!=24||get32(bmp+30)!=0)return false;
 memset(bgr,0,640*480*3);
 for(unsigned y=0;y<384;y++)for(unsigned x=0;x<640;x++)
  memcpy(bgr+((y+48)*640+x)*3,bmp+54+((239-y*240/384)*400+x*400/640)*3,3);
 return true;
}
static void entry(uint8_t*t,int index,unsigned tag,unsigned type,uint32_t count,uint32_t value){uint8_t*p=t+10+index*12;put16(p,tag);put16(p+2,type);put32(p+4,count);put32(p+8,value);}
static bool date_valid(const char*s){if(!s)return true;if(strlen(s)!=19)return false;for(int i=0;i<19;i++){char required=i==4||i==7?':':i==10?' ':i==13||i==16?':':0;if(required?s[i]!=required:s[i]<'0'||s[i]>'9')return false;}return true;}
bool album_image_exif(const uint8_t*jpeg,size_t len,const char*date,const uint8_t*thumb,size_t tn,uint8_t**out,size_t*out_len){
 if(out)*out=NULL;
 if(out_len)*out_len=0;
 if(!out||!out_len||!jpeg||len<4||jpeg[0]!=255||jpeg[1]!=216||!date_valid(date)||tn>60000||(tn&&(!thumb||tn<4||thumb[0]!=255||thumb[1]!=216)))return false;
 /* TIFF offsets are relative to II header. IFD0 six standard tags, IFD1
  * three thumbnail tags; strings outside the IFD use even byte offsets. */
 const unsigned count=date?6:5,ifd0_end=8+2+count*12+4;
 unsigned offset=ifd0_end;const char*strings[]={"Nintendo","Nintendo 3DS","3Danmu Kina",date};unsigned offsets[4]={0};
 for(int i=0;i<(date?4:3);i++){offsets[i]=offset;offset+=(unsigned)strlen(strings[i])+1;if(offset&1)offset++;}
 unsigned ifd1=tn?offset:0;if(tn)offset+=2+3*12+4;
 unsigned thumb_offset=offset;size_t tiff_size=(size_t)offset+tn,segment=2+6+tiff_size;
 if(segment>65535||len>SIZE_MAX-segment-2)return false;
 size_t total=len+segment+2;uint8_t*r=malloc(total);if(!r)return false;
 r[0]=255;r[1]=216;r[2]=255;r[3]=225;r[4]=(uint8_t)(segment>>8);r[5]=(uint8_t)segment;memcpy(r+6,"Exif\0\0",6);
 uint8_t*t=r+12;memset(t,0,tiff_size);t[0]='I';t[1]='I';put16(t+2,42);put32(t+4,8);put16(t+8,count);
 entry(t,0,0x010e,2,12,offsets[2]);entry(t,1,0x010f,2,9,offsets[0]);entry(t,2,0x0110,2,13,offsets[1]);entry(t,3,0x0112,3,1,1);entry(t,4,0x0131,2,12,offsets[2]);
 /* ImageDescription clearly identifies application origin; no MakerNote. */
 if(date)entry(t,5,0x0132,2,20,offsets[3]);
 put32(t+10+count*12,ifd1);
 for(int i=0;i<(date?4:3);i++)memcpy(t+offsets[i],strings[i],strlen(strings[i])+1);
 if(tn){put16(t+ifd1,3);uint8_t*p=t+ifd1+2;put16(p,0x0103);put16(p+2,3);put32(p+4,1);put32(p+8,6);p+=12;put16(p,0x0201);put16(p+2,4);put32(p+4,1);put32(p+8,thumb_offset);p+=12;put16(p,0x0202);put16(p+2,4);put32(p+4,1);put32(p+8,(uint32_t)tn);memcpy(t+thumb_offset,thumb,tn);}
 memcpy(r+4+segment,jpeg+2,len-2);*out=r;*out_len=total;return true;
}
#ifdef __3DS__
#include <stdio.h>
#include <errno.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
static void encode_error(AlbumImageError *error,const char *stage,int code) {
 if(error){error->kind=code==AVERROR(ENOMEM)?ALBUM_IMAGE_MEMORY:ALBUM_IMAGE_CODEC;
  error->stage=stage;error->code=code;}
 printf("Screenshot encoder: stage=%s code=%d\n",stage,code);
}
static bool encode_bmp(const uint8_t *bmp,int w,int h,uint8_t **out,size_t *length,AlbumImageError *error) {
 bool ok=false;int result;*out=NULL;*length=0;
 const AVCodec *codec=avcodec_find_encoder(AV_CODEC_ID_MJPEG);
 if(!codec){encode_error(error,"find-mjpeg",AVERROR_ENCODER_NOT_FOUND);return false;}
 AVCodecContext *ctx=avcodec_alloc_context3(codec);AVFrame *frame=av_frame_alloc();
 AVPacket *pkt=av_packet_alloc();struct SwsContext *sws=NULL;
 if(!ctx||!frame||!pkt){encode_error(error,"allocate-codec",AVERROR(ENOMEM));goto done;}
 ctx->width=w;ctx->height=h;ctx->time_base=(AVRational){1,1};ctx->pix_fmt=AV_PIX_FMT_YUVJ420P;
 ctx->color_range=AVCOL_RANGE_JPEG;ctx->flags|=AV_CODEC_FLAG_QSCALE;
 ctx->global_quality=3*FF_QP2LAMBDA;ctx->thread_count=1;ctx->thread_type=0;
 /* Default optimal Huffman needs a1,843,200-byte table at640x480 and a
  * >51KiB nested ARM stack path. Standard tables avoid both costs. */
 result=av_opt_set(ctx->priv_data,"huffman","default",0);
 if(result<0){encode_error(error,"huffman-option",result);goto done;}
 result=avcodec_open2(ctx,codec,NULL);
 if(result<0){encode_error(error,"open-mjpeg",result);goto done;}
 frame->format=ctx->pix_fmt;frame->width=w;frame->height=h;
 frame->quality=ctx->global_quality;frame->color_range=AVCOL_RANGE_JPEG;
 result=av_frame_get_buffer(frame,32);
 if(result<0){encode_error(error,"allocate-frame",result);goto done;}
 /* Full-range YUV black. Scale directly from bottom-up400x240 BMP into
  * the centered640x384 (or160x96) content, leaving48 (or12) black rows. */
 for(int p=0;p<3;p++)memset(frame->data[p],p?128:0,(size_t)frame->linesize[p]*(p?h/2:h));
 int content=h*4/5,border=(h-content)/2;
 sws=sws_getContext(400,240,AV_PIX_FMT_BGR24,w,content,ctx->pix_fmt,SWS_BILINEAR,NULL,NULL,NULL);
 if(!sws){encode_error(error,"allocate-scaler",AVERROR(ENOMEM));goto done;}
 const uint8_t *src[]={bmp+54+239*400*3,NULL,NULL,NULL};int strides[]={-400*3,0,0,0};
 uint8_t *dst[]={frame->data[0]+border*frame->linesize[0],
  frame->data[1]+border/2*frame->linesize[1],frame->data[2]+border/2*frame->linesize[2],NULL};
 result=sws_scale(sws,src,strides,0,240,dst,frame->linesize);
 if(result!=content){encode_error(error,"scale-frame",result<0?result:AVERROR(EINVAL));goto done;}
 result=avcodec_send_frame(ctx,frame);
 if(result<0){encode_error(error,"send-frame",result);goto done;}
 result=avcodec_receive_packet(ctx,pkt);
 if(result<0||pkt->size<4){encode_error(error,"receive-packet",result<0?result:AVERROR(EINVAL));goto done;}
 *out=malloc((size_t)pkt->size);
 if(!*out){encode_error(error,"copy-packet",AVERROR(ENOMEM));goto done;}
 memcpy(*out,pkt->data,(size_t)pkt->size);*length=(size_t)pkt->size;ok=true;
 done:sws_freeContext(sws);av_packet_free(&pkt);av_frame_free(&frame);avcodec_free_context(&ctx);return ok;
}
bool album_image_encode_detailed(const uint8_t *bmp,size_t n,const char *date,uint8_t **out,size_t *out_len,AlbumImageError *error) {
 if(out)*out=NULL;
 if(out_len)*out_len=0;
 if(error)*error=(AlbumImageError){ALBUM_IMAGE_OK,"none",0};
 if(!out||!out_len||!bmp||n<54+400*240*3||bmp[0]!='B'||bmp[1]!='M'||get32(bmp+10)!=54||get32(bmp+18)!=400||get32(bmp+22)!=240||bmp[28]!=24||get32(bmp+30)!=0||!date_valid(date)){
  if(error)*error=(AlbumImageError){ALBUM_IMAGE_INPUT,"input",AVERROR(EINVAL)};
  return false;
 }
 uint8_t *jpeg=NULL,*thumb=NULL;size_t jn=0,tn=0;bool ok=false;
 if(encode_bmp(bmp,640,480,&jpeg,&jn,error)) {
  /* Thumbnail is optional; failure must not discard a valid full image. */
  AlbumImageError thumbnail_error;
  encode_bmp(bmp,160,120,&thumb,&tn,&thumbnail_error);
  ok=album_image_exif(jpeg,jn,date,thumb,tn,out,out_len);
  if(!ok)encode_error(error,"allocate-exif",AVERROR(ENOMEM));
 }
 free(jpeg);free(thumb);return ok;
}
bool album_image_encode(const uint8_t *bmp,size_t n,const char *date,uint8_t **out,size_t *out_len) {
 return album_image_encode_detailed(bmp,n,date,out,out_len,NULL);
}
#endif
