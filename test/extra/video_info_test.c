#include "video_info.h"
#include "bili_transport.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static int fetches;
static const char *fixture="{\"code\":0,\"data\":{\"bvid\":\"BV1234567890\",\"aid\":12,\"cid\":21,\"title\":\"main\",\"videos\":2,\"owner\":{\"mid\":8,\"name\":\"UP\"},\"ugc_season\":{\"id\":77,\"title\":\"set\",\"ep_count\":2,\"sections\":[{\"episodes\":[{\"aid\":12,\"cid\":21,\"title\":\"one\",\"bvid\":\"BV1234567890\",\"arc\":{\"videos\":2}},{\"aid\":12,\"cid\":22,\"title\":\"two\",\"bvid\":\"BV1234567890\"}]},{\"episodes\":[{\"aid\":12,\"cid\":21,\"title\":\"duplicate\",\"bvid\":\"BV1234567890\"}]}]}}}";
BiliApiResult bili_fetch_json(const char*p,const char**k,const char**v,int n,bool sign,HttpResponse*r){
 assert(!strcmp(p,"/x/web-interface/view") && n==1 && !strcmp(k[0],"bvid") && !strcmp(v[0],"BV1234567890") && !sign);
 fetches++;r->data=malloc(strlen(fixture)+1);strcpy(r->data,fixture);r->len=strlen(fixture);r->status=200;return (BiliApiResult){.outcome=BILI_API_SUCCESS};
}
void net_response_free(HttpResponse *r){free(r->data);memset(r,0,sizeof *r);}
int main(void){VideoInfo x;video_info_init(&x);
 BiliApiResult r=video_info_parse(0,200,fixture,strlen(fixture),&x);
 assert(r.outcome==BILI_API_SUCCESS);assert(x.video.owner_mid==8&&!strcmp(x.video.author,"UP")&&x.video.pages==2);
 assert(x.collection_id==77&&x.count==2&&x.raw_count==3&&x.duplicate_count==1&&!x.partial);
 assert(x.episodes[0].cid==21&&x.episodes[1].cid==22&&x.episodes[1].aid==12);
 assert(!strcmp(x.episodes[1].title,"two"));
 const char *bad="{\"code\":0,\"data\":{\"aid\":12garbage}}";
 r=video_info_parse(0,200,bad,strlen(bad),&x);assert(r.outcome==BILI_API_UNKNOWN&&!x.episodes&&x.count==0);
 r=video_info_parse(0,200,"{\"code\":-101}",13,&x);assert(r.outcome==BILI_API_REJECTED);
 BiliVideo seed={0};strcpy(seed.bvid,"BV1234567890");seed.cid=999;
 r=video_info_get(&seed,&x);assert(r.outcome==BILI_API_SUCCESS&&fetches==1&&x.video.cid==21);
 char *big=calloc(180000,1);size_t at=0;
 at+=(size_t)sprintf(big+at,"{\"code\":0,\"data\":{\"bvid\":\"BV1234567890\",\"aid\":12,\"cid\":21,\"title\":\"main\",\"ugc_season\":{\"id\":77,\"ep_count\":501,\"sections\":[{\"episodes\":[");
 for(int i=0;i<501;i++)at+=(size_t)sprintf(big+at,"%s{\"aid\":12,\"cid\":%d,\"bvid\":\"BV1234567890\",\"title\":\"ep\"}",i?",":"",i+1);
 at+=(size_t)sprintf(big+at,"]}]}}}");
 r=video_info_parse(0,200,big,at,&x);assert(r.outcome==BILI_API_SUCCESS&&x.count==500&&x.raw_count==501&&x.truncated&&x.partial&&x.episodes[499].cid==500);free(big);
 const char*partial="{\"code\":0,\"data\":{\"bvid\":\"BV1234567890\",\"aid\":12,\"cid\":21,\"title\":\"main\",\"ugc_season\":{\"id\":77,\"ep_count\":2,\"sections\":[{\"episodes\":[{\"aid\":12,\"cid\":\"22oops\",\"bvid\":\"BV1234567890\"}]}]}}}";
 r=video_info_parse(0,200,partial,strlen(partial),&x);assert(r.outcome==BILI_API_SUCCESS&&x.partial&&x.skipped_count==1&&x.count==0);
 r=video_info_parse(-1,200,fixture,strlen(fixture),&x);assert(r.outcome==BILI_API_UNKNOWN&&!x.episodes);
 video_info_free(&x);assert(!x.episodes&&x.count==0);
 puts("Video metadata/order/identity/envelope/GET PASS");}

