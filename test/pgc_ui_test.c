#include "pgc_ui.h"
#include "net.h"
#include <stdio.h>
#include "ui.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static int step,mode;
bool aptMainLoop(void){return step<12;}bool aptShouldClose(void){return false;}bool net_is_shutting_down(void){return false;}
void hidScanInput(void){step++;}u32 hidKeysDown(void){if(mode==1)return KEY_B;if(mode==2)return KEY_A;if(step==1)return KEY_DOWN;if(step==2)return KEY_A;return KEY_B;}u32 hidKeysHeld(void){return 0;}void hidTouchRead(touchPosition *t){memset(t,0,sizeof(*t));}
void ui_begin(void){}void ui_begin_bottom(void){}void ui_end(void){}void ui_text(float a,float b,float c,u32 d,const char *e){(void)a;(void)b;(void)c;(void)d;(void)e;}void ui_text_clipped(float a,float b,float c,u32 d,const char *e,float f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;}
bool ui_button(float a,float b,float c,float d,const char *e,u32 f,bool g,float h,float i){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;return false;}
BiliApiResult pgc_season(int64_t id,PgcSeason *s){memset(s,0,sizeof(*s));s->series.season_id=id;strcpy(s->series.title,"Show");s->episodes=calloc(2,sizeof(*s->episodes));s->count=2;s->episodes[0].ep_id=10;s->episodes[1].ep_id=20;return (BiliApiResult){.outcome=BILI_API_SUCCESS};}
BiliApiResult pgc_episode_season(int64_t id,PgcSeason *s){(void)id;return pgc_season(9,s);}
void pgc_season_free(PgcSeason *s){free(s->episodes);memset(s,0,sizeof(*s));}
BiliApiResult pgc_search(const char *k,int p,PgcSeries *s,int m,BiliPageInfo *i){(void)k;(void)p;(void)s;(void)m;memset(i,0,sizeof(*i));return (BiliApiResult){.outcome=BILI_API_SUCCESS};}
const char *bili_last_error(void){return "fixture";}
static int run(int (*fn)(void *),void *arg,const char *m,bool c){(void)m;(void)c;return fn(arg);}
int main(void){PgcSeries s;PgcEpisode e;assert(pgc_browse_season(9,run,&s,&e));assert(e.ep_id==20&&s.season_id==9);step=0;mode=1;assert(!pgc_browse_season(9,run,&s,&e)&&!e.ep_id);step=0;mode=2;assert(pgc_browse_episode(20,run,&s,&e)&&e.ep_id==20);puts("PGC UI fixtures PASS");}


