#include "numeric_text.h"
#include <stdio.h>
#include <string.h>
#include "pgc_ui.h"
#include "net.h"
#include "ui.h"
static bool live(void){return aptMainLoop()&&!net_is_shutting_down()&&!aptShouldClose();}
static u32 input(touchPosition *t){hidScanInput();memset(t,0,sizeof(*t));if(hidKeysHeld()&KEY_TOUCH)hidTouchRead(t);return hidKeysDown();}
static bool button(float x,float y,float w,const char *label,u32 down,touchPosition t){return ui_button(x,y,w,32,label,UI_COL_SEL,down&KEY_TOUCH,t.px,t.py);}
static void error_screen(BiliApiResult r){while(live()){touchPosition t;u32 down=input(&t);ui_begin();ui_text_clipped(12,30,UI_SHARP,UI_COL_TEXT,"番剧信息读取失败",376);char code[96],number[24];numeric_i64(r.code,number,sizeof(number));snprintf(code,sizeof(code),"%s / code %s",r.outcome==BILI_API_REJECTED?"服务端拒绝":"结果未确认",number);ui_text_clipped(12,70,UI_SHARP,UI_COL_ACCENT,code,376);ui_text_clipped(12,110,UI_SHARP,UI_COL_DIM,bili_last_error(),376);ui_begin_bottom();bool back=button(12,160,296,"返回",down,t);ui_text(12,218,UI_SHARP,UI_COL_DIM,"A确认 B返回");ui_end();if(back||(down&(KEY_A|KEY_B)))return;}}
typedef struct {int64_t id;bool by_episode;PgcSeason season;BiliApiResult result;} SeasonJob;
static SeasonJob s_season;
static int season_job(void *arg){SeasonJob *j=arg;j->result=j->by_episode?pgc_episode_season(j->id,&j->season):pgc_season(j->id,&j->season);return j->result.outcome==BILI_API_SUCCESS?0:-1;}
static bool browse_season(int64_t id,bool by_episode,BrowseRunner run,PgcSeries *series,PgcEpisode *episode){
 if(!run||!series||!episode)return false;
 memset(series,0,sizeof(*series));memset(episode,0,sizeof(*episode));memset(&s_season,0,sizeof(s_season));s_season.id=id;s_season.by_episode=by_episode;
 int result=run(season_job,&s_season,"读取番剧与全部分集",true);
 if(result!=0||net_is_shutting_down()||aptShouldClose()){pgc_season_free(&s_season.season);if(result!=0&&!net_is_shutting_down()&&!aptShouldClose())error_screen(s_season.result);return false;}
 int focus=0;bool selected=false;PgcSeason *s=&s_season.season;
 if(by_episode)for(int i=0;i<s->count;i++)if(s->episodes[i].ep_id==id){focus=i;break;}
 while(live()){
 touchPosition t;u32 down=input(&t);if(down&KEY_B)break;
 if((down&KEY_DOWN)&&focus+1<s->count)focus++;
 if((down&KEY_UP)&&focus>0)focus--;
 if((down&KEY_L)&&focus>=4)focus-=4;
 if((down&KEY_R)&&focus+4<s->count)focus+=4;
 int first=(focus/4)*4,hit=-1;ui_begin();ui_text_clipped(12,20,UI_SHARP,UI_COL_TEXT,s->series.title,376);
 if(s->count){PgcEpisode *e=&s->episodes[focus];ui_text_clipped(12,64,UI_SHARP,UI_COL_ACCENT,e->group,376);ui_text_clipped(12,100,UI_SHARP,UI_COL_TEXT,e->title,376);}
 else ui_text(12,72,UI_SHARP,UI_COL_TEXT,"暂无可选分集");
 ui_text_clipped(12,160,UI_SHARP,UI_COL_DIM,s->truncated?"分集超出4096条，列表不完整":"",376);
 ui_begin_bottom();char label[240];
 for(int i=first;i<s->count&&i<first+4;i++){snprintf(label,sizeof(label),"[%.60s] %.175s",s->episodes[i].group,s->episodes[i].title);if(ui_button(10,12+(i-first)*35,300,31,label,i==focus?UI_COL_ACCENT:UI_COL_SEL,down&KEY_TOUCH,t.px,t.py))hit=i;}
 bool prev=button(10,158,90,"上4集",down,t),next=button(110,158,90,"下4集",down,t),back=button(210,158,100,"返回",down,t);ui_text(12,218,UI_SHARP,UI_COL_DIM,"上下选择 A打开 L/R翻页 B返回");ui_end();
 if(back)break;
 if(prev)focus=first>=4?first-4:0;
 if(next&&first+4<s->count)focus=first+4;
 if((down&KEY_A)&&s->count)hit=focus;
 if(hit>=0){*series=s->series;*episode=s->episodes[hit];selected=true;break;}
 }
 pgc_season_free(s);return selected;
}
bool pgc_browse_season(int64_t id,BrowseRunner run,PgcSeries *series,PgcEpisode *episode){return browse_season(id,false,run,series,episode);}
bool pgc_browse_episode(int64_t id,BrowseRunner run,PgcSeries *series,PgcEpisode *episode){return browse_season(id,true,run,series,episode);}
typedef struct {const char *keyword;int page;PgcSeries series[20];BiliPageInfo info;BiliApiResult result;} SearchJob;
static SearchJob s_search;
static int search_job(void *arg){SearchJob *j=arg;j->result=pgc_search(j->keyword,j->page,j->series,20,&j->info);return j->result.outcome==BILI_API_SUCCESS?0:-1;}
bool pgc_browse_search(const char *keyword,BrowseRunner run,PgcSeries *series,PgcEpisode *episode){
 if(!keyword||!run||!series||!episode)return false;
 memset(series,0,sizeof(*series));memset(episode,0,sizeof(*episode));memset(&s_search,0,sizeof(s_search));s_search.keyword=keyword;s_search.page=1;bool fetch=true;int focus=0;
 while(live()){
 if(fetch){int result=run(search_job,&s_search,"搜索番剧",true);if(result!=0||net_is_shutting_down()||aptShouldClose()){if(result!=0&&!net_is_shutting_down()&&!aptShouldClose())error_screen(s_search.result);return false;}fetch=false;focus=0;}
 touchPosition t;u32 down=input(&t);if(down&KEY_B)return false;
 if((down&KEY_DOWN)&&focus+1<s_search.info.count)focus++;
 if((down&KEY_UP)&&focus>0)focus--;
 int first=(focus/4)*4,hit=-1;char label[240];ui_begin();snprintf(label,sizeof(label),"番剧: %s / 第%d页",keyword,s_search.page);ui_text_clipped(12,24,UI_SHARP,UI_COL_TEXT,label,376);
 ui_text_clipped(12,80,UI_SHARP,UI_COL_ACCENT,s_search.info.count?s_search.series[focus].title:"没有番剧搜索结果",376);ui_begin_bottom();
 for(int i=first;i<s_search.info.count&&i<first+4;i++)if(ui_button(10,12+(i-first)*35,250,31,s_search.series[i].title,i==focus?UI_COL_ACCENT:UI_COL_SEL,down&KEY_TOUCH,t.px,t.py))hit=i;
 bool up=button(265,12,45,"上",down,t),dn=button(265,82,45,"下",down,t);bool prev=button(10,158,90,"上一页",down,t),next=button(110,158,90,"下一页",down,t),back=button(210,158,100,"返回",down,t);ui_text(12,218,UI_SHARP,UI_COL_DIM,"上下选择 A打开 L/R翻页 B返回");ui_end();
 if(back)return false;
 if(up)focus=first>=4?first-4:0;
 if(dn&&first+4<s_search.info.count)focus=first+4;
 if((prev||(down&KEY_L))&&s_search.page>1){s_search.page--;fetch=true;}
 if((next||(down&KEY_R))&&s_search.info.has_more){s_search.page++;fetch=true;}
 if((down&KEY_A)&&s_search.info.count)hit=focus;
 if(hit>=0&&pgc_browse_season(s_search.series[hit].season_id,run,series,episode))return true;
 }
 return false;
}




