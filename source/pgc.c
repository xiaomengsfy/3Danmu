#include "numeric_text.h"
#include "pgc.h"
#include "bili_transport.h"
#include "api_response.h"
#include "jsonx.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <errno.h>
#define PGC_EP_LIMIT 4096
static BiliApiResult invalid(void){return (BiliApiResult){.outcome=BILI_API_UNKNOWN};}
static Json *open_json(const char *b,size_t n,BiliApiResult *r){
 *r=api_response_result(0,200,b,n);
 if(r->outcome!=BILI_API_SUCCESS)return NULL;
 Json *j=json_parse(b,n);
 if(!j)*r=invalid();
 return j;
}
/* Identity/timing strings must be complete decimal integers; jsonx itself
 * accepts numeric prefixes, so it is not sufficient for completeness checks. */
static int64_t num(Json *j,int root,const char *key){
 char str[64];int64_t n=0;double d=0;
 if(json_get_str(j,root,key,str,sizeof(str))){
  if(!str[0])return 0;
  for(size_t i=0;str[i];i++)if(str[i]<'0'||str[i]>'9')return 0;
  errno=0;char *end=NULL;long long v=strtoll(str,&end,10);
  return errno||!end||*end?0:(int64_t)v;
 }
 if(!json_get_int(j,root,key,&n)||!json_get_num(j,root,key,&d)||(double)n!=d)return 0;
 return n;
}static void text(Json *j,int root,const char *key,char *out,size_t n){out[0]=0;json_get_str(j,root,key,out,n);}
static void strip(char *s){char *d=s;bool tag=false;for(char *p=s;*p;p++){if(*p=='<'){tag=true;continue;}if(*p=='>'){tag=false;continue;}if(!tag)*d++=*p;}*d=0;}
BiliApiResult pgc_parse_search(const char *b,size_t n,int page,PgcSeries *out,int max,BiliPageInfo *info){
 if(info)memset(info,0,sizeof(*info));
 if(out&&max>0)memset(out,0,(size_t)max*sizeof(*out));
 if(!out||max<1||!info||page<1)return invalid();
 BiliApiResult r;Json *j=open_json(b,n,&r);
 if(!j)return r;
 int data=json_find(j,-1,"data"),arr=json_find(j,data,"result");int count=json_arr_len(j,arr);
 if(data<0||arr<0||count<0){json_free(j);return invalid();}
 info->total=num(j,data,"numResults");int64_t size=num(j,data,"pagesize");
 if(size<1)size=20;
 info->has_more=(int64_t)page*size<info->total;
 for(int i=0;i<count;i++){int row=json_arr_at(j,arr,i);int64_t id=num(j,row,"season_id");
 if(id<=0)continue;
 if(info->count>=max){info->truncated=true;continue;}
 PgcSeries *s=&out[info->count++];s->season_id=id;text(j,row,"title",s->title,sizeof(s->title));strip(s->title);text(j,row,"cover",s->cover,sizeof(s->cover));}
 json_free(j);
 return r;
}
void pgc_season_free(PgcSeason *s){if(s){free(s->episodes);memset(s,0,sizeof(*s));}}
static bool append(Json *j,int arr,const char *group,bool extra,PgcSeason *s){
 int count=json_arr_len(j,arr);
 if(count<0)return false;
 for(int i=0;i<count;i++){
 int row=json_arr_at(j,arr,i);int64_t id=num(j,row,"id");
 if(id<=0)id=num(j,row,"ep_id");
 if(id<=0)continue;
 bool duplicate=false;for(int k=0;k<s->count;k++)if(s->episodes[k].ep_id==id){duplicate=true;break;}if(duplicate)continue;
 if(s->count>=PGC_EP_LIMIT){s->truncated=true;continue;}
 PgcEpisode *next=realloc(s->episodes,(size_t)(s->count+1)*sizeof(*next));
 if(!next)return false;
 s->episodes=next;
 PgcEpisode *e=&next[s->count++];memset(e,0,sizeof(*e));e->extra=extra;e->season_id=s->series.season_id;e->ep_id=id;e->aid=num(j,row,"aid");e->cid=num(j,row,"cid");e->duration_ms=num(j,row,"duration");
 text(j,row,"bvid",e->bvid,sizeof(e->bvid));char short_title[96],long_title[160];text(j,row,"title",short_title,sizeof(short_title));text(j,row,"long_title",long_title,sizeof(long_title));snprintf(e->title,sizeof(e->title),"%.95s%s%.103s",short_title,long_title[0]?" ":"",long_title);snprintf(e->group,sizeof(e->group),"%s",group);
 }return true;
}
BiliApiResult pgc_parse_season(const char *b,size_t n,PgcSeason *s){
 if(!s)return invalid();
 memset(s,0,sizeof(*s));BiliApiResult r;Json *j=open_json(b,n,&r);
 if(!j)return r;
 int root=json_find(j,-1,"result");
 if(root<0){json_free(j);return invalid();}
 s->series.season_id=num(j,root,"season_id");text(j,root,"title",s->series.title,sizeof(s->series.title));text(j,root,"cover",s->series.cover,sizeof(s->series.cover));
 int arr=json_find(j,root,"episodes");
 if(s->series.season_id<=0||arr<0||!append(j,arr,"正片",false,s)){pgc_season_free(s);r=invalid();}
 json_free(j);
 return r;
}
BiliApiResult pgc_parse_sections(const char *b,size_t n,PgcSeason *s){
 if(!s||s->series.season_id<=0)return invalid();
 BiliApiResult r;Json *j=open_json(b,n,&r);
 if(!j)return r;
 int root=json_find(j,-1,"result"),main=json_find(j,root,"main_section.episodes"),groups=json_find(j,root,"section");bool ok=root>=0&&(main>=0||groups>=0)&&(groups<0||json_arr_len(j,groups)>=0);
 if(main>=0)ok=ok&&append(j,main,"正片",false,s);
 for(int i=0;i<json_arr_len(j,groups)&&ok;i++){int g=json_arr_at(j,groups,i);char title[96];text(j,g,"title",title,sizeof(title));int arr=json_find(j,g,"episodes");ok=arr>=0&&append(j,arr,title[0]?title:"番外",true,s);}
 json_free(j);return ok?r:invalid();
}
/* Preview/DRM flags can be JSON booleans or integers. False must be
 * explicitly present: absent is unknown, never permission to play. */
/* jsonx has no boolean getter; strict envelope pass already guarantees valid
 * grammar. Token-safe flag extraction uses jsmn below rather than substring. */
#define JSMN_PARENT_LINKS
#define JSMN_STATIC
#include "vendor/jsmn.h"
/* Required keys are ASCII, but JSON may encode their letters with \uXXXX.
 * Compare decoded codepoints so escaped aliases remain duplicate keys. */
static bool key_equal(const char *b,const jsmntok_t *t,const char *key){
 if(t->type!=JSMN_STRING)return false;
 size_t k=0;
 for(int i=t->start;i<t->end;i++){
  unsigned value=(unsigned char)b[i];
  if(value=='\\'){
   if(++i>=t->end||b[i]!='u'||i+4>=t->end)return false;
   value=0;for(int n=0;n<4;n++){unsigned char c=(unsigned char)b[++i];unsigned digit=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c-'A'+10;value=value*16+digit;}
  }
  if(!key[k]||value!=(unsigned char)key[k++])return false;
 }
 return key[k]==0;
}static int boolflag(const char *b,size_t n,const char *key){
 jsmn_parser p;
 jsmn_init(&p);int count=jsmn_parse(&p,b,n,NULL,0);
 if(count<1)return -1;
 jsmntok_t *t=malloc((size_t)count*sizeof(*t));
 if(!t)return -1;
 jsmn_init(&p);int got=jsmn_parse(&p,b,n,t,(unsigned)count),value=-1,matches=0;
 /* Only direct result children; nested flags cannot authorize the response. */
 int result=-1,result_matches=0;
 for(int i=1;i+1<got;i++)if(t[i].parent==0&&key_equal(b,&t[i],"result")){result=i+1;result_matches++;}
 if(result_matches!=1||t[result].type!=JSMN_OBJECT){free(t);return -1;}
 for(int i=1;i+1<got;i++)if(t[i].parent==result&&key_equal(b,&t[i],key)){
  if(++matches>1){value=-1;break;}
  if(t[i+1].type!=JSMN_PRIMITIVE){value=-1;continue;}
  int len=t[i+1].end-t[i+1].start;const char *v=b+t[i+1].start;
  if((len==5&&!memcmp(v,"false",5))||(len==1&&*v=='0'))value=0;
  else if((len==4&&!memcmp(v,"true",4))||(len==1&&*v=='1'))value=1;
 } free(t);return value;
}
BiliApiResult pgc_parse_playurl(const char *b,size_t n,const PgcEpisode *e,PgcPlayback *out){
 if(!out)return invalid();
 memset(out,0,sizeof(*out));
 if(!e||e->ep_id<=0)return invalid();
 BiliApiResult r;Json *j=open_json(b,n,&r);
 if(!j)return r;
 int root=json_find(j,-1,"result");
 if(root<0){json_free(j);return invalid();}
 int preview=boolflag(b,n,"is_preview"),drm=boolflag(b,n,"is_drm");
 if(preview==1){out->status=PGC_PLAY_PREVIEW;goto done;}if(drm==1){out->status=PGC_PLAY_DRM;goto done;}
 int arr=json_find(j,root,"durl"),count=json_arr_len(j,arr);
 if(count>1){out->status=PGC_PLAY_MULTISEG;goto done;}if(count!=1){out->status=json_find(j,root,"dash")>=0?PGC_PLAY_DASH:PGC_PLAY_UNSUPPORTED;goto done;}
 if(preview<0||drm<0)goto done;
 int row=json_arr_at(j,arr,0);int64_t total=num(j,root,"timelength"),length=num(j,row,"length");
 if(total<=0||length<=0||total>INT_MAX||length>INT_MAX||llabs(total-length)>1500)goto done;
 if(e->duration_ms>0&&total+1500<e->duration_ms){out->status=PGC_PLAY_PREVIEW;goto done;}
 char format[64];text(j,root,"format",format,sizeof(format));int64_t codec=num(j,root,"video_codecid");
 if(strncmp(format,"mp4",3)||(codec!=0&&codec!=7)){out->status=PGC_PLAY_UNSUPPORTED;goto done;}
 char url[4096];text(j,row,"url",url,sizeof(url));size_t len=strlen(url);
 if(len<9||len>=sizeof(out->url)||strncmp(url,"https://",8))goto done;
 memcpy(out->url,url,len+1);out->duration_ms=total;
 int64_t quality=num(j,root,"quality");out->quality=quality==16||quality==32?(int)quality:0;
 out->status=PGC_PLAY_FULL;
 done:json_free(j);
 return r;
}
BiliApiResult pgc_search(const char *keyword,int page,PgcSeries *out,int max,BiliPageInfo *info){
 if(info)memset(info,0,sizeof(*info));
 if(out&&max>0)memset(out,0,(size_t)max*sizeof(*out));
 if(!keyword||!*keyword||page<1)return invalid();
 char p[24];snprintf(p,sizeof(p),"%d",page);const char *k[]={"search_type","keyword","page"},*v[]={"media_bangumi",keyword,p};HttpResponse h={0};BiliApiResult r=bili_fetch_json("/x/web-interface/wbi/search/type",k,v,3,true,&h);
 if(r.outcome==BILI_API_SUCCESS)r=pgc_parse_search(h.data,h.len,page,out,max,info);
 net_response_free(&h);
 return r;
}
static BiliApiResult load_season(int64_t id,bool by_episode,PgcSeason *out){
 if(!out)return invalid();
 memset(out,0,sizeof(*out));
 if(id<=0)return invalid();
 char sid[24];numeric_i64(id,sid,sizeof(sid));
 const char *k[]={by_episode?"ep_id":"season_id"},*v[]={sid};HttpResponse h={0};
 BiliApiResult r=bili_fetch_json("/pgc/view/web/season",k,v,1,false,&h);
 if(r.outcome==BILI_API_SUCCESS)r=pgc_parse_season(h.data,h.len,out);
 net_response_free(&h);
 if(r.outcome!=BILI_API_SUCCESS)return r;
 if(!by_episode&&out->series.season_id!=id){pgc_season_free(out);return invalid();}
 numeric_i64(out->series.season_id,sid,sizeof(sid));k[0]="season_id";
 r=bili_fetch_json("/pgc/web/season/section",k,v,1,false,&h);
 if(r.outcome==BILI_API_SUCCESS)r=pgc_parse_sections(h.data,h.len,out);
 net_response_free(&h);
 if(r.outcome!=BILI_API_SUCCESS){pgc_season_free(out);return r;}
 if(by_episode){bool found=false;for(int i=0;i<out->count;i++)if(out->episodes[i].ep_id==id)found=true;
  if(!found){pgc_season_free(out);return invalid();}}
 return r;
}
BiliApiResult pgc_season(int64_t id,PgcSeason *out){return load_season(id,false,out);}
BiliApiResult pgc_episode_season(int64_t id,PgcSeason *out){return load_season(id,true,out);}
BiliApiResult pgc_playurl(const PgcEpisode *e,PgcPlayback *out){return pgc_playurl_quality(e,16,out);}
BiliApiResult pgc_playurl_quality(const PgcEpisode *e,int qn,PgcPlayback *out){
 if(out)memset(out,0,sizeof(*out));
 if(!e||!out||e->ep_id<=0||(qn!=16&&qn!=32))return invalid();
 char id[24];numeric_i64(e->ep_id,id,sizeof(id));const char *k[]={"ep_id","qn","fnval","fourk"},*v[]={id,qn==32?"32":"16","1","0"};HttpResponse h={0};BiliApiResult r=bili_fetch_json("/pgc/player/web/playurl",k,v,4,false,&h);
 if(r.outcome==BILI_API_SUCCESS)r=pgc_parse_playurl(h.data,h.len,e,out);
 net_response_free(&h);
 return r;
}
const char *pgc_play_message(PgcPlayStatus s){switch(s){case PGC_PLAY_FULL:return "完整兼容播放地址已确认";case PGC_PLAY_PREVIEW:return "仅返回试看或时长不完整，无法播放全集";case PGC_PLAY_DRM:return "DRM 内容不受支持";case PGC_PLAY_MULTISEG:return "多分段完整播放暂不支持";case PGC_PLAY_DASH:return "独立音视频 DASH 播放暂不支持";case PGC_PLAY_UNSUPPORTED:return "服务端未返回兼容的完整 MP4";default:return "播放完整性或授权标记无法确认";}}









