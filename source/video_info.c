#include "video_info.h"
#include "bili_transport.h"
#include "api_response.h"
#include "jsonx.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#define JSMN_STATIC
#include "vendor/jsmn.h"
static BiliApiResult invalid(void){return (BiliApiResult){.outcome=BILI_API_UNKNOWN};}
void video_info_init(VideoInfo*x){if(x){memset(x,0,sizeof*x);x->expected_count=-1;x->video.views=-1;}}
void video_info_free(VideoInfo*x){if(x){free(x->episodes);video_info_init(x);}}
static int64_t number(Json*j,int root,const char *key){
 char text[64];int64_t n;double d;
 if(json_get_str(j,root,key,text,sizeof text)){
  size_t len=strlen(text);if(!len||len>=sizeof(text)-1)return -1;
  for(size_t i=0;i<len;i++)if(text[i]<'0'||text[i]>'9')return -1;
  errno=0;char*end;long long value=strtoll(text,&end,10);
  return errno||*end?-1:(int64_t)value;
 }
 if(!json_get_int(j,root,key,&n)||!json_get_num(j,root,key,&d)||n<0||d>9007199254740991.0||(double)n!=d)return -1;
 return n;
}
/* jsonx preserves token indices; reject decoded duplicate keys before any
 * identity/schema lookup can silently choose one value. */
static int token_skip(const jsmntok_t*t,int count,int at){
 int end=t[at].end;at++;while(at<count&&t[at].start<end)at++;return at;
}
static bool unique_keys(const char*b,size_t n,Json*j){
 jsmn_parser parser;jsmn_init(&parser);int count=jsmn_parse(&parser,b,n,NULL,0);
 if(count<1||count>65536)return false;
 jsmntok_t*t=malloc((size_t)count*sizeof *t);if(!t)return false;
 jsmn_init(&parser);bool ok=jsmn_parse(&parser,b,n,t,(unsigned)count)==count;
 for(int obj=0;obj<count&&ok;obj++)if(t[obj].type==JSMN_OBJECT){
  for(int a=obj+1;a<count&&t[a].start<t[obj].end&&ok;a=token_skip(t,count,a+1)){
   char key[256];if(t[a].type!=JSMN_STRING||t[a].end-t[a].start>=255||
     !json_get_str(j,a,"",key,sizeof key)){ok=false;break;}
   for(int prior=obj+1;prior<a;prior=token_skip(t,count,prior+1)){
    char previous[256];if(!json_get_str(j,prior,"",previous,sizeof previous)||!strcmp(key,previous)){ok=false;break;}
   }
  }
 }
 free(t);return ok;
}
static int bounded(int64_t n){return n>=0&&n<=INT_MAX?(int)n:0;}
static bool bvid_ok(const char*s){if(strlen(s)!=12||s[0]!='B'||s[1]!='V')return false;for(int i=2;i<12;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='A'&&s[i]<='Z')||(s[i]>='a'&&s[i]<='z')))return false;return true;}
static void metadata(Json*j,int root,BiliVideo*v){
 memset(v,0,sizeof*v);v->views=-1;
 v->aid=number(j,root,"aid");v->cid=number(j,root,"cid");
 if(v->aid<0)v->aid=0;
 if(v->cid<0)v->cid=0;
 json_get_str(j,root,"bvid",v->bvid,sizeof v->bvid);
 json_get_str(j,root,"title",v->title,sizeof v->title);
 json_get_str(j,root,"pic",v->pic,sizeof v->pic);
 v->owner_mid=number(j,root,"owner.mid");
 json_get_str(j,root,"owner.name",v->author,sizeof v->author);
 if(v->owner_mid<=0){v->owner_mid=number(j,root,"author.mid");json_get_str(j,root,"author.name",v->author,sizeof v->author);}
 if(v->owner_mid<0)v->owner_mid=0;
 v->duration=bounded(number(j,root,"duration"));v->pages=bounded(number(j,root,"videos"));
 if(v->pages==0){int a=json_find(j,root,"pages");int count=json_arr_len(j,a);if(count>0)v->pages=count;}
 v->views=number(j,root,"stat.view");
}
static bool append_episode(Json*j,int row,VideoInfo*x){
 x->raw_count++;
 int64_t aid=number(j,row,"aid"),cid=number(j,row,"cid");
 if(aid<=0||cid<=0){x->partial=true;x->skipped_count++;return true;}
 BiliVideo v;int arc=json_find(j,row,"arc");metadata(j,arc>=0?arc:row,&v);
 char row_bvid[16]={0};json_get_str(j,row,"bvid",row_bvid,sizeof row_bvid);
 /* CID differs legitimately when the collection selects a non-first P.
  * Archive identity must still agree before episode identity is applied. */
 if((json_find(j,row,"bvid")>=0&&!bvid_ok(row_bvid))||
    (arc>=0&&json_find(j,arc,"bvid")>=0&&!bvid_ok(v.bvid))||
    (arc>=0&&json_find(j,arc,"aid")>=0&&(v.aid<=0||v.aid!=aid))||
    (row_bvid[0]&&v.bvid[0]&&strcmp(row_bvid,v.bvid))){x->partial=true;x->skipped_count++;return true;}
 v.aid=aid;v.cid=cid;
 json_get_str(j,row,"title",v.title,sizeof v.title);
 if(!v.bvid[0])memcpy(v.bvid,row_bvid,sizeof v.bvid);
 if(!bvid_ok(v.bvid)||(aid==x->video.aid&&strcmp(v.bvid,x->video.bvid))){x->partial=true;x->skipped_count++;return true;}
 for(int k=0;k<x->count;k++)if(x->episodes[k].aid==aid&&x->episodes[k].cid==cid){
  if(strcmp(x->episodes[k].bvid,v.bvid)){x->partial=true;x->skipped_count++;}
  else x->duplicate_count++;
  return true;
 }
 if(x->count>=VIDEO_INFO_EPISODE_LIMIT){x->truncated=x->partial=true;return true;}
 BiliVideo*next=realloc(x->episodes,(size_t)(x->count+1)*sizeof *next);
 if(!next)return false;
 x->episodes=next;next[x->count++]=v;return true;
}
BiliApiResult video_info_parse(int t,int status,const char*b,size_t n,VideoInfo*x){
 if(!x)return invalid();
 video_info_free(x);
 BiliApiResult r=api_response_result(t,status,b,n);
 if(r.outcome!=BILI_API_SUCCESS)return r;
 Json*j=json_parse(b,n);if(!j)return invalid();
 if(!unique_keys(b,n,j)){json_free(j);return invalid();}
 int data=json_find(j,-1,"data");bool ok=data>=0;
 if(ok){metadata(j,data,&x->video);ok=x->video.aid>0&&x->video.cid>0&&bvid_ok(x->video.bvid)&&x->video.title[0];}
 int season=ok?json_find(j,data,"ugc_season"):-1;
 if(ok&&season>=0){
  x->collection_id=number(j,season,"id");json_get_str(j,season,"title",x->collection_title,sizeof x->collection_title);
  x->expected_count=number(j,season,"ep_count");
  int sections=json_find(j,season,"sections"),count=json_arr_len(j,sections);
  if(x->collection_id<=0){x->collection_id=0;x->partial=true;}
  if(count<0)x->partial=true;
  for(int i=0;i<count&&ok;i++){
   int section=json_arr_at(j,sections,i),arr=json_find(j,section,"episodes"),nrows=json_arr_len(j,arr);
   if(nrows<0){x->partial=true;continue;}
   for(int k=0;k<nrows&&ok;k++)ok=append_episode(j,json_arr_at(j,arr,k),x);
  }
  if(x->expected_count>=0&&x->expected_count!=x->count)x->partial=true;
  if(x->count==0)x->partial=true;
 }
 json_free(j);
 if(!ok){video_info_free(x);return invalid();}
 return r;
}
BiliApiResult video_info_get(const BiliVideo*seed,VideoInfo*x){
 if(!x)return invalid();
 BiliVideo copy={0};if(seed)copy=*seed;
 video_info_free(x);
 if(!seed||!bvid_ok(copy.bvid))return invalid();
 const char*keys[]={"bvid"},*vals[]={copy.bvid};HttpResponse response={0};
 BiliApiResult r=bili_fetch_json("/x/web-interface/view",keys,vals,1,false,&response);
 if(r.outcome==BILI_API_SUCCESS)r=video_info_parse(0,response.status,response.data,response.len,x);
 if(r.outcome==BILI_API_SUCCESS&&(strcmp(copy.bvid,x->video.bvid)||(copy.aid>0&&copy.aid!=x->video.aid))){video_info_free(x);r=invalid();}
 net_response_free(&response);return r;
}
