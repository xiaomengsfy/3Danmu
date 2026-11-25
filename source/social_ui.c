#include <stdio.h>
#include "social_ui.h"
#include "ui.h"
#include "net.h"
#include "thumb.h"
#include <stdlib.h>
#include <string.h>
static bool live(void){return aptMainLoop()&&!aptShouldClose()&&!net_is_shutting_down();
}
static u32 input(touchPosition*t){hidScanInput();
    memset(t,0,sizeof(*t));
    if(hidKeysHeld()&KEY_TOUCH)hidTouchRead(t);
    return hidKeysDown();
}
static bool button(int x,int y,int w,int h,const char*s,u32 k,touchPosition t){return ui_button(x,y,w,h,s,UI_COL_SEL,k&KEY_TOUCH,t.px,t.py);
}
static int lines(const char*body,int first,int y,int visible){const char*p=body;
    int row=0;
    while(*p){char line[256];
        int n=0;
        while(*p&&*p!='\n'){int bytes=1;
            unsigned char c=(unsigned char)*p;
            if(c>=0xf0)bytes=4;
            else if(c>=0xe0)bytes=3;
            else if(c>=0xc0)bytes=2;
            if(n+bytes>250)break;
            memcpy(line+n,p,(size_t)bytes);
            line[n+bytes]=0;
            if(n&&ui_text_width(line,UI_SHARP)>376)break;
            n+=bytes;
            p+=bytes;
        }
        line[n]=0;
        if(*p=='\n')p++;
        if(row>=first&&row<first+visible)ui_text(12,(float)(y+(row-first)*22),UI_SHARP,UI_COL_TEXT,line);
        row++;
    }return row;
}
typedef struct{int64_t mid,account;
    bool follow;
    SocialRelation relation;
    BiliApiResult result;
} RelationJob;
static int relation_read(void*a){RelationJob*j=a;
    j->result=social_relation(j->mid,&j->relation);
    return j->result.outcome==BILI_API_SUCCESS?0:-1;
}
static int relation_write(void*a){RelationJob*j=a;
    if(j->account<=0||j->account!=bili_account_id())return -1;
    j->result=social_relation_set(j->mid,j->follow);
    return j->result.outcome==BILI_API_SUCCESS?0:-1;
}
SocialRelation social_relationship_ui(int64_t mid,const char*name,BrowseRunner run){RelationJob j={.mid=mid,.account=bili_account_id()};
    if(!run||mid<=0||mid==bili_account_id())return j.relation;
    run(relation_read,&j,"读取关注状态",true);
    bool sent=false,confirm=false;
    char message[192]="请选择关注操作";
    while(live()&&j.account==bili_account_id()){touchPosition t;
        u32 k=input(&t);
        if(k&KEY_B)break;
        ui_begin();
        ui_text_clipped(12,20,UI_SHARP,UI_COL_TEXT,name?name:"UP主",376);
        ui_text_clipped(12,66,UI_SHARP,UI_COL_ACCENT,message,376);
        ui_text_clipped(12,110,UI_SHARP,UI_COL_DIM,j.relation.known?(j.relation.following?"已关注":"未关注"):"关注状态未确认",376);
        ui_text_clipped(12,152,UI_SHARP,UI_COL_DIM,"单次提交，结果不明先查询。",376);
        ui_begin_bottom();
        bool mutate=false;
        bool can_mutate=bili_logged_in()&&j.relation.known&&!sent;
        if(can_mutate)mutate=button(12,30,296,44,confirm?"再次确认取消关注":j.relation.following?"取消关注":"确认关注",k,t)||(k&KEY_A);
        bool query=button(12,94,296,42,"查询状态",k,t)||(!can_mutate&&(k&KEY_A)),back=button(12,160,296,42,"返回",k,t);
        ui_text(12,218,UI_SHARP,UI_COL_DIM,can_mutate?(confirm?"A 再次确认取消  B 返回":"A 确认  B 返回"):"A 查询状态  B 返回");
        ui_end();
        if(back)break;
        if(mutate){if(j.relation.following&&!confirm){confirm=true;
                strcpy(message,"确定取消关注？再次确认才提交。");
                continue;
            }
            j.follow=!j.relation.following;
            sent=true;
            confirm=false;
            run(relation_write,&j,"提交一次关注操作",false);
            BiliApiResult result=j.result;
            if(!live())break;
            if(result.outcome==BILI_API_REJECTED)strcpy(message,"服务器拒绝，未自动重试。");
            else {int ok=run(relation_read,&j,"核对关注状态",true);
                snprintf(message,sizeof(message),"%s",ok==0&&j.relation.known&&j.relation.following==j.follow?"状态查询已确认。":"结果未确认；请查询，勿重复提交。");
            }
        }else if(query){int ok=run(relation_read,&j,"查询关注状态",true);
            snprintf(message,sizeof(message),"%s",ok==0?"已刷新关注状态。":"查询未确认。");
        }}
    return j.relation;
}
typedef struct{int64_t mid;
    bool fans;
    int page;
    BiliProfile users[20];
    BiliPageInfo info;
} PeopleJob;
static int people_job(void*a){PeopleJob*j=a;
    return social_people(j->mid,j->fans,j->page,j->users,20,&j->info).outcome==BILI_API_SUCCESS?0:-1;
}
SocialSelection social_people_ui(int64_t mid,bool fans,BrowseRunner run){SocialSelection result={0};
    PeopleJob*j=calloc(1,sizeof(*j));
    if(!j||!run){free(j);
        return result;
    }
    j->mid=mid;
    j->fans=fans;
    j->page=1;
    bool fetch=true,loaded=false,last_window=false;
    int focus=0;
    while(live()){if(fetch){loaded=run(people_job,j,fans?"读取粉丝":"读取关注",true)==0;
            fetch=false;
            focus=last_window&&j->info.count?(j->info.count-1)/4*4:0;
            if(!loaded){memset(&j->info,0,sizeof(j->info));focus=0;}
            if(!live())break;
        }
        touchPosition t;
        u32 k=input(&t);
        if(k&KEY_B)break;
        if((k&KEY_DOWN)&&focus+1<j->info.count)focus++;
        if((k&KEY_UP)&&focus>0)focus--;
        if(k&KEY_L){
            if(focus/4>0)focus=(focus/4-1)*4;
            else if(j->page>1){j->page--;last_window=true;fetch=true;continue;}
        }
        if(k&KEY_R){
            if((focus/4+1)*4<j->info.count)focus=(focus/4+1)*4;
            else if(loaded&&j->info.has_more){j->page++;last_window=false;fetch=true;continue;}
        }
        int first=focus/4*4,hit=-1;
        ui_begin();
        ui_text(12,12,UI_SHARP,UI_COL_TEXT,fans?"粉丝列表":"关注列表");
        if(loaded&&j->info.count){ui_text_clipped(12,54,UI_SHARP,UI_COL_ACCENT,j->users[focus].name,376);
            lines(j->users[focus].sign,0,92,4);
        }else ui_text(12,60,UI_SHARP,UI_COL_TEXT,loaded?"没有条目":"读取未确认 / 无权限");
        ui_begin_bottom();
        for(int i=first;i<j->info.count&&i<first+4;i++)if(ui_button(10,12+(i-first)*34,300,30,j->users[i].name,
            i==focus?UI_COL_ACCENT:UI_COL_SEL,k&KEY_TOUCH,t.px,t.py))hit=i;
        ui_text(10,218,UI_SHARP,UI_COL_DIM,"上下选择  A 打开  B 返回  L/R 翻页");
        ui_end();
        if((k&KEY_A)&&loaded&&j->info.count)hit=focus;
        if(hit>=0){result.kind=SOCIAL_UP;
            result.profile=j->users[hit];
            break;
        }}
    free(j);
    return result;
}
static void images(const SocialDynamic*card){static BiliVideo pics[9];
    memset(pics,0,sizeof(pics));
    for(int i=0;i<card->image_count;i++){const char*url=card->images[i].url;
        if(!strncmp(url,"//",2))url+=2;
        else if(!strncmp(url,"https://",8))url+=8;
        else if(!strncmp(url,"http://",7))url+=7;
        else continue;
        const char*slash=strchr(url,'/');
        if(!slash)continue;
        size_t host=(size_t)(slash-url);
        if(host<10||memcmp(url+host-10,".hdslb.com",10))continue;
        snprintf(pics[i].pic,sizeof(pics[i].pic),"https://%.150s@192w_120h_1c.jpg",url);
    }
    thumb_start(pics,card->image_count);
    int focus=0;
    while(live()){touchPosition t;
        u32 k=input(&t);
        if(k&KEY_B)break;
        if((k&KEY_RIGHT)&&focus+1<card->image_count)focus++;
        if((k&KEY_LEFT)&&focus>0)focus--;
        ui_begin();
        thumb_new_frame(1);
        const C2D_Image*im=thumb_get(focus);
        if(im)C2D_DrawImageAt(*im,8,12,0.2f,NULL,4.0f,3.3f);
        else ui_text(12,70,UI_SHARP,UI_COL_DIM,"图片加载中 / 格式不支持");
        ui_begin_bottom();
        char label[100];
        snprintf(label,sizeof(label),"图片 %d/%d  原图 %dx%d",focus+1,card->image_count,card->images[focus].width,card->images[focus].height);
        ui_text_clipped(10,14,UI_SHARP,UI_COL_TEXT,label,300);
        bool prev=button(10,74,145,44,"上一张",k,t),next=button(165,74,145,44,"下一张",k,t),back=button(10,158,300,44,"返回",k,t);
        ui_end();
        if(back)break;
        if(prev&&focus>0)focus--;
        if(next&&focus+1<card->image_count)focus++;
    }thumb_stop();
}
typedef struct{int64_t mid;
    bool videos;
    char cursor[SOCIAL_CURSOR];
    SocialDynamic cards[50];
    SocialPage info;
} DynamicJob;
static int dynamic_job(void*a){DynamicJob*j=a;
    return social_dynamics(j->mid,j->cursor,j->videos,j->cards,50,&j->info).outcome==BILI_API_SUCCESS?0:-1;
}
SocialSelection social_dynamics_ui(int64_t mid,BrowseRunner run){SocialSelection result={0};
    DynamicJob*j=calloc(1,sizeof(*j));
    if(!j||!run){free(j);
        return result;
    }j->mid=mid;
    int focus=0,scroll=0;
    bool fetch=true,loaded=false;
    while(live()){if(fetch){loaded=run(dynamic_job,j,"读取动态",true)==0;
            focus=scroll=0;
            fetch=false;
            if(!live())break;
        }touchPosition t;
        u32 k=input(&t);
        if(k&KEY_B)break;
        int count=loaded?j->info.page.count:0;
        SocialDynamic*c=count?&j->cards[focus]:NULL;
        if((k&KEY_DOWN)&&scroll<1000)scroll++;
        if((k&KEY_UP)&&scroll>0)scroll--;
        if((k&KEY_RIGHT)&&focus+1<count){focus++;
            scroll=0;
            c=&j->cards[focus];
        }if((k&KEY_LEFT)&&focus>0){focus--;
            scroll=0;
            c=&j->cards[focus];
        }
        ui_begin();
        if(c){ui_text_clipped(12,8,UI_SHARP,UI_COL_ACCENT,c->author.name,376);
            int total=lines(c->body,scroll,36,8);
            if(scroll>0&&scroll>=total)scroll=total>0?total-1:0;
        }else ui_text(12,60,UI_SHARP,UI_COL_TEXT,loaded?"没有动态":"读取未确认 / 无权限");
        ui_begin_bottom();
        bool filter=button(10,8,145,36,j->videos?"仅视频":"全部动态",k,t),page=button(165,8,145,36,"下一页",k,t),prev=button(10,54,145,36,"上一条",k,t),next=button(165,54,145,36,"下一条",k,t),open=button(10,100,145,36,"打开内容 A",k,t),up=button(165,100,145,36,"打开UP",k,t),pic=button(10,146,145,36,"查看图片",k,t),back=button(165,146,145,36,"返回 B",k,t);
        ui_text_clipped(10,190,UI_SHARP,UI_COL_DIM,c&&c->truncated?"上下滚动；正文或图片已限长":"上下滚正文，左右换动态",300);
        ui_end();
        if(back)break;
        if(filter||(k&KEY_X)){j->videos=!j->videos;
            j->cursor[0]=0;
            fetch=true;
        }else if((page||(k&KEY_R))&&loaded&&j->info.page.has_more){snprintf(j->cursor,sizeof(j->cursor),"%s",j->info.next);
            fetch=true;
        }else if(prev&&focus>0){focus--;
            scroll=0;
        }else if(next&&focus+1<count){focus++;
            scroll=0;
        }else if(c&&(open||(k&KEY_A))){result=c->target;
            break;
        }else if(c&&up){result.kind=SOCIAL_UP;
            result.profile=c->author;
            break;
        }else if(c&&pic&&c->image_count)images(c);
    }free(j);
    return result;
}
typedef struct{SocialMessageKind kind;
    char cursor[SOCIAL_CURSOR];
    SocialMessage items[20];
    SocialPage info;
} MessageJob;
static int message_job(void*a){MessageJob*j=a;
    return social_messages(j->kind,j->cursor,j->items,20,&j->info).outcome==BILI_API_SUCCESS?0:-1;
}
SocialSelection social_messages_ui(BrowseRunner run){const char*names[]={"回复","提到我","收到的赞","系统通知","未读统计"};
    SocialSelection result={0};
    MessageJob*j=calloc(1,sizeof(*j));
    if(!j||!run){free(j);
        return result;
    }bool fetch=true,loaded=false;
    int focus=0,scroll=0;
    while(live()){if(fetch){loaded=run(message_job,j,"读取消息（只读）",true)==0;
            fetch=false;
            focus=scroll=0;
            if(!live())break;
        }touchPosition t;
        u32 k=input(&t);
        if(k&KEY_B)break;
        int count=loaded?j->info.page.count:0;
        if((k&KEY_DOWN)&&scroll<600)scroll++;
        if((k&KEY_UP)&&scroll>0)scroll--;
        ui_begin();
        ui_text(12,8,UI_SHARP,UI_COL_ACCENT,names[j->kind]);
        if(count){SocialMessage*m=&j->items[focus];
            ui_text_clipped(12,36,UI_SHARP,UI_COL_TEXT,m->title,376);
            int total=lines(m->body,scroll,64,7);
            if(scroll>=total)scroll=total>0?total-1:0;
        }else if(social_message_support_note(j->kind))lines(social_message_support_note(j->kind),0,70,5);
        else ui_text(12,70,UI_SHARP,UI_COL_TEXT,loaded?"没有消息":"消息读取未确认");
        ui_begin_bottom();
        int kind=-1;
        for(int i=0;i<5;i++)if(button(10+(i%3)*100,8+(i/3)*42,96,36,names[i],k,t))kind=i;
        bool prev=button(10,96,145,36,"上一条",k,t),next=button(165,96,145,36,"下一条",k,t),page=button(10,140,145,36,"下一页",k,t),open=button(165,140,145,36,"打开内容 A",k,t),back=button(10,184,300,28,"返回 B",k,t);
        ui_end();
        if(back)break;
        if(kind>=0){j->kind=(SocialMessageKind)kind;
            j->cursor[0]=0;
            fetch=true;
        }else if(prev&&focus>0){focus--;
            scroll=0;
        }else if(next&&focus+1<count){focus++;
            scroll=0;
        }else if((page||(k&KEY_R))&&loaded&&j->info.page.has_more){snprintf(j->cursor,sizeof(j->cursor),"%s",j->info.next);
            fetch=true;
        }else if((open||(k&KEY_A))&&count&&j->items[focus].target.kind!=SOCIAL_NONE){result=j->items[focus].target;
            break;
        }
    }free(j);
    return result;
}
