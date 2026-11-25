#include "api_response.h"
#include "social_api.h"
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
static bool boolean(const Document *d,int t,bool *out) {
    if(t<0 || d->tokens[t].type!=JSMN_PRIMITIVE) return false;
    if(eq(d,t,"true")) { *out=true;
        return true;
    }
    if(eq(d,t,"false")) { *out=false;
        return true;
    }
    return false;
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
BiliApiResult social_parse_relation(int t,int s,const char*b,size_t n,SocialRelation*out) {
    if(out)memset(out,0,sizeof(*out));
    Document d;
    BiliApiResult r=begin(t,s,b,n,&d);
    int64_t a;
    if(r.outcome==BILI_API_SUCCESS) {
        if(!out || !num(&d,0,"data.attribute",&a) || (a!=0&&a!=1&&a!=2&&a!=6&&a!=128))r.outcome=BILI_API_UNKNOWN;
        else {out->known=true;
            out->attribute=(int)a;
            out->following=a==2||a==6;
            out->mutual=a==6;
        }
    }release(&d);
    return r;
}
static bool person(const Document*d,int at,BiliProfile*p) {
    memset(p,0,sizeof(*p));
    p->fans=p->videos=-1;
    if(!num(d,at,"mid",&p->mid) || p->mid<=0)return false;
    if(!txt(d,at,"uname",p->name,sizeof(p->name)) && !txt(d,at,"name",p->name,sizeof(p->name)))return false;
    txt(d,at,"sign",p->sign,sizeof(p->sign));
    txt(d,at,"face",p->face,sizeof(p->face));
    return p->name[0]!=0;
}
BiliApiResult social_parse_people(int t,int s,const char*b,size_t n,int page,BiliProfile*out,int max,BiliPageInfo*info) {
    if(out&&max>0)memset(out,0,(size_t)max*sizeof(*out));
    if(info)memset(info,0,sizeof(*info));
    Document d;
    BiliApiResult r=begin(t,s,b,n,&d);
    if(r.outcome!=BILI_API_SUCCESS){release(&d);
        return r;
    }
    int arr=path(&d,0,"data.list");
    int64_t total;
    int count=arr>=0&&d.tokens[arr].type==JSMN_ARRAY?d.tokens[arr].size:-1;
    if(!out||max<=0||!info||page<1||!num(&d,0,"data.total",&total)||count<0||count>total)goto bad;
    info->total=total;
    info->has_more=(int64_t)page*20<total;
    info->truncated=count>max;
    for(int i=0,at=arr+1;i<count;i++,at=skip(&d,at)) {BiliProfile p;
        if(!person(&d,at,&p))goto bad;
        if(info->count<max)out[info->count++]=p;
    }
    release(&d);
    return r;
    bad:if(info)memset(info,0,sizeof(*info));
    if(out&&max>0)memset(out,0,(size_t)max*sizeof(*out));
    r.outcome=BILI_API_UNKNOWN;
    release(&d);
    return r;
}
static bool dynamic(const Document*d,int at,SocialDynamic*out,int depth) {
    if(depth>2)return false;
    memset(out,0,sizeof(*out));
    if(!txt(d,at,"type",out->type,sizeof(out->type)))return false;
    if(!strcmp(out->type,"DYNAMIC_TYPE_NONE")){txt(d,at,"id_str",out->id,sizeof(out->id));
        strcpy(out->body,"原动态已删除或不可见");
        return true;
    }
    if(!txt(d,at,"id_str",out->id,sizeof(out->id))||!out->id[0])return false;
    int author=path(d,at,"modules.module_author");
    if(!person(d,author,&out->author))return false;
    out->target.kind=SOCIAL_UP;
    out->target.profile=out->author;
    txt(d,at,"modules.module_dynamic.desc.text",out->body,sizeof(out->body));
    int major=path(d,at,"modules.module_dynamic.major");
    if(!strcmp(out->type,"DYNAMIC_TYPE_FORWARD")) {
        int orig=field(d,at,"orig");
        SocialDynamic *nested=calloc(1,sizeof(*nested));
        if(!nested)return false;
        bool ok=orig>=0&&dynamic(d,orig,nested,depth+1);
        if(ok){size_t used=strlen(out->body);
            snprintf(out->body+used,sizeof(out->body)-used,"\n转发 %s: %s",nested->author.name,nested->body);
            out->target=nested->target;
            out->image_count=nested->image_count;
            memcpy(out->images,nested->images,sizeof(out->images));
        }free(nested);
        return ok;
    }
    int opus=field(d,major,"opus");
    if(opus>=0) {
        /* Decode into heap-owned card storage; never put an8KiB copy on the
         *16KiB API worker stack. Missing summary preserves the description. */
        int summary=path(d,opus,"summary.text");
        if(summary>=0 && !text(d,summary,out->body,sizeof(out->body)))return false;
    }
    int archive=field(d,major,"archive");
    if(archive>=0) {BiliVideo *v=&out->target.video;
        int64_t duration;
        if(!txt(d,archive,"bvid",v->bvid,sizeof(v->bvid)) || strlen(v->bvid)!=12 || strncmp(v->bvid,"BV",2) || !txt(d,archive,"title",v->title,sizeof(v->title)))return false;
        num(d,archive,"aid",&v->aid);
        if(num(d,archive,"duration",&duration)&&duration<=INT_MAX)v->duration=(int)duration;
        v->owner_mid=out->author.mid;
        txt(d,archive,"cover",v->pic,sizeof(v->pic));
        snprintf(v->author,sizeof(v->author),"%s",out->author.name);
        out->target.kind=SOCIAL_VIDEO;
        if(!out->body[0])snprintf(out->body,sizeof(out->body),"%s",v->title);
    }
    int pgc=field(d,major,"pgc");
    if(pgc>=0) {if(!num(d,pgc,"epid",&out->target.ep_id)||out->target.ep_id<=0)return false;
        out->target.kind=SOCIAL_PGC;
        if(!out->body[0])txt(d,pgc,"title",out->body,sizeof(out->body));
    }
    int pictures=opus>=0?field(d,opus,"pics"):path(d,major,"draw.items");
    if(pictures>=0) {if(d->tokens[pictures].type!=JSMN_ARRAY)return false;
        int count=d->tokens[pictures].size;
        for(int i=0,p=pictures+1;i<count;i++,p=skip(d,p)) {SocialImage im={0};
            int64_t w=0,h=0;
            if(!txt(d,p,"src",im.url,sizeof(im.url))&&!txt(d,p,"url",im.url,sizeof(im.url)))return false;
            num(d,p,"width",&w);
            num(d,p,"height",&h);
            if(w>INT_MAX||h>INT_MAX)return false;
            im.width=(int)w;
            im.height=(int)h;
            if(out->image_count<9)out->images[out->image_count++]=im;
            else out->truncated=true;
        }
    }
    if(!out->body[0])snprintf(out->body,sizeof(out->body),"此类型暂不支持正文：%.39s",out->type);
    if(strlen(out->body)>=sizeof(out->body)-5)out->truncated=true;
    return true;
}
BiliApiResult social_parse_dynamics(int t,int s,const char*b,size_t n,SocialDynamic*out,int max,SocialPage*info) {
    if(out&&max>0)memset(out,0,(size_t)max*sizeof(*out));
    if(info)memset(info,0,sizeof(*info));
    Document d;
    BiliApiResult r=begin(t,s,b,n,&d);
    if(r.outcome!=BILI_API_SUCCESS){release(&d);
        return r;
    }
    int arr=path(&d,0,"data.items");
    bool more=false;
    int64_t m;
    int mt=path(&d,0,"data.has_more");
    if(!boolean(&d,mt,&more)) {if(!ident(&d,mt,&m)||m>1)goto bad;
        more=m!=0;
    }
    if(!out||max<=0||!info||arr<0||d.tokens[arr].type!=JSMN_ARRAY)goto bad;
    int offset=path(&d,0,"data.offset");
    if(offset<0||d.tokens[offset].end-d.tokens[offset].start>=SOCIAL_CURSOR||!text(&d,offset,info->next,sizeof(info->next)) || (more&&!info->next[0]))goto bad;
    info->page.has_more=more;
    int count=d.tokens[arr].size;
    /* Never advance an opaque server offset past entries we could not retain. */
    if(count>max)goto bad;
    info->page.total=count;
    info->page.truncated=false;
    for(int i=0,at=arr+1;i<count;i++,at=skip(&d,at)) {SocialDynamic *card=calloc(1,sizeof(*card));
        if(!card)goto bad;
        bool ok=dynamic(&d,at,card,0);
        if(ok&&info->page.count<max)out[info->page.count++]=*card;
        free(card);
        if(!ok)goto bad;
    }release(&d);
    return r;
    bad:if(info)memset(info,0,sizeof(*info));
    if(out&&max>0)memset(out,0,(size_t)max*sizeof(*out));
    r.outcome=BILI_API_UNKNOWN;
    release(&d);
    return r;
}
BiliApiResult social_parse_messages(int t,int s,const char*b,size_t n,SocialMessageKind kind,SocialMessage*out,int max,SocialPage*info) {
    if(out&&max>0)memset(out,0,(size_t)max*sizeof(*out));
    if(info)memset(info,0,sizeof(*info));
    Document d;
    BiliApiResult r=begin(t,s,b,n,&d);
    if(r.outcome!=BILI_API_SUCCESS){release(&d);
        return r;
    }
    if(!out||max<=0||!info)goto bad;
    if(kind==SOCIAL_UNREAD) {
        const char*keys[]={"reply","at","like","sys_msg"};
        int64_t v;
        char line[96];
        strcpy(out[0].title,"未读消息");
        for(int i=0;i<4;i++){if(!num(&d,path(&d,0,"data"),keys[i],&v))goto bad;
            char digits[24];
            numeric_i64(v,digits,sizeof(digits));
            snprintf(line,sizeof(line),"%s: %s\n",keys[i],digits);
            strncat(out[0].body,line,sizeof(out[0].body)-strlen(out[0].body)-1);
        }info->page.count=1;
        release(&d);
        return r;
    }
    if(kind!=SOCIAL_REPLIES)goto bad;
    int arr=path(&d,0,"data.items");
    if(arr<0||d.tokens[arr].type!=JSMN_ARRAY)goto bad;
    int cursor=path(&d,0,"data.cursor");
    bool end=true;
    if(!boolean(&d,field(&d,cursor,"is_end"),&end))goto bad;
    info->page.has_more=!end;
    int count=d.tokens[arr].size;
    if(count>max)goto bad; /* Never skip unretained messages with a cursor. */
    info->page.total=count;
    for(int i=0,at=arr+1;i<count;i++,at=skip(&d,at)) {
        SocialMessage msg={0};
        if(!num(&d,at,"id",&msg.id)||msg.id<=0)goto bad;
        strcpy(msg.title,"回复");
        if(!txt(&d,at,"item.source_content",msg.body,sizeof(msg.body)))goto bad;
        char context[1024];
        if(txt(&d,at,"item.title",context,sizeof(context)) && strcmp(context,msg.body)) {
            size_t used=strlen(msg.body);
            snprintf(msg.body+used,sizeof(msg.body)-used,"\n%s",context);
        }
        char who[80];
        if(txt(&d,at,"user.nickname",who,sizeof(who)))snprintf(msg.title,sizeof(msg.title),"%.79s",who);
        if(!num(&d,at,"reply_time",&msg.time))goto bad;
        char bv[16];
        if(txt(&d,at,"item.bvid",bv,sizeof(bv))&&strlen(bv)==12&&!strncmp(bv,"BV",2)) {
            msg.target.kind=SOCIAL_VIDEO;
            strcpy(msg.target.video.bvid,bv);
            snprintf(msg.target.video.title,sizeof(msg.target.video.title),"%.199s",msg.body);
        }
        out[info->page.count++]=msg;
    }
    if(!end){int64_t id,time;
        if(!num(&d,cursor,"id",&id)||!num(&d,cursor,"time",&time))goto bad;
        char first[24],second[24];
        numeric_i64(id,first,sizeof(first));
        numeric_i64(time,second,sizeof(second));
        snprintf(info->next,sizeof(info->next),"%s,%s",first,second);
    }
    release(&d);
    return r;
    bad:if(out&&max>0)memset(out,0,(size_t)max*sizeof(*out));
    if(info)memset(info,0,sizeof(*info));
    r.outcome=BILI_API_UNKNOWN;
    release(&d);
    return r;
}
static BiliApiResult invalid(void){BiliApiResult r={.outcome=BILI_API_UNKNOWN};
    return r;
}
BiliApiResult social_relation(int64_t mid,SocialRelation*out) {
    if(out)memset(out,0,sizeof(*out));
    if(mid<=0||!out)return invalid();
    char id[24];
    numeric_i64(mid,id,sizeof(id));
    const char*k[]={"fid"},*v[]={id};
    HttpResponse h={0};
    BiliApiResult r=bili_fetch_json("/x/relation",k,v,1,false,&h);
    if(r.outcome==BILI_API_SUCCESS)r=social_parse_relation(0,h.status,h.data,h.len,out);
    net_response_free(&h);
    return r;
}
BiliApiResult social_relation_set(int64_t mid,bool follow) {
    if(mid<=0||mid==bili_account_id())return invalid();
    char id[24];
    numeric_i64(mid,id,sizeof(id));
    const char*k[]={"fid","act","re_src"},*v[]={id,follow?"1":"2","11"};
    HttpResponse h={0};
    BiliApiResult r=bili_submit_json("/x/relation/modify",k,v,3,&h);
    net_response_free(&h);
    return r;
}
BiliApiResult social_people(int64_t mid,bool fans,int page,BiliProfile*out,int max,BiliPageInfo*info) {
    if(out&&max>0)memset(out,0,(size_t)max*sizeof(*out));
    if(info)memset(info,0,sizeof(*info));
    if(mid<=0||page<1||!out||max<=0||!info)return invalid();
    char id[24],pn[16];
    numeric_i64(mid,id,sizeof(id));
    snprintf(pn,sizeof(pn),"%d",page);
    const char*k[]={"vmid","pn","ps","order"},*v[]={id,pn,"20","desc"};
    HttpResponse h={0};
    BiliApiResult r=bili_fetch_json(fans?"/x/relation/followers":"/x/relation/followings",k,v,4,false,&h);
    if(r.outcome==BILI_API_SUCCESS)r=social_parse_people(0,h.status,h.data,h.len,page,out,max,info);
    net_response_free(&h);
    return r;
}
BiliApiResult social_dynamics(int64_t mid,const char*cursor,bool videos,SocialDynamic*out,int max,SocialPage*info) {
    if(out&&max>0)memset(out,0,(size_t)max*sizeof(*out));
    if(info)memset(info,0,sizeof(*info));
    if(mid<0||!cursor||strlen(cursor)>=SOCIAL_CURSOR||!out||max<=0||!info)return invalid();
    char id[24];
    numeric_i64(mid,id,sizeof(id));
    const char*k[]={"host_mid","offset","type","features"},*v[]={id,cursor,videos?"video":"all","itemOpusStyle"};
    HttpResponse h={0};
    BiliApiResult r=bili_fetch_json(mid?"/x/polymer/web-dynamic/v1/feed/space":"/x/polymer/web-dynamic/v1/feed/all",k+(mid?0:1),v+(mid?0:1),mid?4:3,false,&h);
    if(r.outcome==BILI_API_SUCCESS) {
        r=social_parse_dynamics(0,h.status,h.data,h.len,out,max,info);
        if(r.outcome==BILI_API_SUCCESS&&info->page.has_more&&(!strcmp(cursor,info->next)||info->page.count==0)){r.outcome=BILI_API_UNKNOWN;
            memset(info,0,sizeof(*info));
        }
    }net_response_free(&h);
    return r;
}
const char *social_message_support_note(SocialMessageKind kind) {
    switch(kind) {
        case SOCIAL_AT:return "提到我暂不支持：公开源码只确认端点和参数，缺响应结构证据。";
        case SOCIAL_LIKES:return "收到的赞暂不支持：缺最近和历史分组、游标结构证据。";
        case SOCIAL_SYSTEM:return "系统通知暂不支持：已确认专用消息域名，缺响应结构证据。";
        case SOCIAL_REPLIES:case SOCIAL_UNREAD:return NULL;
        default:return "不支持的消息类别。";
    }
}
BiliApiResult social_messages(SocialMessageKind kind,const char*cursor,SocialMessage*out,int max,SocialPage*info) {
    /* Verified endpoint references (source-only, not probed): nooblong fork
     *4789508a93f3... session.json / session.py. Unsupported categories never
     * reach transport; system host must be message.bilibili.com. */
    const char*paths[]={"/x/msgfeed/reply","/x/msgfeed/at","/x/msgfeed/like","https://message.bilibili.com/x/sys-msg/query_user_notify","/x/msgfeed/unread"};
    if(out&&max>0)memset(out,0,(size_t)max*sizeof(*out));
    if(info)memset(info,0,sizeof(*info));
    if((unsigned)kind>SOCIAL_UNREAD||!cursor||strlen(cursor)>=SOCIAL_CURSOR||!out||max<=0||!info)return invalid();
    if(social_message_support_note(kind))return (BiliApiResult){.outcome=BILI_API_REJECTED};
    char id[24]="0",time[24]="0";
    if(*cursor){const char*comma=strchr(cursor,',');
        if(!comma||comma==cursor||strchr(comma+1,',')||!comma[1]||(size_t)(comma-cursor)>=sizeof(id)||strlen(comma+1)>=sizeof(time))return invalid();
        memcpy(id,cursor,(size_t)(comma-cursor));
        id[comma-cursor]=0;
        strcpy(time,comma+1);
        for(int part=0;part<2;part++){const char*p=part?time:id;
            for(int q=0;p[q];q++)if(p[q]<'0'||p[q]>'9')return invalid();
            errno=0;
            char*end;
            (void)strtoll(p,&end,10);
            if(errno==ERANGE||*end)return invalid();
        }}
    const char*k[]={"id",kind==SOCIAL_REPLIES?"reply_time":kind==SOCIAL_AT?"at_time":"like_time"},*v[]={id,time};
    HttpResponse h={0};
    BiliApiResult r=bili_fetch_json(paths[kind],k,v,kind==SOCIAL_UNREAD?0:2,false,&h);
    if(r.outcome==BILI_API_SUCCESS){r=social_parse_messages(0,h.status,h.data,h.len,kind,out,max,info);
        if(r.outcome==BILI_API_SUCCESS&&info->page.has_more&&!strcmp(cursor,info->next)){r.outcome=BILI_API_UNKNOWN;
            memset(info,0,sizeof(*info));
        }}net_response_free(&h);
    return r;
}
