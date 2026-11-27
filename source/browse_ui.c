#include "browse_ui.h"
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "net.h"

static bool live(void) { return aptMainLoop() && !net_is_shutting_down() && !aptShouldClose(); }
static u32 input(touchPosition *touch) {
    hidScanInput();memset(touch,0,sizeof(*touch));
    if(hidKeysHeld()&KEY_TOUCH) hidTouchRead(touch);
    return hidKeysDown();
}
static bool button(float x,float y,float w,float h,const char *name,u32 down,touchPosition t) {
    return ui_button(x,y,w,h,name,UI_COL_SEL,down&KEY_TOUCH,t.px,t.py);
}
static void decimal(int64_t n,char *out) {
    if(n<0) { strcpy(out,"未知");return; }
    char rev[24];int count=0;do{rev[count++]=(char)('0'+n%10);n/=10;}while(n);
    for(int i=0;i<count;i++)out[i]=rev[count-1-i];
    out[count]=0;
}
static void profile_top(const BiliProfile *p,const char *message) {
    char uid[24],fans[24],videos[24],line[128];decimal(p->mid,uid);
    decimal(p->fans,fans);decimal(p->videos,videos);
    ui_text_clipped(12,16,UI_SHARP,UI_COL_TEXT,p->name,376);
    snprintf(line,sizeof(line),"UID %s    Lv%d",uid,p->level);
    ui_text_clipped(12,48,UI_SHARP,UI_COL_DIM,line,376);
    snprintf(line,sizeof(line),"粉丝 %s    投稿 %s",fans,videos);
    ui_text_clipped(12,80,UI_SHARP,UI_COL_DIM,line,376);
    ui_text_clipped(12,116,UI_SHARP,UI_COL_TEXT,p->sign[0]?p->sign:"暂无签名",376);
    ui_text_clipped(12,166,UI_SHARP,UI_COL_ACCENT,message?message:"",376);
}
int browse_search_kind(void) {
    int focus=0;
    while(live()) {
        touchPosition t;u32 down=input(&t);if(down&KEY_B)return -1;
        if((down&KEY_DOWN)&&focus<2)focus++;
        if((down&KEY_UP)&&focus>0)focus--;
        ui_begin();ui_text(12,30,UI_SHARP,UI_COL_TEXT,"选择搜索类型");ui_begin_bottom();
        static const char *names[]={"视频","UP主","番剧"};int hit=-1;
        for(int i=0;i<3;i++)if(ui_button(12,22+i*52,296,42,names[i],
            i==focus?UI_COL_ACCENT:UI_COL_SEL,down&KEY_TOUCH,t.px,t.py))hit=i;
        ui_text(12,218,UI_SHARP,UI_COL_DIM,"上下选择  A 打开  B 返回");
        bool back=button(12,180,296,30,"返回",down,t);ui_end();
        if(down&KEY_A)hit=focus;
        if(hit>=0)return hit;
        if(back)return -1;
    }
    return -1;
}
BrowseChoice browse_profile_relationship(const BiliProfile *p,bool own,const char *message,const char *relation) {
    int focus=0;
    while(live()) {
        touchPosition t;u32 down=input(&t);if(down&KEY_B)return BROWSE_BACK;
        int count=own?5:2;
        if((down&KEY_LEFT) && focus%2)focus--;
        else if((down&KEY_RIGHT) && !(focus%2) && focus+1<count)focus++;
        else if((down&KEY_UP) && focus>=2)focus-=2;
        else if((down&KEY_DOWN) && focus/2<(count-1)/2) {
            focus+=2;if(focus>=count)focus=count-1;
        }
        ui_begin();profile_top(p,message);ui_begin_bottom();
        BrowseChoice choice=BROWSE_BACK;bool back=false;
        static const char *own_names[]={"我的投稿","播放历史","我的关注","我的粉丝","稍后再看"};
        static const BrowseChoice own_choices[]={BROWSE_SUBMISSIONS,BROWSE_HISTORY,BROWSE_FOLLOWING,BROWSE_FANS,BROWSE_WATCHLATER};
        const char *up_names[]={"查看投稿",relation?relation:"关注 / 查询状态"};
        static const BrowseChoice up_choices[]={BROWSE_SUBMISSIONS,BROWSE_RELATION};
        for(int i=0;i<count;i++) {
            bool hit=ui_button(10+(i%2)*155,18+(i/2)*48,145,38,
                own?own_names[i]:up_names[i],i==focus?UI_COL_ACCENT:UI_COL_SEL,down&KEY_TOUCH,t.px,t.py);
            if(hit||((down&KEY_A)&&i==focus))choice=own?own_choices[i]:up_choices[i];
        }
        ui_text(12,218,UI_SHARP,UI_COL_DIM,"方向键选择  A 打开  B 返回");
        back=button(12,184,296,28,"返回",down,t);ui_end();
        if(back || choice!=BROWSE_BACK)return choice;
    }
    return BROWSE_BACK;
}
BrowseChoice browse_profile(const BiliProfile *p,bool own,const char *message) {
    return browse_profile_relationship(p,own,message,NULL);
}
typedef struct {
    const char *keyword;int page;
    BiliProfile users[20];BiliPageInfo info;BiliApiResult result;
} UserJob;
/* Kept off the nested 3DS main/thread stacks. Only one modal runs at a time. */
static UserJob s_users;
static int users_job(void *arg) {
    UserJob *j=arg;j->result=bili_search_users(j->keyword,j->page,j->users,20,&j->info);
    return j->result.outcome==BILI_API_SUCCESS?0:-1;
}
bool browse_users(const char *keyword,BrowseRunner run,BiliProfile *selected) {
    memset(selected,0,sizeof(*selected));memset(&s_users,0,sizeof(s_users));
    s_users.keyword=keyword;s_users.page=1;int focus=0;bool fetch=true,loaded=false,last_window=false;
    while(live()) {
        if(fetch) {
            loaded=run(users_job,&s_users,"搜索UP主",true)==0;
            if(!loaded || net_is_shutting_down() || aptShouldClose())return false;
            focus=last_window&&s_users.info.count?(s_users.info.count-1)/4*4:0;fetch=false;
        }
        touchPosition t;u32 down=input(&t);if(down&KEY_B)return false;
        if((down&KEY_DOWN) && focus+1<s_users.info.count)focus++;
        if((down&KEY_UP) && focus>0)focus--;
        if(down&KEY_L) {
            if(focus/4>0)focus=(focus/4-1)*4;
            else if(s_users.page>1){s_users.page--;last_window=true;fetch=true;continue;}
        }
        if(down&KEY_R) {
            if((focus/4+1)*4<s_users.info.count)focus=(focus/4+1)*4;
            else if(s_users.info.has_more){s_users.page++;last_window=false;fetch=true;continue;}
        }
        int first=(focus/4)*4,hit=-1;char title[160];
        ui_begin();
        if(s_users.info.count)profile_top(&s_users.users[focus],NULL);
        else ui_text(12,60,UI_SHARP,UI_COL_TEXT,"没有搜索结果");
        ui_begin_bottom();snprintf(title,sizeof(title),"UP: %s  P%d",keyword,s_users.page);
        ui_text_clipped(10,4,UI_SHARP,UI_COL_TEXT,title,300);
        for(int i=first;i<s_users.info.count && i<first+4;i++)
            if(ui_button(10,32+(i-first)*32,300,28,s_users.users[i].name,
                i==focus?UI_COL_ACCENT:UI_COL_SEL,down&KEY_TOUCH,t.px,t.py))hit=i;
        ui_text(10,218,UI_SHARP,UI_COL_DIM,"上下选择  A 打开  B 返回  L/R 翻页");ui_end();
        if((down&KEY_A) && s_users.info.count)hit=focus;
        if(hit>=0){*selected=s_users.users[hit];return true;}
    }
    return false;
}
typedef struct {int64_t aid,owner;bool found,remove;BiliApiResult result;} WatchJob;
static int watch_write(void *arg) {
    WatchJob *j=arg;
    if(!j->owner || j->owner!=bili_account_id())return -1;
    j->result=j->remove?bili_watchlater_remove(j->aid):bili_watchlater_add(j->aid);
    return j->result.outcome==BILI_API_SUCCESS?0:-1;
}
static int watch_read(void *arg) {
    WatchJob *j=arg;j->result=bili_watchlater_contains(j->aid,&j->found);
    return j->result.outcome==BILI_API_SUCCESS?0:-1;
}
static bool watch_query(WatchJob *job,BrowseRunner run) {
    job->owner=bili_account_id();job->found=false;
    job->result=(BiliApiResult){.outcome=BILI_API_UNKNOWN};
    int result=run(watch_read,job,"查询稍后再看",true);
    return result==0 && job->result.outcome==BILI_API_SUCCESS &&
        job->owner>0 && job->owner==bili_account_id();
}
/* Return whether a write was attempted: the caller must reload a Watch Later
 * list even when the reply was lost. One write per modal prevents key repeats
 * or uncertain readback from reversing/replaying the requested operation. */
bool browse_watchlater_add(const BiliVideo *video,BrowseRunner run,bool (*login)(void)) {
    WatchJob job={.aid=video->aid};bool submitted=false,known=false;
    bool query_needed=video->aid>0 && bili_logged_in();
    char message[192]="请登录后查询稍后再看状态。";
    if(video->aid<=0)snprintf(message,sizeof(message),"视频编号未确认，请重新加载后再试。");
    while(live()) {
        if(query_needed) {
            known=watch_query(&job,run);query_needed=false;
            snprintf(message,sizeof(message),"%s",known?
                (job.found?"已在稍后再看，确认后取消。":"当前不在稍后再看，确认后添加。"):
                "查询未确认，请重新查询后操作。");
            if(!live())break;
        }
        if(known && job.owner!=bili_account_id()) {
            known=false;snprintf(message,sizeof(message),"登录状态已变化，请重新查询。");
        }
        touchPosition t;u32 down=input(&t);if(down&KEY_B)break;
        ui_begin();ui_text_clipped(12,20,UI_SHARP,UI_COL_TEXT,video->title,376);
        ui_text_clipped(12,80,UI_SHARP,UI_COL_ACCENT,message,376);
        ui_text_clipped(12,126,UI_SHARP,UI_COL_DIM,"单次提交，结果不明时先查询。",376);
        ui_begin_bottom();bool change=false,query=false,sign_in=false;
        if(!bili_logged_in())sign_in=button(12,38,296,44,"扫码登录",down,t)||(down&KEY_A);
        else if(video->aid>0) {
            if(known && !submitted)change=ui_button(12,32,296,44,
                job.found?"取消稍后再看":"确认添加",job.found?UI_COL_ACCENT:UI_COL_SEL,
                down&KEY_TOUCH,t.px,t.py)||(down&KEY_A);
            query=button(12,94,296,44,"查询状态",down,t)||((submitted||!known)&&(down&KEY_A));
        }
        ui_text(12,218,UI_SHARP,UI_COL_DIM,!bili_logged_in()?"A 登录  B 返回":video->aid<=0?"B 返回":
            submitted||!known?"A 查询状态  B 返回":job.found?"A 取消稍后再看  B 返回":"A 确认添加  B 返回");
        bool back=button(12,166,296,38,"返回",down,t);ui_end();if(back)break;
        if(sign_in) { if(login())query_needed=video->aid>0;continue; }
        if(change) {
            submitted=true;job.remove=job.found;
            job.result=(BiliApiResult){.outcome=BILI_API_UNKNOWN};
            run(watch_write,&job,job.remove?"正在取消，请等待":"正在添加，请等待",false);
            if(!live())break;
            BiliApiResult write=job.result;
            if(job.owner!=bili_account_id()) {
                known=false;snprintf(message,sizeof(message),"登录状态已变化，结果未确认。");
            } else if(write.outcome==BILI_API_SUCCESS) {
                job.found=!job.remove;
                snprintf(message,sizeof(message),"%s",job.remove?
                    "已取消稍后再看，服务器已确认。":"已在稍后再看，服务器已确认。");
            } else if(write.outcome==BILI_API_REJECTED) {
                snprintf(message,sizeof(message),"%s",bili_last_error());
            } else {
                known=watch_query(&job,run);
                snprintf(message,sizeof(message),"%s",known && job.found!=job.remove?
                    (job.remove?"已取消稍后再看，查询已确认。":"已在稍后再看，查询已确认。"):
                    "结果未确认，请查询或稍后核对。");
            }
        } else if(query) {
            known=watch_query(&job,run);
            snprintf(message,sizeof(message),"%s",known ?
                (job.found?"已在稍后再看。":"当前列表没有此视频。"):
                "查询未确认，请稍后重试。");
        }
    }
    return submitted;
}

int browse_folders(const BiliFavFolder *folders,int count,bool truncated) {
    if(!folders || count<0)return -1;
    int focus=0;
    while(live()) {
        touchPosition touch;u32 down=input(&touch);
        if(down&KEY_B)return -1;
        if(count>0) {
            if((down&KEY_UP) && focus>0)focus--;
            else if((down&KEY_DOWN) && focus+1<count)focus++;
            else if((down&(KEY_LEFT|KEY_L)) && focus>=4)focus-=4;
            else if((down&(KEY_RIGHT|KEY_R)) && focus/4<(count-1)/4) {
                focus+=4;if(focus>=count)focus=count-1;
            }
        }
        ui_begin();
        char line[128],id[24];
        ui_text_clipped(12,16,UI_SHARP,UI_COL_ACCENT,"我的收藏夹",376);
        if(count>0) {
            const BiliFavFolder *f=&folders[focus];decimal(f->id,id);
            ui_text_clipped(12,50,UI_SHARP,UI_COL_TEXT,f->title,376);
            snprintf(line,sizeof line,"收藏夹 ID %s",id);
            ui_text_clipped(12,88,UI_SHARP,UI_COL_DIM,line,376);
            snprintf(line,sizeof line,"视频数量：%d",f->media_count);
            ui_text_clipped(12,120,UI_SHARP,UI_COL_TEXT,line,376);
            snprintf(line,sizeof line,"第 %d/%d 个  第 %d/%d 页",focus+1,count,focus/4+1,(count+3)/4);
            ui_text_clipped(12,154,UI_SHARP,UI_COL_DIM,line,376);
        } else ui_text_clipped(12,50,UI_SHARP,UI_COL_TEXT,"还没有收藏夹",376);
        if(truncated)ui_text_clipped(12,188,UI_SHARP,UI_COL_DIM,"收藏夹超过128个，仅显示前128个",376);
        ui_begin_bottom();int hit=-1,first=focus/4*4;
        for(int i=first;i<count && i<first+4;i++) {
            if(ui_button(10,12+(i-first)*40,300,34,folders[i].title,
                i==focus?UI_COL_ACCENT:UI_COL_SEL,down&KEY_TOUCH,touch.px,touch.py))hit=i;
        }
        bool back=button(10,178,300,30,"返回",down,touch);
        ui_text(8,218,UI_SHARP,UI_COL_DIM,"上下选择 左右/L/R翻页 A打开 B返回");
        ui_end();
        if(back)return -1;
        if(hit>=0)return hit;
        if((down&KEY_A)&&count>0)return focus;
    }
    return -1;
}
