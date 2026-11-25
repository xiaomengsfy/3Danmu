#include "video_actions_api.h"
#include "bili_transport.h"
#include "numeric_text.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int posts;static char adds[256],dels[256],multiply[8];
BiliApiResult bili_fetch_json(const char*a,const char**b,const char**c,int d,bool e,HttpResponse*f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return (BiliApiResult){.outcome=BILI_API_UNKNOWN};}
BiliApiResult bili_submit_json(const char*a,const char**k,const char**v,int n,HttpResponse*h){(void)a;(void)h;posts++;for(int i=0;i<n;i++){if(!strcmp(k[i],"add_media_ids"))snprintf(adds,sizeof(adds),"%s",v[i]);if(!strcmp(k[i],"del_media_ids"))snprintf(dels,sizeof(dels),"%s",v[i]);if(!strcmp(k[i],"multiply"))snprintf(multiply,sizeof(multiply),"%s",v[i]);}return (BiliApiResult){.outcome=BILI_API_UNKNOWN};}
void net_response_free(HttpResponse*r){(void)r;}
int64_t bili_account_id(void){return 1;}
#define PARSE(fn,s,...) fn(0,200,s,strlen(s),__VA_ARGS__)
int main(void){char digits[24];assert(numeric_i64(INT64_MIN,digits,sizeof(digits))&&!strcmp(digits,"-9223372036854775808"));assert(numeric_i64(INT64_MAX,digits,sizeof(digits))&&!strcmp(digits,"9223372036854775807"));assert(!numeric_i64(123,digits,2));int coins;const char*s="{\"code\":0,\"data\":{\"multiply\":2}}";assert(PARSE(video_parse_coins,s,&coins).outcome==BILI_API_SUCCESS&&coins==2);
s="{\"code\":0,\"data\":{\"multiply\":3}}";assert(PARSE(video_parse_coins,s,&coins).outcome==BILI_API_UNKNOWN&&coins==-1);
s="{\"code\":0,\"data\":{\"multiply\":1,\"multiply\":2}}";assert(PARSE(video_parse_coins,s,&coins).outcome==BILI_API_UNKNOWN);
BiliFavFolder f[3];BiliPageInfo info;s="{\"code\":0,\"data\":{\"count\":2,\"list\":[{\"id\":11,\"title\":\"A\",\"media_count\":3,\"fav_state\":1},{\"id\":22,\"title\":\"B\",\"media_count\":0,\"fav_state\":0}]}}";
assert(PARSE(video_parse_folders,s,f,3,&info).outcome==BILI_API_SUCCESS&&info.count==2&&f[0].contains&&!f[1].contains&&f[1].membership_known);
bool selected[]={false,true};assert(video_favorite_delta_once(99,f,selected,2).outcome==BILI_API_UNKNOWN&&posts==1&&!strcmp(adds,"22")&&!strcmp(dels,"11"));
selected[0]=true;selected[1]=false;assert(video_favorite_delta_once(99,f,selected,2).outcome==BILI_API_SUCCESS&&posts==1);
f[0].membership_known=false;assert(video_favorite_delta_once(99,f,selected,2).outcome==BILI_API_UNKNOWN&&posts==1);
assert(video_coin_once(99,3,false).outcome==BILI_API_UNKNOWN&&posts==1);assert(video_coin_once(99,2,true).outcome==BILI_API_UNKNOWN&&posts==2&&!strcmp(multiply,"2"));
s="{\"code\":0,\"data\":{\"count\":1,\"list\":[{\"id\":11,\"title\":\"A\",\"media_count\":3}]}}";assert(PARSE(video_parse_folders,s,f,3,&info).outcome==BILI_API_UNKNOWN&&info.count==0);
puts("video action fixtures PASS");return 0;}
