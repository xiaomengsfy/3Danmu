#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
typedef struct{int dummy;} C2D_Image;
static void C2D_DrawImageAt(C2D_Image a,float b,float c,float d,void*e,float f,float g){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;}
#define KEY_RIGHT (1u<<22)
#define KEY_LEFT (1u<<23)
#define KEY_X (1u<<24)
#include "../source/social_ui.c"
static int frame,writes,reads,people_reads,bottom;static const char*clicks[32];static u32 keys[32];static bool following=true;
static bool removed_button,people_hints,relation_hints;static int pink_mid;
bool aptMainLoop(void){return frame<25;}bool aptShouldClose(void){return false;}bool net_is_shutting_down(void){return false;}
void hidScanInput(void){frame++;}u32 hidKeysDown(void){return keys[frame]|KEY_TOUCH;}u32 hidKeysHeld(void){return KEY_TOUCH;}void hidTouchRead(touchPosition*t){t->px=1;t->py=1;}
void ui_begin(void){bottom=0;}void ui_begin_bottom(void){bottom=1;}void ui_end(void){}
void ui_text(float a,float b,float c,uint32_t d,const char*e){(void)a;(void)c;if(bottom&&b>=212&&d==UI_COL_DIM){if(strstr(e,"A")&&strstr(e,"B"))relation_hints=true;if(strstr(e,"L/R"))people_hints=true;}}
void ui_text_clipped(float a,float b,float c,uint32_t d,const char*e,float f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;}
float ui_text_width(const char*s,float scale){(void)scale;return strlen(s)*6;}
bool ui_button(float a,float b,float c,float d,const char*e,uint32_t f,bool g,float h,float i){(void)a;(void)c;(void)g;(void)h;(void)i;assert(b+d<=212);if(!strcmp(e,"上一页")||!strcmp(e,"下一页"))removed_button=true;if(f==UI_COL_ACCENT&&!strncmp(e,"UP",2))pink_mid=atoi(e+2);return clicks[frame]&&!strcmp(clicks[frame],e);}
void ui_printf(const char*a,...){(void)a;}
bool bili_logged_in(void){return true;}int64_t bili_account_id(void){return 1;}
BiliApiResult social_relation(int64_t mid,SocialRelation*r){assert(mid==42);reads++;*r=(SocialRelation){.known=true,.following=following};return (BiliApiResult){.outcome=BILI_API_SUCCESS};}
BiliApiResult social_relation_set(int64_t mid,bool f){assert(mid==42);writes++;following=f;return (BiliApiResult){.outcome=BILI_API_UNKNOWN};}
BiliApiResult social_people(int64_t a,bool b,int c,BiliProfile*d,int e,BiliPageInfo*f){assert(a==42&&e==20);(void)b;people_reads++;memset(f,0,sizeof(*f));f->count=20;f->has_more=c==1;for(int n=0;n<20;n++){d[n]=(BiliProfile){.mid=100+(c-1)*20+n};snprintf(d[n].name,sizeof d[n].name,"UP%d",(int)d[n].mid);}return (BiliApiResult){.outcome=BILI_API_SUCCESS};}
BiliApiResult social_dynamics(int64_t a,const char*b,bool c,SocialDynamic*d,int e,SocialPage*f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return (BiliApiResult){.outcome=BILI_API_UNKNOWN};}
const char *social_message_support_note(SocialMessageKind kind){(void)kind;return NULL;}
BiliApiResult social_messages(SocialMessageKind a,const char*b,SocialMessage*c,int d,SocialPage*e){(void)a;(void)b;(void)c;(void)d;(void)e;return (BiliApiResult){.outcome=BILI_API_UNKNOWN};}
void thumb_start(const BiliVideo*a,int b){(void)a;(void)b;}void thumb_stop(void){}void thumb_new_frame(int a){(void)a;}const C2D_Image*thumb_get(int a){(void)a;return NULL;}
static int runner(int(*fn)(void*),void*arg,const char*msg,bool cancel){(void)msg;int before=writes;int result=fn(arg);if(writes>before)assert(!cancel);return result;}
static void reset(void){frame=writes=reads=people_reads=bottom=pink_mid=0;removed_button=people_hints=relation_hints=false;following=true;memset(clicks,0,sizeof clicks);memset(keys,0,sizeof keys);keys[24]=KEY_B;}
int main(void){
 reset();clicks[1]="取消关注";clicks[2]="再次确认取消关注";clicks[3]="再次确认取消关注";SocialRelation r=social_relationship_ui(42,"UP",runner);assert(writes==1&&reads==2&&r.known&&!r.following);
 reset();keys[1]=KEY_A;keys[2]=KEY_B;r=social_relationship_ui(42,"UP",runner);assert(!writes&&r.following&&relation_hints);
 reset();keys[1]=KEY_A;keys[2]=KEY_A;keys[3]=KEY_A;keys[4]=KEY_B;r=social_relationship_ui(42,"UP",runner);assert(writes==1&&reads==3&&!r.following&&relation_hints);
 reset();following=false;keys[1]=KEY_A;keys[2]=KEY_A;keys[3]=KEY_B;r=social_relationship_ui(42,"UP",runner);assert(writes==1&&reads==3&&r.following);
 reset();keys[1]=KEY_R;keys[2]=KEY_DOWN;keys[3]=KEY_A;SocialSelection s=social_people_ui(42,false,runner);assert(s.kind==SOCIAL_UP&&s.profile.mid==105&&people_reads==1&&pink_mid==105&&people_hints&&!removed_button);
 reset();keys[1]=KEY_R;keys[2]=KEY_R;keys[3]=KEY_L;keys[4]=KEY_A;s=social_people_ui(42,true,runner);assert(s.kind==SOCIAL_UP&&s.profile.mid==104&&people_reads==1);
 reset();for(int n=1;n<=5;n++)keys[n]=KEY_R;keys[6]=KEY_A;s=social_people_ui(42,true,runner);assert(s.kind==SOCIAL_UP&&s.profile.mid==120&&people_reads==2);
 reset();for(int n=1;n<=5;n++)keys[n]=KEY_R;keys[6]=KEY_L;keys[7]=KEY_A;s=social_people_ui(42,true,runner);assert(s.kind==SOCIAL_UP&&s.profile.mid==116&&people_reads==3);
 puts("social UI relationship A/B, once + readback, cached-window focus/open PASS");return 0;}
