#include "video_info.h"
#include "bili_transport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
static const char *reply;
BiliApiResult bili_fetch_json(const char *p,const char **k,const char **v,int n,bool sign,HttpResponse *out){assert(!strcmp(p,"/x/web-interface/view")&&n==1&&!strcmp(k[0],"bvid")&&!strcmp(v[0],"BV1234567890")&&!sign);out->len=strlen(reply);out->data=malloc(out->len+1);strcpy(out->data,reply);out->status=200;return(BiliApiResult){.outcome=BILI_API_SUCCESS};}
void net_response_free(HttpResponse *r){free(r->data);memset(r,0,sizeof(*r));}
int main(void){VideoInfo x;video_info_init(&x);BiliVideo seed={.aid=12};strcpy(seed.bvid,"BV1234567890");
 reply="{\"code\":0,\"data\":{\"aid\":999,\"cid\":9991,\"bvid\":\"BV9999999999\",\"title\":\"wrong response\"}}";BiliApiResult r=video_info_get(&seed,&x);assert(r.outcome==BILI_API_UNKNOWN&&!x.video.aid);printf("wrong response getter outcome=%d bvid=%s aid=%d (expect UNKNOWN/cleared)\n",r.outcome,x.video.bvid,(int)x.video.aid);
 reply="{\"code\":0,\"data\":{\"aid\":999,\"cid\":9991,\"bvid\":\"BV1234567890\",\"title\":\"wrong aid\"}}";r=video_info_get(&seed,&x);assert(r.outcome==BILI_API_UNKNOWN&&!x.video.aid);printf("same bvid mismatched known aid outcome=%d aid=%d (expect UNKNOWN/cleared)\n",r.outcome,(int)x.video.aid);
 reply="{\"code\":0,\"data\":{\"aid\":12,\"cid\":21,\"bvid\":\"BV1234567890\",\"title\":\"root\",\"ugc_season\":{\"id\":77,\"ep_count\":1,\"sections\":[{\"episodes\":[{\"aid\":12,\"cid\":21,\"bvid\":\"BV1234567890\",\"title\":\"first\",\"arc\":{\"aid\":999,\"cid\":9991,\"bvid\":\"BV9999999999\",\"title\":\"wrong arc\"}}]}]}}}";
 r=video_info_parse(0,200,reply,strlen(reply),&x);assert(r.outcome!=BILI_API_SUCCESS||x.partial);printf("inconsistent row vs arc outcome=%d partial=%d count=%d episode aid=%d cid=%d bvid=%s (expect disabled/rejected)\n",r.outcome,x.partial,x.count,x.count?(int)x.episodes[0].aid:0,x.count?(int)x.episodes[0].cid:0,x.count?x.episodes[0].bvid:"");
 reply="{\"code\":0,\"data\":{\"aid\":12,\"cid\":21,\"bvid\":\"BV1234567890\",\"title\":\"root\",\"ugc_season\":{\"id\":77,\"ep_count\":1,\"sections\":[{\"episodes\":[{\"aid\":12,\"cid\":21,\"bvid\":\"BV1234567890\",\"title\":\"first\"},{\"aid\":12,\"cid\":21,\"bvid\":\"BV9999999999\",\"title\":\"conflicting duplicate\"}]}]}}}";
 r=video_info_parse(0,200,reply,strlen(reply),&x);assert(r.outcome!=BILI_API_SUCCESS||x.partial);printf("conflicting duplicate outcome=%d partial=%d count=%d duplicate_count=%d (expect disabled/rejected)\n",r.outcome,x.partial,x.count,x.duplicate_count);
 reply="{\"code\":0,\"data\":{\"aid\":12,\"aid\":999,\"cid\":21,\"bvid\":\"BV1234567890\",\"title\":\"ambiguous identity\"}}";r=video_info_parse(0,200,reply,strlen(reply),&x);assert(r.outcome==BILI_API_UNKNOWN&&!x.video.aid);printf("duplicate aid fields outcome=%d aid=%d (expect UNKNOWN/cleared)\n",r.outcome,(int)x.video.aid);
 reply="{\"code\":0,\"data\":{\"aid\":12,\"\\u0061id\":999,\"cid\":21,\"bvid\":\"BV1234567890\",\"title\":\"escaped duplicate\"}}";
 r=video_info_parse(0,200,reply,strlen(reply),&x);assert(r.outcome==BILI_API_UNKNOWN&&!x.video.aid);
 reply="{\"code\":0,\"data\":{\"aid\":12,\"cid\":21,\"bvid\":\"BV1234567890\",\"title\":\"root\",\"ugc_season\":{\"id\":77,\"ep_count\":1,\"sections\":[{\"episodes\":[{\"aid\":12,\"cid\":22,\"bvid\":\"BV1234567890\",\"title\":\"P2\",\"arc\":{\"aid\":12,\"cid\":21,\"bvid\":\"BV1234567890\"}}]}]}}}";
 r=video_info_parse(0,200,reply,strlen(reply),&x);assert(r.outcome==BILI_API_SUCCESS&&!x.partial&&x.count==1&&x.episodes[0].cid==22);
 puts("Identity binding five regressions + escaped duplicates + legitimate P2 PASS");
 video_info_free(&x);return 0;}
