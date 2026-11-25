#include <assert.h>
#include <stdio.h>
#include <string.h>
#define KEY_START (1u<<12)
#include "../../source/ime.c"
#undef printf
static int frame,screen,pink[2],underline[2],tip,done_on_first;
bool aptMainLoop(void){if(frame==4)done_on_first++;return frame<6;}
void hidScanInput(void){frame++;}
u32 hidKeysDown(void){return frame<=2?KEY_TOUCH:frame>=4?KEY_START:0;}
u32 hidKeysHeld(void){return frame<=2?KEY_TOUCH:0;}
void hidTouchRead(touchPosition*t){t->px=frame==1?200:135;t->py=168;}
void ui_begin(void){screen=0;}void ui_begin_bottom(void){screen=1;}void ui_end(void){}
void ui_printf(const char *fmt,...){(void)fmt;}
float ui_text_width(const char*s,float scale){(void)scale;return (float)strlen(s)*6;}
float ui_text_height(float scale){(void)scale;return 16;}
static void text(uint32_t c,const char*s){if(!strcmp(s,"nv")&&c==UI_COL_ACCENT)pink[screen]++;if(!strcmp(s,"输入单字可能找不到候选字，可尝试组词输入。"))tip++;}
void ui_text(float x,float y,float s,uint32_t c,const char*t){(void)x;(void)y;(void)s;text(c,t);}
void ui_text_clipped(float x,float y,float s,uint32_t c,const char*t,float w){(void)x;(void)y;(void)s;(void)w;text(c,t);}
void ui_rect(float x,float y,float w,float h,uint32_t c){(void)x;(void)y;if(c==UI_COL_ACCENT&&h>=3&&w==12)underline[screen]++;}
bool ui_button(float x,float y,float w,float h,const char*l,uint32_t c,bool touched,float tx,float ty){(void)x;(void)y;(void)w;(void)h;(void)l;(void)c;(void)touched;(void)tx;(void)ty;return false;}
int main(void){
 static u8 blob[]={2,3,'n','u',0xe5,0xa5,0xb3};static u32 offsets[]={0};
 s_blob=blob;s_off=offsets;s_count=1;
 Cand c[CAND_MAX];assert(collect("nv",2,c)==1&&c[0].consume==2);
 char out[120]={0};assert(ime_input("输入","中",out,sizeof out));
 assert(!strcmp(out,"中nv")&&done_on_first==1);
 assert(pink[0]>0&&pink[1]>0&&underline[0]>0&&underline[1]>0&&tip>0);
 puts("IME real controller: v/u, first-Done commit, both-screen pink + thick underline PASS");
}
