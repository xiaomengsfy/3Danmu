#include "api_response.h"
#include "video_actions_api.h"
#include "bili_transport.h"
#include <stdio.h>
#include "numeric_text.h"
#include "jsonx.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#define JSMN_STATIC
#include "vendor/jsmn.h"
/* jsmn intentionally permits incomplete JSON grammar and json_get_int accepts
* numeric prefixes. Writes need a strict envelope before either is trusted.
* This grammar pass is bounded in size/depth and consumes exactly one value. */
typedef struct { const char *p, *end;
} Cursor;
static void spaces(Cursor *c) {
    while (c->p < c->end && (*c->p==' ' || *c->p=='\t' || *c->p=='\r' || *c->p=='\n')) c->p++;
}
static bool quoted(Cursor *c, bool key) {
    if (c->p == c->end || *c->p++ != '"') return false;
    while (c->p < c->end) {
        unsigned char ch = (unsigned char)*c->p++;
        if (ch == '"') return true;
        if (ch < 32) return false;
        if (ch == '\\') {
            if (c->p == c->end) return false;
            ch = (unsigned char)*c->p++;
            if (ch == 'u') {
                /* C-string key comparison must never alias "code\u0000x"
                * to "code" after unescaping. */
                if (key && c->end-c->p>=4 && !memcmp(c->p,"0000",4)) return false;
                for (int i=0;i<4;i++) {
                    if (c->p == c->end) return false;
                    ch = (unsigned char)*c->p++;
                    if (!((ch>='0' && ch<='9') || (ch>='a' && ch<='f') || (ch>='A' && ch<='F'))) return false;
                }
            } else if (!strchr("\"\\/bfnrt", ch)) return false;
        }
    }
    return false;
}
static bool digit(Cursor *c) { return c->p < c->end && *c->p >= '0' && *c->p <= '9';
}
static bool value(Cursor *c, int depth) {
    spaces(c);
    if (depth > 48 || c->p == c->end) return false;
    char ch=*c->p;
    if (ch=='"') return quoted(c,false);
    if (ch=='{' || ch=='[') {
        bool object=ch=='{';
        char end=object?'}':']';
        c->p++;
        spaces(c);
        if (c->p<c->end && *c->p==end) { c->p++;
            return true;
        }
        for (;;) {
            if (object) {
                if (!quoted(c,true)) return false;
                spaces(c);
                if(c->p==c->end || *c->p++!=':') return false;
            }
            if (!value(c,depth+1)) return false;
            spaces(c);
            if(c->p==c->end) return false;
            ch=*c->p++;
            if(ch==end) return true;
            if(ch!=',') return false;
            spaces(c);
        }
    }
    const char *literal = ch=='t'?"true":ch=='f'?"false":ch=='n'?"null":NULL;
    if(literal) {
        size_t n=strlen(literal);
        if ((size_t)(c->end-c->p)<n || memcmp(c->p,literal,n)) return false;
        c->p+=n;
        return true;
    }
    if(ch=='-') c->p++;
    if(!digit(c)) return false;
    if(*c->p=='0') c->p++;
    else while(digit(c)) c->p++;
    if(c->p<c->end && *c->p=='.') {
        c->p++;
        if(!digit(c)) return false;
        while(digit(c)) c->p++;
    }
    if(c->p<c->end && (*c->p=='e' || *c->p=='E')) {
        c->p++;
        if(c->p<c->end && (*c->p=='+' || *c->p=='-')) c->p++;
        if(!digit(c)) return false;
        while(digit(c)) c->p++;
    }
    return true;
}
typedef struct { const char *body;
    jsmntok_t *tokens;
    int count;
    Json *json;
} Document;
static void release(Document *d) { free(d->tokens);
    json_free(d->json);
    memset(d,0,sizeof(*d));
}
static bool document(Document *d,const char *body,size_t len) {
    memset(d,0,sizeof(*d));
    if (!body || !len || len>2u*1024*1024) return false;
    Cursor c={body,body+len};
    if(!value(&c,0)) return false;
    spaces(&c);
    if(c.p!=c.end) return false;
    jsmn_parser p;
    jsmn_init(&p);
    int n=jsmn_parse(&p,body,len,NULL,0);
    if(n<=0 || n>65536) return false;
    d->tokens=malloc((size_t)n*sizeof(*d->tokens));
    if(!d->tokens) return false;
    jsmn_init(&p);
    d->count=jsmn_parse(&p,body,len,d->tokens,(unsigned)n);
    d->body=body;
    d->json=json_parse(body,len);
    if(d->count<=0 || d->tokens[0].type!=JSMN_OBJECT || !d->json) { release(d);
        return false;
    }
    return true;
}
static bool eq(const Document *d,int token,const char *text) {
    return token>=0 && token<d->count &&
    (size_t)(d->tokens[token].end-d->tokens[token].start)==strlen(text) &&
    !memcmp(d->body+d->tokens[token].start,text,strlen(text));
}
static int skip(const Document *d,int t) {
    int end=d->tokens[t].end;
    t++;
    while(t<d->count && d->tokens[t].start<end) t++;
    return t;
}
/* Duplicate required keys are ambiguous, including escaped spellings. */
static int field(const Document *d,int object,const char *name) {
    if(object<0 || d->tokens[object].type!=JSMN_OBJECT) return -1;
    int found=-1;
    for(int t=object+1;t<d->count && d->tokens[t].start<d->tokens[object].end;) {
        char key[64];
        json_get_str(d->json,t,"",key,sizeof(key));
        if (!strcmp(key,name)) { if(found>=0) return -2;
            found=t+1;
        }
        t=skip(d,t+1);
    }
    return found;
}
static bool integer(const Document *d,int t,int64_t *out) {
    if(t<0 || d->tokens[t].type!=JSMN_PRIMITIVE) return false;
    int n=d->tokens[t].end-d->tokens[t].start;
    if(n<=0 || n>20) return false;
    char text[24];
    memcpy(text,d->body+d->tokens[t].start,(size_t)n);
    text[n]=0;
    int start=text[0]=='-'?1:0;
    if(start==n) return false;
    for(int i=start;i<n;i++) if(text[i]<'0' || text[i]>'9') return false;
    errno=0;
    char *end;
    long long v=strtoll(text,&end,10);
    if(errno==ERANGE || *end) return false;
    *out=(int64_t)v;
    return true;
}
static bool null_token(const Document *d,int t) { return t>=0 && d->tokens[t].type==JSMN_PRIMITIVE && eq(d,t,"null");
}
/* Strict schema access: every step rejects duplicate decoded keys. */
static int path(const Document *d,int at,const char *p) {
    char key[96];
    while(*p) { const char *end=strchr(p,'.');
        size_t n=end?(size_t)(end-p):strlen(p);
        if(n>=sizeof(key))return -1;
        memcpy(key,p,n);
        key[n]=0;
        at=field(d,at,key);
        if(at<0)return -1;
        if(!end)break;
        p=end+1;
    }return at;
}
static bool text(const Document*d,int at,char*out,size_t cap) {
    if(at<0 || d->tokens[at].type!=JSMN_STRING || cap<2)return false;
    if(!json_get_str(d->json,at,"",out,cap))return false;
    size_t n=strlen(out);
    /* jsonx can end a bounded copy inside a UTF-8 sequence. */
    if(n==cap-1) { while(n && ((unsigned char)out[n-1]&0xc0)==0x80)n--;
        if(n && (unsigned char)out[n-1]>=0xc0)n--;
        out[n]=0;
    }
    for(size_t i=0;out[i];){unsigned char c=(unsigned char)out[i];
        size_t need=c<0x80?1:c>=0xc2&&c<=0xdf?2:c>=0xe0&&c<=0xef?3:c>=0xf0&&c<=0xf4?4:0;
        if(!need)return false;
        for(size_t k=1;k<need;k++)if(!out[i+k]||((unsigned char)out[i+k]&0xc0)!=0x80)return false;
        if(need==3&&((c==0xe0&&(unsigned char)out[i+1]<0xa0)||(c==0xed&&(unsigned char)out[i+1]>=0xa0)))return false;
        if(need==4&&((c==0xf0&&(unsigned char)out[i+1]<0x90)||(c==0xf4&&(unsigned char)out[i+1]>=0x90)))return false;
        i+=need;
    }
    return true;
}
static bool txt(const Document*d,int at,const char*p,char*out,size_t cap) {return text(d,path(d,at,p),out,cap);
}
static bool ident(const Document*d,int at,int64_t*out) {
    if(integer(d,at,out))return *out>=0;
    char s[24];
    if(at<0||d->tokens[at].end-d->tokens[at].start>19||!text(d,at,s,sizeof(s)) || !s[0])return false;
    for(int i=0;s[i];i++)if(s[i]<'0'||s[i]>'9')return false;
    errno=0;
    char*end;
    long long n=strtoll(s,&end,10);
    if(errno || *end)return false;
    *out=n;
    return true;
}
static bool num(const Document*d,int at,const char*p,int64_t*out) {return ident(d,path(d,at,p),out);
}
static BiliApiResult begin(int t,int s,const char*b,size_t n,Document*d) {
    BiliApiResult r=api_response_result(t,s,b,n);
    memset(d,0,sizeof(*d));
    if(r.outcome==BILI_API_SUCCESS && !document(d,b,n))r.outcome=BILI_API_UNKNOWN;
    if(r.outcome==BILI_API_SUCCESS)for(int obj=0;obj<d->count;obj++)if(d->tokens[obj].type==JSMN_OBJECT) {
        for(int a=obj+1;a<d->count && d->tokens[a].start<d->tokens[obj].end;a=skip(d,a+1)) {
            char key[256];
            if(d->tokens[a].end-d->tokens[a].start>=255 || !text(d,a,key,sizeof(key)) || field(d,obj,key)<0) {r.outcome=BILI_API_UNKNOWN;
                break;
            }
        }
        if(r.outcome!=BILI_API_SUCCESS)break;
    }return r;
}
BiliApiResult video_parse_coins(int t,int s,const char*b,size_t n,int*out){if(out)*out=-1;
    Document d;
    BiliApiResult r=begin(t,s,b,n,&d);
    int64_t v;
    if(r.outcome==BILI_API_SUCCESS){if(!out||!num(&d,0,"data.multiply",&v)||v>2)r.outcome=BILI_API_UNKNOWN;
        else *out=(int)v;
    }release(&d);
    return r;
}
BiliApiResult video_parse_folders(int t,int s,const char*b,size_t n,BiliFavFolder*out,int max,BiliPageInfo*info){if(out&&max>0)memset(out,0,(size_t)max*sizeof(*out));
    if(info)memset(info,0,sizeof(*info));
    Document d;
    BiliApiResult r=begin(t,s,b,n,&d);
    if(r.outcome!=BILI_API_SUCCESS){release(&d);
        return r;
    }
    int64_t total;
    int arr=path(&d,0,"data.list");
    if(!out||max<1||!info||!num(&d,0,"data.count",&total)||total>10000)goto bad;
    int count=arr>=0&&d.tokens[arr].type==JSMN_ARRAY?d.tokens[arr].size:-1;
    if(total==0&&null_token(&d,arr))count=0;
    if(count<0||count!=total)goto bad;
    info->total=total;
    info->truncated=count>max;
    for(int i=0,at=arr+1;i<count;i++,at=skip(&d,at)){BiliFavFolder f={0};
        int64_t state,size;
        if(!num(&d,at,"id",&f.id)||f.id<=0||!txt(&d,at,"title",f.title,sizeof(f.title))||!f.title[0]||!num(&d,at,"fav_state",&state)||state>1||!num(&d,at,"media_count",&size)||size>INT_MAX)goto bad;
        f.contains=state==1;
        f.membership_known=true;
        f.media_count=(int)size;
        for(int k=0;k<info->count;k++)if(out[k].id==f.id)goto bad;
        if(info->count<max)out[info->count++]=f;
    }release(&d);
    return r;
    bad:if(out&&max>0)memset(out,0,(size_t)max*sizeof(*out));
    if(info)memset(info,0,sizeof(*info));
    r.outcome=BILI_API_UNKNOWN;
    release(&d);
    return r;
}
static BiliApiResult invalid(void){return (BiliApiResult){.outcome=BILI_API_UNKNOWN};
}
BiliApiResult video_coins(int64_t aid,int*out){if(out)*out=-1;
    if(aid<=0||!out)return invalid();
    char id[24];
    numeric_i64(aid,id,sizeof(id));
    const char*k[]={"aid"},*v[]={id};
    HttpResponse h={0};
    BiliApiResult r=bili_fetch_json("/x/web-interface/archive/coins",k,v,1,false,&h);
    if(r.outcome==BILI_API_SUCCESS)r=video_parse_coins(0,h.status,h.data,h.len,out);
    net_response_free(&h);
    return r;
}
BiliApiResult video_coin_once(int64_t aid,int count,bool like){if(aid<=0||count<1||count>2)return invalid();
    char id[24];
    numeric_i64(aid,id,sizeof(id));
    const char*k[]={"aid","multiply","select_like"},*v[]={id,count==1?"1":"2",like?"1":"0"};
    HttpResponse h={0};
    BiliApiResult r=bili_submit_json("/x/web-interface/coin/add",k,v,3,&h);
    net_response_free(&h);
    return r;
}
BiliApiResult video_folders(int64_t aid,BiliFavFolder*out,int max,BiliPageInfo*info){if(out&&max>0)memset(out,0,(size_t)max*sizeof(*out));
    if(info)memset(info,0,sizeof(*info));
    int64_t mid=bili_account_id();
    if(mid<=0||aid<=0||!out||max<=0||!info)return invalid();
    char uid[24],id[24];
    numeric_i64(mid,uid,sizeof(uid));
    numeric_i64(aid,id,sizeof(id));
    const char*k[]={"up_mid","type","rid"},*v[]={uid,"2",id};
    HttpResponse h={0};
    BiliApiResult r=bili_fetch_json("/x/v3/fav/folder/created/list-all",k,v,3,false,&h);
    if(r.outcome==BILI_API_SUCCESS)r=video_parse_folders(0,h.status,h.data,h.len,out,max,info);
    net_response_free(&h);
    return r;
}
BiliApiResult video_favorite_delta_once(int64_t aid,const BiliFavFolder*folders,const bool*selected,int count){if(aid<=0||!folders||!selected||count<1||count>VIDEO_FOLDER_MAX)return invalid();
    char add[2400]={0},del[2400]={0},id[24];
    int changes=0;
    for(int i=0;i<count;i++){if(!folders[i].membership_known||folders[i].id<=0)return invalid();
        for(int k=0;k<i;k++)if(folders[k].id==folders[i].id)return invalid();
        if(folders[i].contains==selected[i])continue;
        changes++;
        char*f=selected[i]?add:del;
        size_t used=strlen(f);
        char digits[24];
        numeric_i64(folders[i].id,digits,sizeof(digits));
        size_t length=strlen(digits);
        if(used+length+(used?1u:0u)>=2400)return invalid();
        snprintf(f+used,2400-used,"%s%s",used?",":"",digits);
    }
    if(!changes)return (BiliApiResult){.outcome=BILI_API_SUCCESS};
    numeric_i64(aid,id,sizeof(id));
    const char*k[]={"rid","type","add_media_ids","del_media_ids"},*v[]={id,"2",add,del};
    HttpResponse h={0};
    BiliApiResult r=bili_submit_json("/x/v3/fav/resource/deal",k,v,4,&h);
    net_response_free(&h);
    return r;
}
