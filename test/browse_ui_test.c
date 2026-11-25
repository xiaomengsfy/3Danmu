/* Execute real modal controllers with scripted input/render boundaries. No
 * device, network, credentials or live account. Screen pixels remain unverified. */
#include <stdio.h>
#include <string.h>
#include "browse_ui.h"
#include "ui.h"
#undef printf
static int checks,failures,frame,read_count,write_count,run_count;
static bool logged=true,contains=true,cancel_read,shutdown_flag,close_after_read;
static int draws_after_close,search_count,remove_count;
static int64_t account=123;
static bool change_account_after_read,resolve_write=true,saw_removed,saw_remove_button;
static const char *buttons[40];static u32 keys[40];
static bool removed_button,search_hints;static int bottom;
static BiliApiOutcome write_outcome,read_outcome,readback_outcome;
static bool saw_once,saw_confirmed,saw_unknown;
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)
bool aptMainLoop(void){return frame<30;}
bool aptShouldClose(void){return shutdown_flag;}
bool net_is_shutting_down(void){return shutdown_flag;}
void hidScanInput(void){frame++;}
u32 hidKeysDown(void){return keys[frame]|KEY_TOUCH;}
u32 hidKeysHeld(void){return KEY_TOUCH;}
u32 hidKeysUp(void){return 0;}
void hidTouchRead(touchPosition *t){t->px=10;t->py=40;}
void ui_begin(void){bottom=0;if(shutdown_flag)draws_after_close++;} void ui_begin_bottom(void){bottom=1;} void ui_end(void){}
void ui_list_reset(void){}
void ui_text(float x,float y,float s,uint32_t c,const char *text){(void)x;(void)s;if(bottom&&y>=212&&c==UI_COL_DIM&&strstr(text,"A")&&strstr(text,"B")&&strstr(text,"L/R"))search_hints=true;}
void ui_text_clipped(float x,float y,float s,uint32_t c,const char *text,float w){
    (void)x;(void)y;(void)s;(void)c;(void)w;
    if(strstr(text,"单次"))saw_once=true;
    if(strstr(text,"已在稍后再看"))saw_confirmed=true;
    if(strstr(text,"未确认"))saw_unknown=true;
    if(strstr(text,"已取消稍后再看"))saw_removed=true;
}
bool ui_button(float x,float y,float w,float h,const char *label,uint32_t bg,bool t,float tx,float ty){
    (void)x;(void)y;(void)w;(void)bg;(void)t;(void)tx;(void)ty;
    CHECK(y+h<=UI_TOUCH_BOTTOM);
    if(!strcmp(label,"取消稍后再看")){saw_remove_button=true;CHECK(bg==UI_COL_ACCENT);}
    if(!strcmp(label,"向上")||!strcmp(label,"向下")||!strcmp(label,"上一页")||!strcmp(label,"下一页")||!strcmp(label,"消息")||strstr(label,"动态"))removed_button=true;
    return buttons[frame] && !strcmp(label,buttons[frame]);
}
bool bili_logged_in(void){return logged;}
int64_t bili_account_id(void){return logged?account:0;}
const char *bili_last_error(void){return "synthetic failure";}
BiliApiResult bili_search_users(const char *k,int page,BiliProfile *out,int max,BiliPageInfo *info){
    (void)k;(void)max;read_count++;memset(out,0,20*sizeof(*out));memset(info,0,sizeof(*info));
    info->count=search_count;info->total=21;info->has_more=page==1;
    for(int i=0;i<search_count;i++){out[i].mid=42+(page-1)*20+i;snprintf(out[i].name,sizeof(out[i].name),i?"UP%d":"UP",i+1);}
    return (BiliApiResult){.outcome=read_outcome};
}
BiliApiResult bili_watchlater_add(int64_t aid){CHECK(aid==99);write_count++;if(resolve_write)contains=true;return (BiliApiResult){.outcome=write_outcome};}
BiliApiResult bili_watchlater_remove(int64_t aid){CHECK(aid==99);write_count++;remove_count++;if(resolve_write)contains=false;return (BiliApiResult){.outcome=write_outcome};}
BiliApiResult bili_watchlater_contains(int64_t aid,bool *found){CHECK(aid==99);read_count++;*found=contains;return (BiliApiResult){.outcome=write_count?readback_outcome:read_outcome};}
static int run(int(*fn)(void*),void *arg,const char *m,bool cancellable){
    (void)m;run_count++;int before=write_count;int r=fn(arg);if(write_count>before)CHECK(!cancellable);
    if(cancellable && change_account_after_read)account++;
    if(cancellable && close_after_read)shutdown_flag=true;
    return cancellable && cancel_read?-1:r;
}
static bool login(void){logged=true;return true;}
static void reset(void){
    frame=read_count=write_count=run_count=draws_after_close=0;search_count=1;close_after_read=false;logged=true;contains=true;cancel_read=false;shutdown_flag=false;
    saw_once=saw_confirmed=saw_unknown=false;write_outcome=BILI_API_SUCCESS;read_outcome=readback_outcome=BILI_API_SUCCESS;
    removed_button=search_hints=false;remove_count=0;account=123;change_account_after_read=false;resolve_write=true;saw_removed=saw_remove_button=false;
    memset(buttons,0,sizeof(buttons));memset(keys,0,sizeof(keys));keys[9]=KEY_B;
}
int main(void){
    BiliVideo v={.aid=99};strcpy(v.title,"Video");BiliProfile p;
    reset();buttons[1]="UP主";CHECK(browse_search_kind()==1);
    reset();buttons[1]="番剧";CHECK(browse_search_kind()==2);
    reset();keys[1]=KEY_B;CHECK(browse_search_kind()==-1);
    reset();keys[1]=KEY_R;keys[2]=KEY_A;
    CHECK(browse_users("name",run,&p) && p.mid==62 && read_count==2);
    reset();search_count=20;keys[1]=KEY_R;keys[2]=KEY_A;
    CHECK(browse_users("name",run,&p) && p.mid==46 && read_count==1 && search_hints && !removed_button);
    reset();search_count=20;keys[1]=KEY_R;keys[2]=KEY_R;keys[3]=KEY_L;keys[4]=KEY_DOWN;keys[5]=KEY_A;
    CHECK(browse_users("name",run,&p) && p.mid==47 && read_count==1);
    reset();search_count=20;for(int i=1;i<=5;i++)keys[i]=KEY_R;keys[6]=KEY_A;
    CHECK(browse_users("name",run,&p) && p.mid==62 && read_count==2);
    reset();search_count=20;for(int i=1;i<=5;i++)keys[i]=KEY_R;keys[6]=KEY_L;keys[7]=KEY_A;
    CHECK(browse_users("name",run,&p) && p.mid==58 && read_count==3);
    reset();close_after_read=true;buttons[1]="UP";
    CHECK(!browse_users("name",run,&p) && !draws_after_close && !p.mid);
    reset();cancel_read=true;buttons[1]="UP";
    CHECK(!browse_users("name",run,&p) && !p.mid);
    reset();buttons[1]="我的投稿";CHECK(browse_profile(&p,true,"")==BROWSE_SUBMISSIONS);
    reset();buttons[1]="我的关注";CHECK(browse_profile(&p,true,"")==BROWSE_FOLLOWING);
    reset();buttons[1]="关注的动态";keys[2]=KEY_B;CHECK(browse_profile(&p,true,"")==BROWSE_BACK && !removed_button);
    reset();buttons[1]="稍后再看";CHECK(browse_profile(&p,true,"")==BROWSE_WATCHLATER);
    reset();buttons[1]="查看投稿";CHECK(browse_profile(&p,false,"")==BROWSE_SUBMISSIONS);
    reset();contains=false;buttons[1]="确认添加";buttons[2]="确认添加";buttons[3]="返回";
    browse_watchlater_add(&v,run,login);
    CHECK(write_count==1 && !remove_count && read_count==1 && saw_confirmed && saw_once);
    reset();buttons[1]="取消稍后再看";buttons[2]="取消稍后再看";buttons[3]="返回";
    browse_watchlater_add(&v,run,login);
    CHECK(write_count==1 && remove_count==1 && read_count==1 && saw_removed && saw_remove_button);
    reset();keys[1]=KEY_A;keys[2]=KEY_A;keys[3]=KEY_B;
    browse_watchlater_add(&v,run,login);
    CHECK(write_count==1 && remove_count==1 && read_count==2 && saw_removed);
    reset();write_outcome=BILI_API_UNKNOWN;buttons[1]="取消稍后再看";buttons[2]="返回";
    browse_watchlater_add(&v,run,login);
    CHECK(write_count==1 && remove_count==1 && read_count==2 && saw_removed);
    reset();contains=false;write_outcome=BILI_API_UNKNOWN;buttons[1]="确认添加";buttons[2]="返回";
    browse_watchlater_add(&v,run,login);
    CHECK(write_count==1 && !remove_count && read_count==2 && saw_confirmed);
    reset();write_outcome=BILI_API_UNKNOWN;resolve_write=false;buttons[1]="取消稍后再看";buttons[2]="取消稍后再看";buttons[3]="返回";
    browse_watchlater_add(&v,run,login);
    CHECK(write_count==1 && read_count==2 && saw_unknown && !saw_removed);
    reset();read_outcome=BILI_API_UNKNOWN;keys[1]=KEY_A;keys[2]=KEY_B;
    browse_watchlater_add(&v,run,login);CHECK(!write_count && read_count==2 && saw_unknown && !saw_remove_button);
    reset();cancel_read=true;keys[1]=KEY_A;keys[2]=KEY_B;
    browse_watchlater_add(&v,run,login);CHECK(!write_count && read_count==2 && !saw_remove_button);
    reset();change_account_after_read=true;keys[1]=KEY_A;keys[2]=KEY_B;
    browse_watchlater_add(&v,run,login);CHECK(!write_count && !saw_remove_button);
    reset();close_after_read=true;browse_watchlater_add(&v,run,login);CHECK(!write_count && !draws_after_close);
    reset();keys[1]=KEY_B;browse_watchlater_add(&v,run,login);CHECK(!write_count);
    reset();v.aid=0;keys[1]=KEY_A;keys[2]=KEY_B;
    browse_watchlater_add(&v,run,login);CHECK(!write_count && !read_count);v.aid=99;
    reset();logged=false;keys[1]=KEY_A;keys[2]=KEY_A;keys[3]=KEY_B;
    CHECK(browse_watchlater_add(&v,run,login) && remove_count==1 && read_count==1);
    reset();write_outcome=BILI_API_REJECTED;resolve_write=false;keys[1]=KEY_A;keys[2]=KEY_B;
    CHECK(browse_watchlater_add(&v,run,login) && write_count==1 && !saw_removed);
    reset();write_outcome=BILI_API_UNKNOWN;readback_outcome=BILI_API_UNKNOWN;
    keys[1]=KEY_A;keys[2]=KEY_A;keys[3]=KEY_B;
    CHECK(browse_watchlater_add(&v,run,login) && write_count==1 && read_count==3 && saw_unknown && !saw_removed);
    reset();buttons[1]="返回";
    CHECK(!browse_watchlater_add(&v,run,login) && !write_count);
    printf("browse UI: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
