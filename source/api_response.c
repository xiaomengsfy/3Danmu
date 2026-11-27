#include "api_response.h"
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
typedef struct { const char *p, *end; } Cursor;
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
static bool digit(Cursor *c) { return c->p < c->end && *c->p >= '0' && *c->p <= '9'; }
static bool value(Cursor *c, int depth) {
	spaces(c);
	if (depth > 48 || c->p == c->end) return false;
	char ch=*c->p;
	if (ch=='"') return quoted(c,false);
	if (ch=='{' || ch=='[') {
		bool object=ch=='{';char end=object?'}':']';c->p++;spaces(c);
		if (c->p<c->end && *c->p==end) { c->p++;return true; }
		for (;;) {
			if (object) {
				if (!quoted(c,true)) return false;
				spaces(c);if(c->p==c->end || *c->p++!=':') return false;
			}
			if (!value(c,depth+1)) return false;
			spaces(c);if(c->p==c->end) return false;
			ch=*c->p++;if(ch==end) return true;if(ch!=',') return false;
			spaces(c);
		}
	}
	const char *literal = ch=='t'?"true":ch=='f'?"false":ch=='n'?"null":NULL;
	if(literal) {
		size_t n=strlen(literal);
		if ((size_t)(c->end-c->p)<n || memcmp(c->p,literal,n)) return false;
		c->p+=n;return true;
	}
	if(ch=='-') c->p++;
	if(!digit(c)) return false;
	if(*c->p=='0') c->p++;else while(digit(c)) c->p++;
	if(c->p<c->end && *c->p=='.') {
		c->p++;if(!digit(c)) return false;while(digit(c)) c->p++;
	}
	if(c->p<c->end && (*c->p=='e' || *c->p=='E')) {
		c->p++;if(c->p<c->end && (*c->p=='+' || *c->p=='-')) c->p++;
		if(!digit(c)) return false;
		while(digit(c)) c->p++;
	}
	return true;
}

typedef struct { const char *body; jsmntok_t *tokens; int count; Json *json; } Document;
static void release(Document *d) { free(d->tokens);json_free(d->json);memset(d,0,sizeof(*d)); }
static bool document(Document *d,const char *body,size_t len) {
	memset(d,0,sizeof(*d));
	if (!body || !len || len>2u*1024*1024) return false;
	Cursor c={body,body+len};
	if(!value(&c,0)) return false;
	spaces(&c);if(c.p!=c.end) return false;
	jsmn_parser p;jsmn_init(&p);
	int n=jsmn_parse(&p,body,len,NULL,0);
	if(n<=0 || n>65536) return false;
	d->tokens=malloc((size_t)n*sizeof(*d->tokens));if(!d->tokens) return false;
	jsmn_init(&p);d->count=jsmn_parse(&p,body,len,d->tokens,(unsigned)n);
	d->body=body;d->json=json_parse(body,len);
	if(d->count<=0 || d->tokens[0].type!=JSMN_OBJECT || !d->json) { release(d);return false; }
	return true;
}
static bool eq(const Document *d,int token,const char *text) {
	return token>=0 && token<d->count &&
	       (size_t)(d->tokens[token].end-d->tokens[token].start)==strlen(text) &&
	       !memcmp(d->body+d->tokens[token].start,text,strlen(text));
}
static int skip(const Document *d,int t) {
	int end=d->tokens[t].end;t++;
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
		if (!strcmp(key,name)) { if(found>=0) return -2;found=t+1; }
		t=skip(d,t+1);
	}
	return found;
}
static bool integer(const Document *d,int t,int64_t *out) {
	if(t<0 || d->tokens[t].type!=JSMN_PRIMITIVE) return false;
	int n=d->tokens[t].end-d->tokens[t].start;
	if(n<=0 || n>20) return false;
	char text[24];memcpy(text,d->body+d->tokens[t].start,(size_t)n);text[n]=0;
	int start=text[0]=='-'?1:0;if(start==n) return false;
	for(int i=start;i<n;i++) if(text[i]<'0' || text[i]>'9') return false;
	errno=0;char *end;long long v=strtoll(text,&end,10);
	if(errno==ERANGE || *end) return false;
	*out=(int64_t)v;return true;
}
static bool number(const Document *d,int obj,const char *key,int64_t *out) { return integer(d,field(d,obj,key),out); }
static bool string(const Document *d,int obj,const char *key,char *out,size_t n) {
	int t=field(d,obj,key);
	return t>=0 && d->tokens[t].type==JSMN_STRING && json_get_str(d->json,t,"",out,n);
}
static bool null_token(const Document *d,int t) { return t>=0 && d->tokens[t].type==JSMN_PRIMITIVE && eq(d,t,"null"); }
static bool boolean(const Document *d,int t,bool *out) {
	if(t<0 || d->tokens[t].type!=JSMN_PRIMITIVE) return false;
	if(eq(d,t,"true")) { *out=true;return true; }
	if(eq(d,t,"false")) { *out=false;return true; }
	return false;
}
static int voucher(const Document *d,int object) {
	const char *keys[]={"v_voucher","gaia_voucher"};
	int found=0;
	for(size_t i=0;i<2;i++) {
		int t=field(d,object,keys[i]);
		if(t==-2) return -1;
		if(t>=0 && !null_token(d,t) && !(d->tokens[t].type==JSMN_STRING && eq(d,t,""))) found=1;
	}
	return found;
}
static BiliApiResult envelope(int transport,int status,const char *body,size_t len,Document *d) {
	BiliApiResult r={.outcome=BILI_API_UNKNOWN,.http_status=status};memset(d,0,sizeof(*d));
	if(transport) return r;
	if(status>=400 && status<500 && status!=408) {
		r.outcome=BILI_API_REJECTED;r.login_required=status==401;
	}
	if(status!=200) return r;
	if(!document(d,body,len) || !number(d,0,"code",&r.code)) return r;
	int data=field(d,0,"data"),root_voucher=voucher(d,0),data_voucher=voucher(d,data);
	if(data==-2 || root_voucher<0 || data_voucher<0) return r;
	r.has_code=true;r.login_required=r.code==-101;
	r.outcome=r.code==0?BILI_API_SUCCESS:BILI_API_REJECTED;
	if(root_voucher || data_voucher) r.outcome=BILI_API_REJECTED;
	return r;
}
BiliApiResult api_response_result(int transport,int status,const char *body,size_t len) {
	Document d;BiliApiResult r=envelope(transport,status,body,len,&d);release(&d);return r;
}

BiliApiResult api_response_folders(int t,int s,const char *b,size_t n,
                                   BiliFavFolder *out,int max,BiliPageInfo *info) {
	if(out && max>0) memset(out,0,(size_t)max*sizeof(*out));
	if(info) memset(info,0,sizeof(*info));
	Document d;BiliApiResult r=envelope(t,s,b,n,&d);
	if(r.outcome!=BILI_API_SUCCESS) { release(&d);return r; }
	int data=field(&d,0,"data"),arr=field(&d,data,"list");int64_t total;
	if(!out || max<=0 || !info || !number(&d,data,"count",&total) || total<0) goto invalid;
	int count=arr>=0 && d.tokens[arr].type==JSMN_ARRAY?d.tokens[arr].size:-1;
	if(count<0 && total==0 && null_token(&d,arr)) count=0;
	if(count<0 || count>total || (count==0 && total>0)) goto invalid;
	info->total=total;info->truncated=total>count || count>max;
	int at=arr+1;
	for(int i=0;i<count;i++) {
		BiliFavFolder f={0};int64_t size;
		if(!number(&d,at,"id",&f.id) || f.id<=0 ||
		   !string(&d,at,"title",f.title,sizeof(f.title)) || !f.title[0] ||
		   !number(&d,at,"media_count",&size) || size<0 || size>INT_MAX) goto invalid;
		f.media_count=(int)size;
		if(info->count<max) out[info->count++]=f;
		at=skip(&d,at);
	}
	release(&d);return r;
invalid:
	if(out && max>0) memset(out,0,(size_t)max*sizeof(*out));
	if(info) memset(info,0,sizeof(*info));
	r.outcome=BILI_API_UNKNOWN;release(&d);return r;
}

static void image_url(char *url,size_t n) {
	if(url[0]=='/' && url[1]=='/' && strlen(url)+6<n) {
		memmove(url+6,url,strlen(url)+1);memcpy(url,"https:",6);
	} else if(!strncmp(url,"http:",5) && strlen(url)+1<n) {
		memmove(url+5,url+4,strlen(url+4)+1);memcpy(url,"https",5);
	}
}
BiliApiResult api_response_favorites(int t,int s,const char *b,size_t n,int page,
                                     BiliVideo *out,int max,BiliPageInfo *info) {
	if(out && max>0) memset(out,0,(size_t)max*sizeof(*out));
	if(info) memset(info,0,sizeof(*info));
	Document d;BiliApiResult r=envelope(t,s,b,n,&d);
	if(r.outcome!=BILI_API_SUCCESS) { release(&d);return r; }
	int data=field(&d,0,"data"),meta=field(&d,data,"info"),arr=field(&d,data,"medias");
	int64_t total;bool more;
	if(!out || max<=0 || !info || page<=0 || !number(&d,meta,"media_count",&total) || total<0 ||
	   !boolean(&d,field(&d,data,"has_more"),&more)) goto invalid;
	int count=arr>=0 && d.tokens[arr].type==JSMN_ARRAY?d.tokens[arr].size:-1;
	int64_t offset=((int64_t)page-1)*20;
	if(count<0 && null_token(&d,arr) && !more && offset>=total) count=0;
	if(count<0 || count>20 || count>total || (count==0 && (more || offset<total)) ||
	   (count>0 && offset+count>total) || (!more && offset+count<total) ||
	   (more && offset+count>=total)) goto invalid;
	info->total=total;info->has_more=more;
	int at=arr+1;
	for(int i=0;i<count;i++) {
		BiliVideo v={0};int64_t type=0,num=0;v.views=-1;
		if(!number(&d,at,"type",&type)) goto invalid;
		if(type!=2) { at=skip(&d,at);continue; }
		/* Unavailable/deleted entries may lack bvid; retain server page metadata. */
		if(!string(&d,at,"bvid",v.bvid,sizeof(v.bvid)) || !v.bvid[0] ||
		   !string(&d,at,"title",v.title,sizeof(v.title)) || !v.title[0]) { at=skip(&d,at);continue; }
		if(!number(&d,at,"id",&v.aid) || v.aid<=0) goto invalid;
		string(&d,at,"cover",v.pic,sizeof(v.pic));image_url(v.pic,sizeof(v.pic));
		string(&d,field(&d,at,"upper"),"name",v.author,sizeof(v.author));
		if(number(&d,at,"duration",&num) && num>=0 && num<=INT_MAX) v.duration=(int)num;
		if(number(&d,at,"page",&num) && num>0 && num<=INT_MAX) v.pages=(int)num;
		if(number(&d,field(&d,at,"cnt_info"),"play",&num) && num>=0) v.views=num;
		if(info->count<max) out[info->count++]=v;else info->truncated=true;
		at=skip(&d,at);
	}
	release(&d);return r;
invalid:
	if(out && max>0) memset(out,0,(size_t)max*sizeof(*out));
	if(info) memset(info,0,sizeof(*info));
	r.outcome=BILI_API_UNKNOWN;release(&d);return r;
}
BiliApiResult api_response_like(int t,int s,const char *b,size_t n,BiliLikeState *state) {
	if(state) *state=BILI_LIKE_UNKNOWN;
	Document d;BiliApiResult r=envelope(t,s,b,n,&d);
	if(r.outcome==BILI_API_SUCCESS) {
		int64_t v;
		if(!state || !number(&d,0,"data",&v) || (v!=0 && v!=1)) r.outcome=BILI_API_UNKNOWN;
		else *state=v==1?BILI_LIKE_CONFIRMED:BILI_LIKE_NOT_REPORTED;
	}
	release(&d);return r;
}

/* Browse responses use the same envelope/grammar checks as account actions. */
static void plain_text(char *s) {
    char *to=s;bool tag=false;
    for(char *p=s;*p;p++) {
        if(*p=='<') tag=true;
        else if(*p=='>') tag=false;
        else if(!tag) *to++=*p;
    }
    *to=0;
}
static bool optional_text(const Document *d,int obj,const char *key,char *out,size_t n) {
    int v=field(d,obj,key);
    return v==-1 || (v>=0 && d->tokens[v].type==JSMN_STRING && json_get_str(d->json,v,"",out,n));
}
static bool optional_number(const Document *d,int obj,const char *key,int64_t *out) {
    int v=field(d,obj,key);return v==-1 || integer(d,v,out);
}
static bool profile_item(const Document *d,int at,bool own,bool search,BiliProfile *p) {
    memset(p,0,sizeof(*p));p->fans=p->videos=-1;p->level=-1;
    int64_t level=-1;
    if(!number(d,at,"mid",&p->mid) || p->mid<=0 ||
       !string(d,at,own||search?"uname":"name",p->name,sizeof(p->name)) ||
       !optional_text(d,at,search?"usign":"sign",p->sign,sizeof(p->sign)) ||
       !optional_text(d,at,search?"upic":"face",p->face,sizeof(p->face))) return false;
    if(own) {
        if(!number(d,field(d,at,"level_info"),"current_level",&level)) return false;
    } else if(!optional_number(d,at,"level",&level)) return false;
    if(level < -1 || level>INT_MAX || !optional_number(d,at,"fans",&p->fans) ||
       !optional_number(d,at,"videos",&p->videos) || p->fans < -1 || p->videos < -1) return false;
    p->level=(int)level;plain_text(p->name);plain_text(p->sign);image_url(p->face,sizeof(p->face));
    return p->name[0]!=0;
}
BiliApiResult api_response_profile(int t,int s,const char *b,size_t n,bool own,BiliProfile *out) {
    if(out) memset(out,0,sizeof(*out));
    Document d;BiliApiResult r=envelope(t,s,b,n,&d);
    if(r.outcome==BILI_API_SUCCESS) {
        int data=field(&d,0,"data");bool logged=false;
        if(own && boolean(&d,field(&d,data,"isLogin"),&logged) && !logged) {
            r.outcome=BILI_API_REJECTED;r.login_required=true;
        } else if(!out || (own && !logged) || !profile_item(&d,data,own,false,out)) {
            r.outcome=BILI_API_UNKNOWN;
        }
    }
    if(r.outcome!=BILI_API_SUCCESS && out) memset(out,0,sizeof(*out));
    release(&d);return r;
}
static void clear_videos(BiliVideo *out,int max,BiliPageInfo *info) {
    if(out && max>0) memset(out,0,(size_t)max*sizeof(*out));
    if(info) memset(info,0,sizeof(*info));
}
static int array_size(const Document *d,int at,bool empty) {
    if(at>=0 && d->tokens[at].type==JSMN_ARRAY) return d->tokens[at].size;
    return empty && null_token(d,at)?0:-1;
}
static bool browse_video(const Document *d,int at,bool watch,BiliVideo *v) {
    memset(v,0,sizeof(*v));v->views=-1;
    if(!number(d,at,"aid",&v->aid) || v->aid<=0) return false;
    int64_t value=0;
    if(!optional_number(d,at,"state",&value)) return false;
    if(value<0) return true; /* unavailable resource is filtered by empty bvid */
    if(!optional_text(d,at,"bvid",v->bvid,sizeof(v->bvid)) ||
       !optional_text(d,at,"title",v->title,sizeof(v->title)) ||
       !optional_text(d,at,"pic",v->pic,sizeof(v->pic))) return false;
    plain_text(v->title);image_url(v->pic,sizeof(v->pic));
    if(watch) {
        if(!optional_text(d,field(d,at,"owner"),"name",v->author,sizeof(v->author)) ||
           !optional_number(d,at,"cid",&v->cid) || v->cid<0) return false;
        value=0;
        if(!optional_number(d,at,"count",&value) || value<0 || value>INT_MAX) return false;
        v->pages=(int)value;value=0;
        if(!optional_number(d,at,"duration",&value) || value<0 || value>INT_MAX) return false;
        v->duration=(int)value;
        if(!optional_number(d,field(d,at,"stat"),"view",&v->views) || v->views < -1) return false;
    } else {
        char length[24]={0};
        if(!optional_text(d,at,"author",v->author,sizeof(v->author)) ||
           !optional_text(d,at,"length",length,sizeof(length)) ||
           !optional_number(d,at,"play",&v->views) || v->views < -1) return false;
        /* MM:SS (minutes may exceed 59); reject overflow and malformed lengths. */
        if(length[0]) {
            int64_t minutes=0,seconds=0;const char *p=length;
            if(*p<'0' || *p>'9') return false;
            while(*p>='0' && *p<='9') { minutes=minutes*10+*p++-'0';if(minutes>INT_MAX/60) return false; }
            if(*p++!=':' || p[0]<'0' || p[0]>'5' || p[1]<'0' || p[1]>'9' || p[2]) return false;
            seconds=(p[0]-'0')*10+p[1]-'0';
            if(minutes*60+seconds>INT_MAX) return false;
            v->duration=(int)(minutes*60+seconds);
        }
    }
    return true;
}
BiliApiResult api_response_watchlater(int t,int s,const char *b,size_t n,int page,
    BiliVideo *out,int max,BiliPageInfo *info,int64_t aid,bool *found) {
    clear_videos(out,max,info);if(found) *found=false;
    Document d;BiliApiResult r=envelope(t,s,b,n,&d);
    if(r.outcome!=BILI_API_SUCCESS) { release(&d);return r; }
    int data=field(&d,0,"data"),arr=field(&d,data,"list");int64_t total;
    if(!out || max<=0 || !info || page<=0 || !number(&d,data,"count",&total) || total<0) goto invalid;
    int count=array_size(&d,arr,total==0);
    if(count<0 || count!=total) goto invalid; /* endpoint promises the entire list */
    int64_t offset=((int64_t)page-1)*20;
    info->total=total;info->has_more=offset+20<total;
    bool match=false;int at=arr+1;
    for(int i=0;i<count;i++) {
        BiliVideo v;
        if(!browse_video(&d,at,true,&v)) goto invalid;
        if(aid>0 && v.aid==aid) match=true;
        if(i>=offset && i<offset+20 && v.bvid[0] && v.title[0]) {
            if(info->count<max) out[info->count++]=v;else info->truncated=true;
        }
        at=skip(&d,at);
    }
    if(found) *found=match;
    release(&d);return r;
invalid:
    clear_videos(out,max,info);r.outcome=BILI_API_UNKNOWN;release(&d);return r;
}
BiliApiResult api_response_users(int t,int s,const char *b,size_t n,int page,
    BiliProfile *out,int max,BiliPageInfo *info) {
    if(out && max>0) memset(out,0,(size_t)max*sizeof(*out));
    if(info) memset(info,0,sizeof(*info));
    Document d;BiliApiResult r=envelope(t,s,b,n,&d);
    if(r.outcome!=BILI_API_SUCCESS) { release(&d);return r; }
    int data=field(&d,0,"data"),arr=field(&d,data,"result");int64_t total,pn,ps,pages;
    if(!out || max<=0 || !info || page<=0 || !number(&d,data,"page",&pn) || pn!=page ||
       !number(&d,data,"pagesize",&ps) || ps!=20 || !number(&d,data,"numResults",&total) || total<0 ||
       !number(&d,data,"numPages",&pages) || pages<0 || pages!=(total/20+(total%20!=0))) goto invalid;
    int count=array_size(&d,arr,total==0);
    int64_t offset=((int64_t)page-1)*20;
    if(count<0 || count>20 || count>total || (count>0 && offset+count>total) || (count==0 && offset<total)) goto invalid;
    info->total=total;info->has_more=page<pages;
    int at=arr+1;
    for(int i=0;i<count;i++) {
        BiliProfile p;char type[24];
        if(!string(&d,at,"type",type,sizeof(type)) || strcmp(type,"bili_user") || !profile_item(&d,at,false,true,&p)) goto invalid;
        if(info->count<max) out[info->count++]=p;else info->truncated=true;
        at=skip(&d,at);
    }
    release(&d);return r;
invalid:
    if(out && max>0) memset(out,0,(size_t)max*sizeof(*out));
    if(info) memset(info,0,sizeof(*info));
    r.outcome=BILI_API_UNKNOWN;release(&d);return r;
}
BiliApiResult api_response_user_videos(int t,int s,const char *b,size_t n,int page,
    BiliVideo *out,int max,BiliPageInfo *info) {
    clear_videos(out,max,info);
    Document d;BiliApiResult r=envelope(t,s,b,n,&d);
    if(r.outcome!=BILI_API_SUCCESS) { release(&d);return r; }
    int data=field(&d,0,"data"),meta=field(&d,data,"page"),arr=field(&d,field(&d,data,"list"),"vlist");
    int64_t pn,ps,total;
    if(!out || max<=0 || !info || page<=0 || !number(&d,meta,"pn",&pn) || pn!=page ||
       !number(&d,meta,"ps",&ps) || ps!=20 || !number(&d,meta,"count",&total) || total<0) goto invalid;
    int count=array_size(&d,arr,total==0);int64_t offset=((int64_t)page-1)*20;
    if(count<0 || count>20 || count>total || (count>0 && offset+count>total) || (count==0 && offset<total)) goto invalid;
    info->total=total;info->has_more=offset+20<total;
    int at=arr+1;
    for(int i=0;i<count;i++) {
        BiliVideo v;if(!browse_video(&d,at,false,&v)) goto invalid;
        if(v.bvid[0] && v.title[0]) {
            if(info->count<max) out[info->count++]=v;else info->truncated=true;
        }
        at=skip(&d,at);
    }
    release(&d);return r;
invalid:
    clear_videos(out,max,info);r.outcome=BILI_API_UNKNOWN;release(&d);return r;
}


/* Only parse IDs from known Bilibili playback links, never fetch a redirect. */
static int64_t history_link_id(const char *url,const char *kind) {
    const char *prefixes[]={"https://www.bilibili.com/bangumi/play/","http://www.bilibili.com/bangumi/play/","//www.bilibili.com/bangumi/play/"};
    const char *p=NULL;
    for(unsigned i=0;i<sizeof(prefixes)/sizeof(prefixes[0]);i++)
        if(!strncmp(url,prefixes[i],strlen(prefixes[i]))){p=url+strlen(prefixes[i]);break;}
    if(!p || strncmp(p,kind,2))return 0;
    p+=2;if(*p<'0'||*p>'9')return 0;
    int64_t id=0;
    while(*p>='0'&&*p<='9'){
        int digit=*p++-'0';if(id>(INT64_MAX-digit)/10)return 0;id=id*10+digit;
    }
    return *p && *p!='?' && *p!='#' && *p!='/' ? 0 : id;
}
BiliApiResult api_response_history(int t,int s,const char *b,size_t n,
    BiliVideo *out,int max,int *count) {
    if(count)*count=0;
    if(out&&max>0)memset(out,0,(size_t)max*sizeof(*out));
    Document d;BiliApiResult r=envelope(t,s,b,n,&d);
    if(r.outcome!=BILI_API_SUCCESS){release(&d);return r;}
    int arr=field(&d,0,"data");
    if(arr>=0&&d.tokens[arr].type==JSMN_OBJECT)arr=field(&d,arr,"list");
    if(!out||max<1||!count||arr<0||d.tokens[arr].type!=JSMN_ARRAY)goto invalid;
    int at=arr+1;
    for(int i=0;i<d.tokens[arr].size;i++,at=skip(&d,at)){
        if(d.tokens[at].type!=JSMN_OBJECT)goto invalid;
        BiliVideo v={.views=-1};int64_t duration=0,pages=0;char redirect[768]={0},business[32]={0};
        int hist=field(&d,at,"history"),bangumi=field(&d,at,"bangumi"),season=field(&d,at,"season"),owner=field(&d,at,"owner");
        if(!optional_text(&d,at,"title",v.title,sizeof(v.title))||
           !optional_text(&d,at,"bvid",v.bvid,sizeof(v.bvid))||
           !optional_text(&d,at,"pic",v.pic,sizeof(v.pic))||
           !optional_text(&d,at,"redirect_url",redirect,sizeof(redirect))||
           !optional_number(&d,at,"aid",&v.aid)||v.aid<0||
           !optional_number(&d,at,"cid",&v.cid)||v.cid<0||
           !optional_number(&d,at,"duration",&duration)||duration<0||duration>INT_MAX||
           !optional_number(&d,at,"videos",&pages)||pages<0||pages>INT_MAX)goto invalid;
        if(!v.pic[0])optional_text(&d,at,"cover",v.pic,sizeof(v.pic));
        if(hist>=0){
            if(!optional_text(&d,hist,"business",business,sizeof(business))||
               !optional_number(&d,hist,"epid",&v.ep_id)||v.ep_id<0)goto invalid;
            if(!v.cid)optional_number(&d,hist,"cid",&v.cid);
            if(!v.aid)optional_number(&d,hist,"oid",&v.aid);
        }
        if(!v.ep_id&&(!optional_number(&d,at,"ep_id",&v.ep_id)||v.ep_id<0))goto invalid;
        if(!v.ep_id&&(!optional_number(&d,bangumi,"ep_id",&v.ep_id)||v.ep_id<0))goto invalid;
        if(!optional_number(&d,season,"season_id",&v.season_id)||v.season_id<0)goto invalid;
        if(!v.season_id&&(!optional_number(&d,at,"season_id",&v.season_id)||v.season_id<0))goto invalid;
        if(!v.season_id&&(!optional_number(&d,bangumi,"season_id",&v.season_id)||v.season_id<0))goto invalid;
        if(!v.ep_id)v.ep_id=history_link_id(redirect,"ep");
        if(!v.season_id)v.season_id=history_link_id(redirect,"ss");
        /* UGC collections also carry season_id. It is not a PGC type tag.
         * An explicit archive business wins; legacy records need an episode,
         * a bangumi object, or a validated bangumi playback URL. */
        int64_t bangumi_season=0;
        if(!optional_number(&d,bangumi,"season_id",&bangumi_season)||bangumi_season<0)goto invalid;
        v.is_pgc=!strcmp(business,"pgc") || (!business[0] &&
            (v.ep_id>0 || bangumi_season>0 || history_link_id(redirect,"ss")>0));
        if(!v.is_pgc){v.ep_id=0;v.season_id=0;}
        if(business[0]&&strcmp(business,"pgc")&&strcmp(business,"archive"))continue;
        if(!optional_text(&d,owner,"name",v.author,sizeof(v.author)))goto invalid;
        if(!v.author[0])optional_text(&d,at,"author_name",v.author,sizeof(v.author));
        optional_number(&d,owner,"mid",&v.owner_mid);
        optional_number(&d,field(&d,at,"stat"),"view",&v.views);
        v.duration=(int)duration;v.pages=(int)pages;plain_text(v.title);image_url(v.pic,sizeof(v.pic));
        if(!v.title[0]||(!v.is_pgc&&!v.bvid[0]))continue;
        if(*count<max)out[(*count)++]=v;
    }
    release(&d);return r;
invalid:
    if(count)*count=0;
    if(out&&max>0)memset(out,0,(size_t)max*sizeof(*out));
    r.outcome=BILI_API_UNKNOWN;release(&d);return r;
}
