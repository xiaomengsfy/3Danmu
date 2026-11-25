#include "pgc.h"
#include "bili_transport.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *response;static int calls;static bool signed_search;static bool episode_mode;static const char*expected_qn="16";
BiliApiResult bili_fetch_json(const char *p,const char **k,const char **v,int n,bool sign,HttpResponse *r){
 calls++;if(episode_mode){assert(!sign);if(strstr(p,"/pgc/view/")){assert(!strcmp(k[0],"ep_id"));assert(!strcmp(v[0],"11"));response="{\"code\":0,\"result\":{\"season_id\":9,\"episodes\":[{\"id\":11,\"cid\":33}]}}";}else{assert(!strcmp(k[0],"season_id"));assert(!strcmp(v[0],"9"));response="{\"code\":0,\"result\":{\"section\":[]}}";}}if(strstr(p,"search")){assert(n==3);assert(!strcmp(k[0],"search_type"));assert(!strcmp(v[0],"media_bangumi"));assert(!strcmp(v[2],"2"));signed_search=sign;}
 if(strstr(p,"playurl")){assert(n==4);assert(!strcmp(k[0],"ep_id"));assert(!strcmp(v[1],expected_qn));assert(!strcmp(v[2],"1"));assert(!strcmp(v[3],"0"));assert(!sign);}
 r->len=strlen(response);r->data=malloc(r->len+1);strcpy(r->data,response);r->status=200;return (BiliApiResult){.outcome=BILI_API_SUCCESS};}
void net_response_free(HttpResponse *r){free(r->data);memset(r,0,sizeof(*r));}
#define PARSE(fn,s,...) fn(s,strlen(s),__VA_ARGS__)
int main(void){
 PgcSeries a[2];BiliPageInfo info;PgcSeason s={0};PgcPlayback p;PgcEpisode e={.ep_id=11,.duration_ms=60000};
 const char *search="{\"code\":0,\"data\":{\"numResults\":21,\"pagesize\":20,\"result\":[{\"season_id\":9,\"title\":\"<em class=\\\"keyword\\\">Test</em>\",\"cover\":\"//image\"}]}}";
 assert(PARSE(pgc_parse_search,search,1,a,2,&info).outcome==BILI_API_SUCCESS);assert(info.count==1&&info.has_more&&a[0].season_id==9);assert(!strcmp(a[0].title,"Test"));
 response=search;assert(pgc_search("test",2,a,2,&info).outcome==BILI_API_SUCCESS);assert(signed_search);
 assert(PARSE(pgc_parse_season,"{\"code\":0,\"result\":{\"season_id\":9,\"title\":\"Series\",\"episodes\":[{\"id\":11,\"aid\":22,\"cid\":33,\"bvid\":\"BVtest\",\"title\":\"1\",\"long_title\":\"Pilot\",\"duration\":60000}]}}",&s).outcome==BILI_API_SUCCESS);assert(s.count==1&&s.episodes[0].ep_id==11&&s.episodes[0].aid==22&&s.episodes[0].cid==33);
 assert(PARSE(pgc_parse_sections,"{\"code\":0,\"result\":{\"main_section\":{\"episodes\":[{\"id\":11,\"cid\":33}]},\"section\":[{\"title\":\"Extra\",\"episodes\":[{\"id\":12,\"cid\":44}]}]}}",&s).outcome==BILI_API_SUCCESS);assert(s.count==2&&!s.episodes[0].extra&&s.episodes[1].extra&&!strcmp(s.episodes[1].group,"Extra"));pgc_season_free(&s);
 const char *full="{\"code\":0,\"result\":{\"is_preview\":0,\"is_drm\":false,\"timelength\":60000,\"video_codecid\":7,\"format\":\"mp4\",\"durl\":[{\"length\":60000,\"url\":\"https://cdn.example/full.mp4\"}]}}";
 assert(PARSE(pgc_parse_playurl,full,&e,&p).outcome==BILI_API_SUCCESS&&p.status==PGC_PLAY_FULL&&p.url[0]);response=full;assert(pgc_playurl(&e,&p).outcome==BILI_API_SUCCESS);
 assert(PARSE(pgc_parse_playurl,"{\"code\":0,\"result\":{\"is_preview\":1}}",&e,&p).outcome==BILI_API_SUCCESS&&p.status==PGC_PLAY_PREVIEW&&!p.url[0]);
 assert(PARSE(pgc_parse_playurl,"{\"code\":0,\"result\":{\"is_drm\":true}}",&e,&p).outcome==BILI_API_SUCCESS&&p.status==PGC_PLAY_DRM);
 assert(PARSE(pgc_parse_playurl,"{\"code\":0,\"result\":{\"durl\":[{},{}]}}",&e,&p).outcome==BILI_API_SUCCESS&&p.status==PGC_PLAY_MULTISEG);
 assert(PARSE(pgc_parse_playurl,"{\"code\":0,\"result\":{\"dash\":{\"video\":[{}],\"audio\":[{}]}}}",&e,&p).outcome==BILI_API_SUCCESS&&p.status==PGC_PLAY_DASH);
 assert(PARSE(pgc_parse_playurl,"{\"code\":-10403,\"message\":\"not authorized\"}",&e,&p).outcome==BILI_API_REJECTED&&!p.url[0]);
 assert(PARSE(pgc_parse_playurl,"{\"code\":0,}",&e,&p).outcome==BILI_API_UNKNOWN);
 assert(PARSE(pgc_parse_search,"{\"code\":0,\"data\":{\"result\":[],\"numResults\":0}}",1,a,2,&info).outcome==BILI_API_SUCCESS&&info.count==0&&!info.has_more);
 assert(PARSE(pgc_parse_playurl,"{\"code\":0,\"result\":{\"is_preview\":0,\"is_drm\":false,\"timelength\":10000,\"format\":\"mp4\",\"durl\":[{\"length\":10000,\"url\":\"https://cdn/x\"}]}}",&e,&p).outcome==BILI_API_SUCCESS&&p.status==PGC_PLAY_PREVIEW);
 assert(PARSE(pgc_parse_playurl,"{\"code\":0,\"result\":{\"nested\":{\"is_preview\":0,\"is_drm\":false},\"timelength\":60000,\"format\":\"mp4\",\"durl\":[{\"length\":60000,\"url\":\"https://cdn/x\"}]}}",&e,&p).outcome==BILI_API_SUCCESS&&p.status==PGC_PLAY_INVALID&&!p.url[0]);
 assert(PARSE(pgc_parse_playurl,"{\"code\":0,\"result\":{\"is_preview\":0,\"is_preview\":1,\"is_drm\":false,\"timelength\":60000,\"format\":\"mp4\",\"durl\":[{\"length\":60000,\"url\":\"https://cdn/x\"}]}}",&e,&p).outcome==BILI_API_SUCCESS&&p.status==PGC_PLAY_INVALID&&!p.url[0]);
 assert(PARSE(pgc_parse_playurl,"{\"code\":0,\"result\":{\"is_preview\":0,\"is_drm\":false,\"timelength\":\"60000oops\",\"format\":\"mp4\",\"durl\":[{\"length\":60000,\"url\":\"https://cdn/x\"}]}}",&e,&p).outcome==BILI_API_SUCCESS&&p.status==PGC_PLAY_INVALID&&!p.url[0]);
 assert(PARSE(pgc_parse_playurl,"{\"code\":0,\"result\":{\"is_preview\":0,\"\\u0069s_preview\":1,\"is_drm\":false,\"timelength\":60000,\"format\":\"mp4\",\"durl\":[{\"length\":60000,\"url\":\"https://cdn/x\"}]}}",&e,&p).outcome==BILI_API_SUCCESS&&p.status==PGC_PLAY_INVALID&&!p.url[0]);
 assert(PARSE(pgc_parse_playurl,"{\"code\":0,\"result\":{\"is_preview\":\"0\",\"is_drm\":\"false\",\"timelength\":60000,\"format\":\"mp4\",\"durl\":[{\"length\":60000,\"url\":\"https://cdn/x\"}]}}",&e,&p).outcome==BILI_API_SUCCESS&&p.status==PGC_PLAY_INVALID&&!p.url[0]);
 assert(calls==2);expected_qn="32";response=full;assert(pgc_playurl_quality(&e,32,&p).outcome==BILI_API_SUCCESS&&p.status==PGC_PLAY_FULL);
 const char*high="{\"code\":0,\"result\":{\"quality\":32,\"is_preview\":0,\"is_drm\":false,\"timelength\":60000,\"format\":\"mp4\",\"durl\":[{\"length\":60000,\"url\":\"https://cdn/x\"}]}}";
 assert(PARSE(pgc_parse_playurl,high,&e,&p).outcome==BILI_API_SUCCESS&&p.status==PGC_PLAY_FULL&&p.quality==32);
 int before=calls;assert(pgc_playurl_quality(&e,80,&p).outcome==BILI_API_UNKNOWN&&calls==before&&!p.url[0]);
 episode_mode=true;assert(pgc_episode_season(11,&s).outcome==BILI_API_SUCCESS&&s.series.season_id==9&&s.episodes[0].ep_id==11);pgc_season_free(&s);puts("PGC fixtures PASS");return 0;
}




