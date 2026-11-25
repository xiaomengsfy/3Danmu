#include "numeric_text.h"
#include "favorite_queue.h"
#include "bili_transport.h"
#include "api_response.h"
#include "jsonx.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <errno.h>
#define JSMN_STATIC
#include "vendor/jsmn.h"
typedef struct {const char *b;jsmntok_t *t;int n;Json *j;} Doc;
static BiliApiResult unknown(void){return (BiliApiResult){.outcome=BILI_API_UNKNOWN};}
static int skip(Doc *d,int i){int end=d->t[i].end;i++;while(i<d->n&&d->t[i].start<end)i++;return i;}
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
}
/* Duplicate fields are malformed, including duplicate has_more/type/ids. */
static int field(Doc *d,int obj,const char *key){if(obj<0||obj>=d->n||d->t[obj].type!=JSMN_OBJECT)return -1;int found=-1;for(int i=obj+1;i<d->n&&d->t[i].start<d->t[obj].end;){int val=i+1;if(val>=d->n)return -2;if(key_equal(d->b,&d->t[i],key)){if(found>=0)return -2;found=val;}i=skip(d,val);}return found;}
static bool integer(Doc *d,int t,int64_t *out){if(t<0||t>=d->n||(d->t[t].type!=JSMN_PRIMITIVE&&d->t[t].type!=JSMN_STRING))return false;int len=d->t[t].end-d->t[t].start;if(len<1||len>20)return false;char s[24];memcpy(s,d->b+d->t[t].start,(size_t)len);s[len]=0;int i=s[0]=='-'?1:0;if(i==len)return false;for(;i<len;i++)if(s[i]<'0'||s[i]>'9')return false;errno=0;char *end;long long value=strtoll(s,&end,10);if(errno||*end)return false;*out=value;return true;}
static bool optional_number(Doc *d,int root,const char *key,int64_t *n){int t=field(d,root,key);return t==-1||integer(d,t,n);}
static bool string(Doc *d,int root,const char *key,char *out,size_t size,bool optional){int t=field(d,root,key);if(t==-1)return optional;if(t<0||d->t[t].type!=JSMN_STRING)return false;return json_get_str(d->j,root,key,out,size);}
static bool boolean(Doc *d,int t,bool *out){if(t<0||d->t[t].type!=JSMN_PRIMITIVE)return false;int len=d->t[t].end-d->t[t].start;const char *b=d->b+d->t[t].start;if(len==4&&!memcmp(b,"true",4)){*out=true;return true;}if(len==5&&!memcmp(b,"false",5)){*out=false;return true;}return false;}
static bool null_value(Doc *d,int token){return token>=0&&d->t[token].type==JSMN_PRIMITIVE&&d->t[token].end-d->t[token].start==4&&!memcmp(d->b+d->t[token].start,"null",4);}
static bool optional_text(Doc *d,int row,const char *key,char *out,size_t size){
 int token=field(d,row,key);if(token==-1||null_value(d,token))return true;
 return string(d,row,key,out,size,false);
}
static bool entry(Doc *d,int row,FqEntry *out){
 if(row<0||d->t[row].type!=JSMN_OBJECT)return false;
 int64_t type=0,attr=0,n=0;BiliVideo *v=&out->video;v->views=-1;
 if(!integer(d,field(d,row,"type"),&type))return false;
 if(type!=2)return true;
 if(!optional_number(d,row,"attr",&attr)||attr<0)return false;
 if(attr&1)return true;
 int id=field(d,row,"id");if(id!=-1&&!null_value(d,id)&&!integer(d,id,&v->aid))return false;
 if(!optional_text(d,row,"bvid",v->bvid,sizeof(v->bvid))||!optional_text(d,row,"title",v->title,sizeof(v->title)))return false;
 if(v->aid<=0||!v->bvid[0]||!v->title[0])return true;
 if(strncmp(v->bvid,"BV",2)||strlen(v->bvid)<3)return false;
 int bv=field(d,row,"bvid");if(d->t[bv].end-d->t[bv].start>=(int)sizeof(v->bvid))return false;
 /* Playback identity is mandatory. Display-only metadata is best effort:
  * removed resources/API variants may leave these fields null or absent. */
 optional_text(d,row,"cover",v->pic,sizeof(v->pic));
 if(integer(d,field(d,row,"duration"),&n)&&n>=0&&n<=INT_MAX)v->duration=(int)n;
 if(integer(d,field(d,row,"page"),&n)&&n>=0&&n<=INT_MAX)v->pages=(int)n;
 int owner=field(d,row,"upper");
 if(owner>=0&&d->t[owner].type==JSMN_OBJECT){
  if(integer(d,field(d,owner,"mid"),&n)&&n>=0)v->owner_mid=n;
  optional_text(d,owner,"name",v->author,sizeof(v->author));
 }
 int counts=field(d,row,"cnt_info");
 if(counts>=0&&d->t[counts].type==JSMN_OBJECT&&integer(d,field(d,counts,"play"),&n)&&n>=0)v->views=n;
 out->usable=true;return true;
}
BiliApiResult fq_parse_page(const char *b,size_t n,int page,FqPage *out){
 if(out)memset(out,0,sizeof(*out));
 if(!out||page<1)return unknown();
 BiliApiResult r=api_response_result(0,200,b,n);if(r.outcome!=BILI_API_SUCCESS)return r;
 Doc d={.b=b};jsmn_parser parser;jsmn_init(&parser);d.n=jsmn_parse(&parser,b,n,NULL,0);if(d.n<1)return unknown();d.t=malloc((size_t)d.n*sizeof(*d.t));d.j=json_parse(b,n);if(!d.t||!d.j){free(d.t);json_free(d.j);return unknown();}jsmn_init(&parser);if(jsmn_parse(&parser,b,n,d.t,(unsigned)d.n)!=d.n)goto bad;
 int data=field(&d,0,"data"),info=field(&d,data,"info"),arr=field(&d,data,"medias");int64_t total=0;bool more=false;
 if(!integer(&d,field(&d,info,"media_count"),&total)||total<0||!boolean(&d,field(&d,data,"has_more"),&more))goto bad;
 int count=-1;if(arr>=0&&d.t[arr].type==JSMN_ARRAY)count=d.t[arr].size;else if(arr>=0&&d.t[arr].type==JSMN_PRIMITIVE&&d.t[arr].end-d.t[arr].start==4&&!memcmp(b+d.t[arr].start,"null",4))count=0;
 int64_t offset=((int64_t)page-1)*20,remaining=total-offset;
 if(remaining<0)remaining=0;
 int expected=remaining>20?20:(int)remaining;
 /* Deletions can leave fewer returned slots than media_count predicts.
  * has_more controls traversal, while total still bounds page coordinates. */
 if(count<0||count>expected||(more&&remaining<=20))goto bad;
 out->count=count;out->total=total;out->has_more=more;
 int at=arr+1;for(int i=0;i<count;i++){if(!entry(&d,at,&out->entries[i]))goto bad;at=skip(&d,at);}
 free(d.t);json_free(d.j);return r;
 bad:free(d.t);json_free(d.j);memset(out,0,sizeof(*out));return unknown();
}
static FavoriteQueueResult stop(FavoriteQueue *q,FavoriteQueueResult why){
 q->stopped=true;q->terminal=why;q->current_aid=0;
 /* A terminal queue never retains personal metadata across logout/relogin. */
 memset(&q->raw,0,sizeof(q->raw));memset(q->snapshot_aids,0,sizeof(q->snapshot_aids));memset(q->snapshot_bvids,0,sizeof(q->snapshot_bvids));
 memset(q->permutation,0,sizeof(q->permutation));memset(q->snapshot_counts,0,sizeof(q->snapshot_counts));q->snapshot_total=q->snapshot_pages=q->random_count=q->random_pos=0;
 return why;
}
static bool identity(FavoriteQueue *q){
 bool same=q->account_mid>0&&q->account_mid==bili_account_id();
 if(!same)q->issue=FQ_ISSUE_ACCOUNT;
 return same;
}
static int64_t page_limit(const FavoriteQueue *q){return q->raw.total/20+(q->raw.total%20!=0);}

static bool fetch(FavoriteQueue *q,int page){
 if(!identity(q))return false;
 char folder[24],pn[24];numeric_i64(q->folder_id,folder,sizeof(folder));snprintf(pn,sizeof(pn),"%d",page);const char *keys[]={"media_id","pn","ps","platform"},*vals[]={folder,pn,"20","web"};HttpResponse response={0};q->last_result=bili_fetch_json("/x/v3/fav/resource/list",keys,vals,4,false,&response);
 q->issue=FQ_ISSUE_REQUEST;
 FqPage next={0};if(q->last_result.outcome==BILI_API_SUCCESS){q->issue=FQ_ISSUE_FORMAT;q->last_result=fq_parse_page(response.data,response.len,page,&next);if(q->last_result.outcome==BILI_API_REJECTED)q->issue=FQ_ISSUE_REQUEST;}net_response_free(&response);
 if(!identity(q)||q->last_result.outcome!=BILI_API_SUCCESS)return false;
 if(q->page>0&&next.total!=q->raw.total){q->issue=FQ_ISSUE_CHANGED;q->last_result=unknown();return false;}
 q->issue=FQ_ISSUE_NONE;q->raw=next;q->page=page;q->index=-1;if(!next.has_more)q->last_page=page;return true;
}
FavoriteQueueResult fq_open(int64_t folder,int page,int64_t selected,FavoriteQueue *q){
 if(!q)return FQ_ERROR;
 memset(q,0,sizeof(*q));q->index=-1;q->folder_id=folder;q->account_mid=bili_account_id();q->last_result=unknown();
 if(folder<=0||page<1||selected<=0)return stop(q,FQ_ERROR);
 if(!identity(q))return stop(q,FQ_STOP);
 if(!fetch(q,page))return stop(q,identity(q)?FQ_ERROR:FQ_STOP);
 int found=-1;for(int i=0;i<q->raw.count;i++)if(q->raw.entries[i].usable&&q->raw.entries[i].video.aid==selected){if(found>=0){q->issue=FQ_ISSUE_CHANGED;return stop(q,FQ_STOP);}found=i;}
 if(found<0){q->issue=FQ_ISSUE_CHANGED;return stop(q,FQ_STOP);}
 q->index=found;q->current_aid=selected;return FQ_NEXT;
}
static uint32_t random_word(uint32_t *state){
 uint32_t x=*state;x^=x<<13;x^=x>>17;x^=x<<5;*state=x;return x;
}
static uint32_t random_below(uint32_t *state,uint32_t bound){
 uint32_t value,threshold=(uint32_t)(-bound)%bound;
 do {value=random_word(state);}while(value<threshold);
 return value%bound;
}
static FavoriteQueueResult snapshot(FavoriteQueue *q,uint32_t seed){
 if(q->raw.total>FQ_RANDOM_MAX)return stop(q,FQ_LIMIT);
 q->snapshot_total=0;
 for(;;){
  int offset=(q->page-1)*20;
  if(offset<0||offset>=FQ_RANDOM_MAX||q->raw.count>FQ_RANDOM_MAX-offset)return stop(q,FQ_LIMIT);
  q->snapshot_counts[q->page-1]=(uint8_t)q->raw.count;
  q->snapshot_total=offset+q->raw.count;
  for(int i=0;i<q->raw.count;i++){
   FqEntry *e=&q->raw.entries[i];
   if(!e->usable)continue;
   /* A duplicate aid cannot be emitted twice in one random pass. Treat an
    * ambiguous snapshot as changed/unusable instead of silently truncating. */
   for(int j=0;j<offset+i;j++)if(q->snapshot_aids[j]==e->video.aid){q->issue=FQ_ISSUE_CHANGED;return stop(q,FQ_STOP);}
   q->snapshot_aids[offset+i]=e->video.aid;
   memcpy(q->snapshot_bvids[offset+i],e->video.bvid,sizeof(e->video.bvid));
   q->permutation[q->random_count++]=(uint16_t)(offset+i);
  }
  if(!q->raw.has_more)break;
  if(!fetch(q,q->page+1))return stop(q,identity(q)?FQ_ERROR:FQ_STOP);
 }
 q->snapshot_pages=q->page;
 if(!q->random_count)return stop(q,FQ_END);
 uint32_t state=seed?seed:0x9e3779b9u;
 for(int i=q->random_count-1;i>0;i--){
  int j=(int)random_below(&state,(uint32_t)i+1);uint16_t temp=q->permutation[i];q->permutation[i]=q->permutation[j];q->permutation[j]=temp;
 }
 return FQ_NEXT;
}
FavoriteQueueResult fq_open_all(int64_t folder,FqOrder order,uint32_t seed,FavoriteQueue *q,BiliVideo *out){
 if(out)memset(out,0,sizeof(*out));
 if(!q)return FQ_ERROR;
 memset(q,0,sizeof(*q));q->index=-1;q->folder_id=folder;q->account_mid=bili_account_id();q->last_result=unknown();q->order=order;
 if(!out||folder<=0||(unsigned)order>(unsigned)FQ_ORDER_LOOP)return stop(q,FQ_ERROR);
 if(!identity(q))return stop(q,FQ_STOP);
 if(!fetch(q,1))return stop(q,identity(q)?FQ_ERROR:FQ_STOP);
 if(order==FQ_ORDER_RANDOM){FavoriteQueueResult r=snapshot(q,seed);if(r!=FQ_NEXT)return r;}
 return fq_next(q,out);
}
static FavoriteQueueResult random_next(FavoriteQueue *q,BiliVideo *out){
 if(q->random_pos>=q->random_count)return stop(q,FQ_END);
 int slot=q->permutation[q->random_pos];
 if(slot<0||slot>=q->snapshot_total||!q->snapshot_aids[slot]||!q->snapshot_bvids[slot][0]){q->issue=FQ_ISSUE_CHANGED;return stop(q,FQ_STOP);}
 if(!identity(q))return stop(q,FQ_STOP);
 /* A frozen AV/BV seed never substitutes a newly collected/reordered item.
  * Live view metadata verifies both identifiers before any stream is opened. */
 out->aid=q->snapshot_aids[slot];memcpy(out->bvid,q->snapshot_bvids[slot],sizeof(out->bvid));
 snprintf(out->title,sizeof(out->title),"收藏夹视频");out->views=-1;
 q->index=slot%20;q->page=slot/20+1;q->random_pos++;q->current_aid=out->aid;
 return FQ_NEXT;
}
FavoriteQueueResult fq_next(FavoriteQueue *q,BiliVideo *out){
 if(out)memset(out,0,sizeof(*out));
 if(!q||!out)return FQ_ERROR;
 if(q->stopped)return q->terminal;
 if(!identity(q))return stop(q,FQ_STOP);
 if(q->order==FQ_ORDER_RANDOM)return random_next(q,out);
 int64_t skipped=0,hops=0;
 for(;;){
  q->index++;
  while(q->index>=q->raw.count){
   int next;
   if(!q->raw.has_more){if(q->order!=FQ_ORDER_LOOP||q->raw.total==0)return stop(q,FQ_END);next=1;}
   else {if(q->page>=INT_MAX||(int64_t)q->page>=page_limit(q))return stop(q,FQ_ERROR);next=q->page+1;}
   if(++hops>page_limit(q)+1)return stop(q,FQ_END);
   if(!fetch(q,next))return stop(q,identity(q)?FQ_ERROR:FQ_STOP);
   q->index=0;
  }
  if(!identity(q))return stop(q,FQ_STOP);
  FqEntry *e=&q->raw.entries[q->index];
  if(!e->usable){if(++skipped>=q->raw.total)return stop(q,FQ_END);continue;}
  q->current_aid=e->video.aid;*out=e->video;return FQ_NEXT;
 }
}




bool fq_has_previous(const FavoriteQueue *q){
 if(!q||q->stopped||q->current_aid<=0)return false;
 if(q->order==FQ_ORDER_RANDOM)return q->random_pos>1;
 if(q->order==FQ_ORDER_LOOP)return q->raw.total>1;
 for(int i=q->index-1;i>=0;i--)if(q->raw.entries[i].usable)return true;
 return q->page>1;
}
/* A missing predecessor is a boundary, not a new current item. Refetch only
 * when scanning crossed a page; never leave index pointing into another page. */
static FavoriteQueueResult previous_end(FavoriteQueue *q,int page,int index){
 int64_t aid=q->current_aid;
 if(q->page!=page&&!fetch(q,page))return stop(q,identity(q)?FQ_ERROR:FQ_STOP);
 if(index<0||index>=q->raw.count||!q->raw.entries[index].usable||q->raw.entries[index].video.aid!=aid){q->issue=FQ_ISSUE_CHANGED;return stop(q,FQ_STOP);}
 q->index=index;return FQ_END;
}
FavoriteQueueResult fq_previous(FavoriteQueue *q,BiliVideo *out){
 if(out)memset(out,0,sizeof(*out));
 if(!q||!out)return FQ_ERROR;
 if(q->stopped)return q->terminal;
 if(!identity(q))return stop(q,FQ_STOP);
 if(q->order==FQ_ORDER_RANDOM){
  if(q->random_pos<=1)return FQ_END;
  q->random_pos-=2;return random_next(q,out);
 }
 if(!fq_has_previous(q))return FQ_END;
 int original_page=q->page,original_index=q->index;
 int index=q->index;int64_t skipped=0,hops=0;
 for(;;){
  --index;
  while(index<0){
   int page=q->page-1;
   if(page<1){
    if(q->order!=FQ_ORDER_LOOP)return previous_end(q,original_page,original_index);
    /* Server may report an early last page after deletions. Discover it
     * through has_more once instead of inferring it from media_count. */
    if(!q->last_page){
     while(q->raw.has_more){
      if(q->page>=INT_MAX||(int64_t)q->page>=page_limit(q))return stop(q,FQ_ERROR);
      if(!fetch(q,q->page+1))return stop(q,identity(q)?FQ_ERROR:FQ_STOP);
     }
    }
    page=q->last_page;
   }
   if(++hops>page_limit(q)+1)return previous_end(q,original_page,original_index);
   if(!fetch(q,page))return stop(q,identity(q)?FQ_ERROR:FQ_STOP);
   index=q->raw.count-1;
  }
  if(!identity(q))return stop(q,FQ_STOP);
  FqEntry *e=&q->raw.entries[index];
  if(!e->usable){if(++skipped>=q->raw.total)return previous_end(q,original_page,original_index);continue;}
  q->index=index;q->current_aid=e->video.aid;*out=e->video;
  return FQ_NEXT;
 }
}
