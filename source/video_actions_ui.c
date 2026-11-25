#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "video_actions_ui.h"
#include "video_actions_api.h"
#include "ui.h"
#include "net.h"
static bool live(void){return aptMainLoop()&&!aptShouldClose()&&!net_is_shutting_down();
}
static u32 input(touchPosition*t){hidScanInput();
    memset(t,0,sizeof(*t));
    if(hidKeysHeld()&KEY_TOUCH)hidTouchRead(t);
    return hidKeysDown();
}
static bool button(int x,int y,int w,int h,const char*s,u32 k,touchPosition t){return ui_button(x,y,w,h,s,UI_COL_SEL,k&KEY_TOUCH,t.px,t.py);
}
typedef struct{int64_t aid,account;
    int coins,count;
    bool like;
    BiliLikeState liked;
    BiliFavFolder folders[VIDEO_FOLDER_MAX],check[VIDEO_FOLDER_MAX];
    bool selected[VIDEO_FOLDER_MAX];
    BiliPageInfo info,check_info;
    BiliApiResult result;
    int mode;
} ActionJob;
static bool bound(ActionJob*j){return j->account>0&&j->account==bili_account_id()&&bili_logged_in();
}
static int read_coins(void*a){ActionJob*j=a;
    j->coins=-1;
    if(!bound(j))return -1;
    j->result=video_coins(j->aid,&j->coins);
    return bound(j)&&j->result.outcome==BILI_API_SUCCESS?0:-1;
}
static int write_coin(void*a){ActionJob*j=a;
    if(!bound(j))return -1;
    j->result=video_coin_once(j->aid,j->count,j->like);
    return j->result.outcome==BILI_API_SUCCESS?0:-1;
}
static int read_folders(void*a){ActionJob*j=a;
    memset(&j->info,0,sizeof(j->info));
    if(!bound(j))return -1;
    j->result=video_folders(j->aid,j->folders,VIDEO_FOLDER_MAX,&j->info);
    return bound(j)&&j->result.outcome==BILI_API_SUCCESS?0:-1;
}
static int check_folders(void*a){ActionJob*j=a;
    if(!bound(j))return -1;
    j->result=video_folders(j->aid,j->check,VIDEO_FOLDER_MAX,&j->check_info);
    return bound(j)&&j->result.outcome==BILI_API_SUCCESS?0:-1;
}
static int write_folders(void*a){ActionJob*j=a;
    if(!bound(j))return -1;
    j->result=video_favorite_delta_once(j->aid,j->folders,j->selected,j->info.count);
    return j->result.outcome==BILI_API_SUCCESS?0:-1;
}
static int read_like(void*a){ActionJob*j=a;
    if(!bound(j))return -1;
    j->result=bili_like_status(j->aid,&j->liked);
    return bound(j)&&j->result.outcome==BILI_API_SUCCESS?0:-1;
}
static int write_like(void*a){ActionJob*j=a;
    if(!bound(j))return -1;
    j->result=bili_like_set(j->aid,j->like);
    return j->result.outcome==BILI_API_SUCCESS?0:-1;
}
static void coin_ui(ActionJob*j,BrowseRunner run){j->count=1;
    j->like=false;
    bool sent=false,confirm=false;
    int baseline=-1;
    char message[192]="先读取投币状态";
    if(run(read_coins,j,"查询已投币数",true)==0){baseline=j->coins;
        snprintf(message,sizeof(message),"已投 %d 枚；本次最多 %d 枚",baseline,2-baseline);
    }else strcpy(message,"投币状态未确认，禁止提交。");
    while(live()&&bound(j)){touchPosition t;
        u32 k=input(&t);
        if(k&KEY_B)break;
        ui_begin();
        ui_text(12,20,UI_SHARP,UI_COL_TEXT,"投币（消耗硬币）");
        ui_text_clipped(12,64,UI_SHARP,UI_COL_ACCENT,message,376);
        char label[128];
        snprintf(label,sizeof(label),"本次 %d 枚，%s附加点赞",j->count,j->like?"":"不");
        ui_text(12,108,UI_SHARP,UI_COL_TEXT,label);
        ui_text(12,152,UI_SHARP,UI_COL_DIM,"每次打开最多提交一次，未知不重试。");
        ui_begin_bottom();
        bool one=false,two=false,toggle=false,submit=false;
        if(!sent&&baseline>=0&&baseline<2){one=button(10,20,145,40,"选择1枚",k,t);
            if(baseline==0)two=button(165,20,145,40,"选择2枚",k,t);
            toggle=button(10,76,300,40,"切换附加点赞",k,t);
            submit=button(10,132,300,40,confirm?"再次确认投币":"确认投币数量",k,t);
        }bool back=button(10,184,300,28,"返回",k,t);
        ui_text(10,218,UI_SHARP,UI_COL_DIM,"触屏选数量 A确认 B返回");
        if(!sent&&baseline>=0&&baseline<2&&(k&KEY_A))submit=true;
        ui_end();
        if(back)break;
        if(one||two||toggle){if(one)j->count=1;
            if(two)j->count=2;
            if(toggle)j->like=!j->like;
            confirm=false;
        }
        if(submit){if(!confirm){confirm=true;
                snprintf(message,sizeof(message),"确认消耗 %d 枚硬币%s？",j->count,j->like?"并点赞":"");
                continue;
            }sent=true;
            run(write_coin,j,"单次投币，请等待",false);
            BiliApiOutcome outcome=j->result.outcome;
            if(!live()||!bound(j))break;
            if(outcome==BILI_API_REJECTED)strcpy(message,"服务器拒绝投币，未重试。");
            else {int read=run(read_coins,j,"核对投币状态",true);
                snprintf(message,sizeof(message),"%s",read==0&&j->coins>=baseline+j->count?"已投币，查询确认。":"结果未确认，请稍后核对，勿重复投币。");
            }}
    }}
static void favorite_ui(ActionJob*j,BrowseRunner run){bool loaded=run(read_folders,j,"读取所有收藏夹状态",true)==0;
    if(!live()||!bound(j))return;
    for(int i=0;i<j->info.count;i++)j->selected[i]=j->folders[i].contains;
    bool sent=false,confirm=false;
    int page=0;
    char message[192]="选择目标收藏夹，确认后仅提交变化。";
    if(!loaded)strcpy(message,"收藏夹状态未确认，禁止提交。");
    else if(j->info.truncated)strcpy(message,"收藏夹超过100个，禁止不完整修改。");
    while(live()&&bound(j)){touchPosition t;
        u32 k=input(&t);
        if(k&KEY_B)break;
        int pages=j->info.count>0?(j->info.count+2)/3:1;
        if(k&(KEY_UP|KEY_L)) { if(page>0)page--; }
        else if((k&(KEY_DOWN|KEY_R)) && page+1<pages)page++;
        int changes=0;
        for(int i=0;i<j->info.count;i++)if(j->selected[i]!=j->folders[i].contains)changes++;
        ui_begin();
        ui_text(12,20,UI_SHARP,UI_COL_TEXT,"选择收藏夹");
        ui_text_clipped(12,64,UI_SHARP,UI_COL_ACCENT,message,376);
        char label[128];
        snprintf(label,sizeof(label),"已修改 %d 个；第 %d/%d 页",changes,page+1,pages);
        ui_text(12,108,UI_SHARP,UI_COL_DIM,label);
        ui_begin_bottom();
        int hit=-1,first=page*3;
        for(int i=first;i<j->info.count&&i<first+3;i++){char row[150];
            snprintf(row,sizeof(row),"[%c] %.127s",j->selected[i]?'x':' ',j->folders[i].title);
            if(ui_button(10,8+(i-first)*36,300,32,row,j->selected[i]?UI_COL_ACCENT:UI_COL_SEL,k&KEY_TOUCH,t.px,t.py))hit=i;
        }
        bool submit=false;
        if(loaded&&!j->info.truncated&&!sent&&changes)submit=button(10,126,300,36,confirm?"再次确认收藏变更":"确认收藏变更",k,t);
        bool back=button(10,176,300,36,"返回",k,t);
        ui_text(10,218,UI_SHARP,UI_COL_DIM,"上下/LR翻页 触屏选夹 X提交 B返回");
        if(loaded&&!j->info.truncated&&!sent&&changes&&(k&KEY_X))submit=true;
        ui_end();
        if(back)break;
        if(hit>=0&&!sent&&loaded&&!j->info.truncated){j->selected[hit]=!j->selected[hit];
            confirm=false;
        }
        if(submit){if(!confirm){confirm=true;
                snprintf(message,sizeof(message),"确认提交 %d 个收藏夹变化？",changes);
                continue;
            }sent=true;
            run(write_folders,j,"单次提交收藏变更",false);
            BiliApiOutcome outcome=j->result.outcome;
            if(!live()||!bound(j))break;
            if(outcome==BILI_API_REJECTED)strcpy(message,"服务器拒绝收藏变更，未重试。");
            else {bool matches=run(check_folders,j,"核对收藏夹状态",true)==0&&!j->check_info.truncated;
                for(int i=0;i<j->info.count&&matches;i++){bool found=false;
                    for(int q=0;q<j->check_info.count;q++)if(j->check[q].id==j->folders[i].id){found=true;
                        if(j->check[q].contains!=j->selected[i])matches=false;
                        break;
                    }if(!found)matches=false;
                }
                snprintf(message,sizeof(message),"%s",matches?"收藏状态查询确认。":"结果未确认，请稍后核对，勿重复提交。");
            }}
    }}
static void like_ui(ActionJob*j,BrowseRunner run){j->liked=BILI_LIKE_UNKNOWN;
    run(read_like,j,"查询点赞状态",true);
    bool sent=false;
    int focus=0;
    char message[192]="请选择点赞操作";
    while(live()&&bound(j)){touchPosition t;
        u32 k=input(&t);
        if(k&KEY_B)break;
        ui_begin();
        if(k&KEY_UP)focus=0;
        if(k&KEY_DOWN)focus=1;
        ui_text(12,20,UI_SHARP,UI_COL_TEXT,"点赞");
        ui_text_clipped(12,64,UI_SHARP,UI_COL_ACCENT,message,376);
        ui_text(12,108,UI_SHARP,UI_COL_DIM,j->liked==BILI_LIKE_CONFIRMED?"近期点赞已报告":"未报告近期点赞，不代表从未点赞");
        ui_begin_bottom();
        bool yes=false,no=false;
        if(!sent){yes=ui_button(10,30,300,44,"确认点赞",focus==0?UI_COL_ACCENT:UI_COL_SEL,k&KEY_TOUCH,t.px,t.py);
            no=ui_button(10,92,300,44,"确认取消点赞",focus==1?UI_COL_ACCENT:UI_COL_SEL,k&KEY_TOUCH,t.px,t.py);
            if(k&KEY_A){yes=focus==0;no=focus==1;}
        }bool back=button(10,166,300,40,"返回",k,t);
        ui_text(10,218,UI_SHARP,UI_COL_DIM,"上下选择 A确认 B返回");
        ui_end();
        if(back)break;
        if(yes||no){sent=true;
            j->like=yes;
            run(write_like,j,"单次提交点赞",false);
            BiliApiOutcome outcome=j->result.outcome;
            if(!live()||!bound(j))break;
            if(outcome==BILI_API_SUCCESS)strcpy(message,"服务器确认操作完成。");
            else if(outcome==BILI_API_REJECTED)strcpy(message,"服务器拒绝操作，未重试。");
            else {run(read_like,j,"查询点赞状态",true);
                strcpy(message,"提交结果未确认，已查询；勿重复提交。");
            }}
    }}
void video_account_actions_ui(const BiliVideo*video,BrowseRunner run,bool(*login)(void)){if(!video||video->aid<=0||!run)return;
    ActionJob*j=calloc(1,sizeof(*j));
    if(!j)return;
    j->aid=video->aid;
    j->account=bili_account_id();
    while(live()){touchPosition t;
        u32 k=input(&t);
        if(k&KEY_B)break;
        ui_begin();
        ui_text_clipped(12,20,UI_SHARP,UI_COL_TEXT,video->title,376);
        ui_text(12,76,UI_SHARP,UI_COL_DIM,"账号操作：明确确认后仅提交一次。");
        ui_begin_bottom();
        bool sign=false,like=false,coin=false,fav=false;
        if(!bili_logged_in())sign=button(10,28,300,44,"扫码登录",k,t);
        else if(bound(j)){like=button(10,10,300,38,"点赞操作",k,t);
            coin=button(10,58,300,38,"投币操作",k,t);
            fav=button(10,106,300,38,"收藏夹操作",k,t);
        }bool back=button(10,164,300,40,"返回",k,t);
        ui_end();
        if(back)break;
        if(sign&&login){login();
            j->account=bili_account_id();
        }else if(!bound(j)&&bili_logged_in())break;
        else if(like)like_ui(j,run);
        else if(coin)coin_ui(j,run);
        else if(fav)favorite_ui(j,run);
    }free(j);
}
void video_account_action_ui(const BiliVideo*video,int kind,BrowseRunner run,bool(*login)(void)) {
    if(!video||video->aid<=0||kind<0||kind>2||!run)return;
    if(!bili_logged_in() && (!login||!login()))return;
    if(!live()||!bili_logged_in())return;
    ActionJob*j=calloc(1,sizeof(*j));
    if(!j)return;
    j->aid=video->aid;
    j->account=bili_account_id();
    if(bound(j)){if(kind==0)like_ui(j,run);
        else if(kind==1)coin_ui(j,run);
        else favorite_ui(j,run);
    }free(j);
}

static int read_action_state(void *arg) {
    ActionJob *j=arg;
    if(!bound(j))return -1;
    j->liked=BILI_LIKE_UNKNOWN;j->coins=-1;
    if(bili_like_status(j->aid,&j->liked).outcome!=BILI_API_SUCCESS)j->liked=BILI_LIKE_UNKNOWN;
    if(!bound(j))return -1;
    if(video_coins(j->aid,&j->coins).outcome!=BILI_API_SUCCESS)j->coins=-1;
    if(!bound(j))return -1;
    j->result=video_folders(j->aid,j->folders,VIDEO_FOLDER_MAX,&j->info);
    return bound(j)?0:-1;
}
void video_account_state(const BiliVideo *video,BrowseRunner run,VideoAccountState *out) {
    if(!out)return;
    *out=(VideoAccountState){.liked=BILI_LIKE_UNKNOWN,.coins=-1};
    if(!video||video->aid<=0||!run||!bili_logged_in())return;
    ActionJob *j=calloc(1,sizeof(*j));if(!j)return;
    j->aid=video->aid;j->account=bili_account_id();
    if(run(read_action_state,j,"读取互动状态",true)==0&&bound(j)) {
        out->liked=j->liked;out->coins=j->coins;
        if(j->result.outcome==BILI_API_SUCCESS) {
            out->favorite_known=!j->info.truncated;
            for(int i=0;i<j->info.count;i++)if(j->folders[i].membership_known&&j->folders[i].contains) {
                out->favorite=true;out->favorite_known=true;break;
            }
        }
    }
    free(j);
}
