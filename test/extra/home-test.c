#include <stdio.h>
#include <string.h>
#include "bili.h"
#include "browse_ui.h"
#undef printf
static BiliVideo s_list[20];
static int s_sel,s_count,s_page,s_mode,s_busy_minimal,s_job_cancelled;
static int64_t s_list_account=123,owner=123;
static char s_busy[192];
enum {MODE_RECOMMEND,MODE_WATCHLATER};
static int loads,restores,modals,stops,load_input_count;
static bool attempted,shutdown_flag,empty_first,load_fail,s_list_failed;
static int resolve;
static void thumb_stop(void){stops++;}
static int browse_run(int(*fn)(void*),void *arg,const char *m,bool c){(void)m;(void)c;return fn(arg);}
static bool login_cb(void){return true;}
bool net_is_shutting_down(void){return shutdown_flag;}
bool aptShouldClose(void){return false;}
int64_t bili_account_id(void){return owner;}
const char *bili_last_error(void){return "synthetic failure";}
int bili_get_cid(const char *b,int64_t *cid,int64_t *aid){(void)b;*cid=1;*aid=99;return resolve;}
bool browse_watchlater_add(const BiliVideo *v,BrowseRunner run,bool(*login)(void)){
 (void)run;(void)login;modals++;if(v->aid!=99)return false;return attempted;
}
static void restore_browse(void){restores++;s_busy_minimal=false;}
static void load_list(void){loads++;s_list_failed=load_fail;load_input_count=s_count;if(!load_fail)s_count=empty_first&&loads==1?0:5;}
#include "home-extracted.c"
static int checks,failures;
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)
static void reset(void){memset(s_list,0,sizeof s_list);s_list[0].aid=99;s_sel=0;s_count=5;s_page=2;s_mode=MODE_WATCHLATER;s_list_account=owner=123;loads=restores=modals=stops=0;attempted=true;shutdown_flag=empty_first=load_fail=s_list_failed=false;s_busy_minimal=s_job_cancelled=0;resolve=0;}
int main(void){
 reset();home_watchlater();CHECK(modals==1 && stops==1 && loads==1 && s_page==2 && !load_input_count && !s_busy_minimal);
 reset();empty_first=true;home_watchlater();CHECK(loads==2 && s_page==1 && s_count==5);
 reset();load_fail=true;home_watchlater();CHECK(loads==1 && !s_count);
 reset();attempted=false;home_watchlater();CHECK(!loads && restores==1);
 reset();s_mode=MODE_RECOMMEND;home_watchlater();CHECK(!loads && restores==1);
 reset();owner=456;home_watchlater();CHECK(!loads && restores==1);
 reset();shutdown_flag=true;home_watchlater();CHECK(!modals && !loads && restores==1);
 reset();s_list[0].aid=0;resolve=-1;home_watchlater();CHECK(!modals && !loads && restores==1);
 reset();s_sel=-1;home_watchlater();CHECK(!stops && !modals && !loads);
 printf("Watch Later home: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
