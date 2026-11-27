#include "api_response.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static BiliApiResult parse(const char*b,BiliVideo*v,int*n){return api_response_history(0,200,b,strlen(b),v,4,n);}
int main(void){BiliVideo v[4];int n=99;
 /* A collection season ID does not identify a licensed PGC season. */
 assert(parse("{\"code\":0,\"data\":[{\"title\":\"UGC collection\",\"bvid\":\"BVcollection\",\"aid\":7,\"season\":{\"season_id\":123}}]}",v,&n).outcome==BILI_API_SUCCESS&&n==1&&!v[0].is_pgc&&!v[0].season_id&&!v[0].ep_id);
 assert(parse("{\"code\":0,\"data\":[{\"title\":\"UGC collection\",\"bvid\":\"BVcollection\",\"season_id\":123,\"history\":{\"business\":\"archive\",\"epid\":456,\"oid\":7}}]}",v,&n).outcome==BILI_API_SUCCESS&&n==1&&!v[0].is_pgc&&!v[0].season_id&&!v[0].ep_id&&v[0].aid==7);
 assert(parse("{\"code\":0,\"data\":{\"list\":[{\"title\":\"PGC\",\"history\":{\"business\":\"pgc\",\"epid\":456},\"season_id\":123}]}}",v,&n).outcome==BILI_API_SUCCESS&&n==1&&v[0].is_pgc&&v[0].ep_id==456&&v[0].season_id==123);
 const char *old="{\"code\":0,\"data\":[{\"aid\":1,\"bvid\":\"BVpgc\",\"title\":\"Show\",\"redirect_url\":\"https://www.bilibili.com/bangumi/play/ep11\"}]}";
 assert(parse(old,v,&n).outcome==BILI_API_SUCCESS&&n==1&&v[0].is_pgc&&v[0].ep_id==11);
 assert(parse("{\"code\":0,\"data\":[{\"title\":\"First\",\"history\":{\"business\":\"pgc\",\"epid\":1,\"cid\":33},\"season\":{\"season_id\":9}}]}",v,&n).outcome==BILI_API_SUCCESS&&n==1&&v[0].ep_id==1&&v[0].cid==33&&v[0].season_id==9);
 assert(parse("{\"code\":0,\"data\":[{\"title\":\"Show\",\"bangumi\":{\"ep_id\":11,\"season_id\":9}}]}",v,&n).outcome==BILI_API_SUCCESS&&n==1&&v[0].is_pgc);
 assert(parse("{\"code\":0,\"data\":[{\"title\":\"Season\",\"redirect_url\":\"https://www.bilibili.com/bangumi/play/ss9?from=history\"}]}",v,&n).outcome==BILI_API_SUCCESS&&n==1&&v[0].season_id==9&&!v[0].ep_id);
 assert(parse("{\"code\":0,\"data\":[{\"title\":\"UGC\",\"bvid\":\"BVtest\",\"aid\":7,\"cid\":8,\"videos\":3,\"pic\":\"//img/a\"}]}",v,&n).outcome==BILI_API_SUCCESS&&n==1&&!v[0].is_pgc&&v[0].cid==8&&v[0].pages==3&&!strcmp(v[0].pic,"https://img/a"));
 assert(parse("{\"code\":0,\"data\":[{\"title\":\"Bad\",\"redirect_url\":\"https://evil.test/bangumi/play/ep999\"}]}",v,&n).outcome==BILI_API_SUCCESS&&n==0);
 assert(parse("{\"code\":0,\"data\":[{\"title\":\"Bad\",\"redirect_url\":\"https://www.bilibili.com/bangumi/play/ep11oops\"}]}",v,&n).outcome==BILI_API_SUCCESS&&n==0);
 assert(parse("{\"code\":0,\"data\":[]}",v,&n).outcome==BILI_API_SUCCESS&&n==0);
 assert(parse("{\"code\":-101}",v,&n).login_required&&n==0&&!v[0].ep_id);
 assert(parse("{\"code\":0,\"data\":{}}",v,&n).outcome==BILI_API_UNKNOWN&&n==0);
 assert(parse("{\"code\":0,}",v,&n).outcome==BILI_API_UNKNOWN&&n==0);
 puts("PASS history old redirect ep1/nestedPGC/season/noBV/UGC/URL boundaries/empty/login/malformed");return 0;}
