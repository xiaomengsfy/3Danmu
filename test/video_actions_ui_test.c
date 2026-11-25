#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
typedef struct{int dummy;} C2D_Image;
static void C2D_DrawImageAt(C2D_Image a,float b,float c,float d,void*e,float f,float g){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;}
#define KEY_RIGHT (1u<<22)
#define KEY_LEFT (1u<<23)
#define KEY_X (1u<<24)
#include "../source/video_actions_ui.c"
static int frame,writes,reads,like_writes;static bool keyboard_like,state_fixture;static const char*clicks[12];
bool aptMainLoop(void){return frame<9;}bool aptShouldClose(void){return false;}bool net_is_shutting_down(void){return false;}
void hidScanInput(void){frame++;}u32 hidKeysDown(void){return keyboard_like?(frame==1?KEY_A:KEY_B):(frame==8?KEY_B:KEY_TOUCH);}u32 hidKeysHeld(void){return KEY_TOUCH;}void hidTouchRead(touchPosition*t){t->px=1;t->py=1;}
void ui_begin(void){}void ui_begin_bottom(void){}void ui_end(void){}
void ui_text(float a,float b,float c,uint32_t d,const char*e){(void)a;(void)b;(void)c;(void)d;(void)e;}
void ui_text_clipped(float a,float b,float c,uint32_t d,const char*e,float f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;}
float ui_text_width(const char*s,float scale){(void)scale;return strlen(s)*6;}
bool ui_button(float a,float b,float c,float d,const char*e,uint32_t f,bool g,float h,float i){(void)a;(void)c;(void)f;(void)g;(void)h;(void)i;assert(b+d<=212);return clicks[frame]&&!strcmp(clicks[frame],e);}
void ui_printf(const char*a,...){(void)a;}
bool bili_logged_in(void){return true;}int64_t bili_account_id(void){return 1;}
BiliApiResult video_coins(int64_t aid,int*out){assert(aid==99);reads++;*out=state_fixture?1:writes?2:0;return (BiliApiResult){.outcome=BILI_API_SUCCESS};}
BiliApiResult video_coin_once(int64_t aid,int count,bool like){assert(aid==99&&count==2&&like);writes++;return (BiliApiResult){.outcome=BILI_API_UNKNOWN};}
BiliApiResult video_folders(int64_t a,BiliFavFolder*b,int c,BiliPageInfo*d){(void)a;(void)c;if(state_fixture){memset(d,0,sizeof(*d));d->count=1;b[0]=(BiliFavFolder){.id=3,.contains=true,.membership_known=true};return (BiliApiResult){.outcome=BILI_API_SUCCESS};}(void)b;(void)d;return (BiliApiResult){.outcome=BILI_API_UNKNOWN};}
BiliApiResult video_favorite_delta_once(int64_t a,const BiliFavFolder*b,const bool*c,int d){(void)a;(void)b;(void)c;(void)d;return (BiliApiResult){.outcome=BILI_API_UNKNOWN};}
BiliApiResult bili_like_status(int64_t a,BiliLikeState*b){(void)a;*b=state_fixture?BILI_LIKE_CONFIRMED:BILI_LIKE_UNKNOWN;return (BiliApiResult){.outcome=state_fixture?BILI_API_SUCCESS:BILI_API_UNKNOWN};}
BiliApiResult bili_like_set(int64_t a,bool b){(void)a;(void)b;like_writes++;return (BiliApiResult){.outcome=BILI_API_UNKNOWN};}
static int runner(int(*fn)(void*),void*arg,const char*msg,bool cancel){(void)msg;int before=writes;int result=fn(arg);if(writes>before)assert(!cancel);return result;}
int main(void){clicks[1]="选择2枚";clicks[2]="切换附加点赞";clicks[3]="确认投币数量";clicks[4]="再次确认投币";clicks[5]="再次确认投币";ActionJob j={.aid=99,.account=1};coin_ui(&j,runner);assert(writes==1&&reads==2);puts("coin UI once + readback PASS");frame=0;keyboard_like=true;ActionJob l={.aid=99,.account=1};like_ui(&l,runner);assert(like_writes==1);puts("Like keyboard A confirms exactly once, B exits PASS");state_fixture=true;VideoAccountState state;BiliVideo v={.aid=99};video_account_state(&v,runner,&state);assert(state.liked==BILI_LIKE_CONFIRMED&&state.coins==1&&state.favorite&&state.favorite_known);puts("Confirmed interaction state from readbacks PASS");return 0;}
