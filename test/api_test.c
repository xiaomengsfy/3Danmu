/* Compile production net.c with fake httpc only. No live network/cookies.
 * TEST_OLD_POST demonstrates the unsafe legacy path before implementation. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include "../source/net.h"
#include "../source/api_response.h"
#include "../source/bili_transport.h"
Result svcSleepThread(int64_t ns);
#define UI_H
#define mkdir(path, mode) ((void)0)
#include "../source/net.c"
#undef mkdir
#ifndef TEST_OLD_POST
#include "../source/bili.c"
#endif

static int failures, checks, opens, begins, resets, closes, locked;
static int fail_step, http_status;
static bool cancel_during_pause;
static const char *response_body;
static u32 downloaded;
static bool fail_alloc;
static bool stall_body;
static u64 fake_time,time_advance;
static httpcContext *tracked_stream;
static int cancel_during_begin,api_cancels,stream_cancels;
static bool opening_stream;
static char sent_body[2560];
static char request_url[512];
#define CHECK(c, text) do { checks++; if (!(c)) { fprintf(stderr,"FAIL: %s (%d)\n",text,__LINE__); failures++; } } while (0)

void LightLock_Init(LightLock *p) { *p=0; }
void LightLock_Lock(LightLock *p) { ++*p; ++locked; }
void LightLock_Unlock(LightLock *p) { --*p; --locked; }
Result httpcInit(u32 n) { (void)n; resets++; return 0; }
void httpcExit(void) {}
Result httpcOpenContext(httpcContext *c,HTTPC_RequestMethod m,const char *url,u32 p) {
	(void)p; opens++; c->method=m; downloaded=0;snprintf(request_url,sizeof(request_url),"%s",url);
	if(!opening_stream) CHECK(locked > 0,"request must hold serialization lock");
	return fail_step==2 ? -1:0;
}
Result httpcCloseContext(httpcContext *c) { (void)c;closes++;return 0; }
Result httpcCancelConnection(httpcContext *c) { if(c==tracked_stream) stream_cancels++;else api_cancels++;return 0; }
Result httpcSetSSLOpt(httpcContext *c,u32 n) { (void)c;(void)n;return 0; }
Result httpcSetKeepAlive(httpcContext *c,u32 n) { (void)c;(void)n;return 0; }
Result httpcAddRequestHeaderField(httpcContext *c,const char *k,const char *v) { (void)c;(void)k;(void)v;return 0; }
Result httpcAddPostDataRaw(httpcContext *c,const u32 *p,u32 n) {
	(void)c; CHECK(n < sizeof(sent_body),"bounded submitted form");
	if(n<sizeof(sent_body)) { memcpy(sent_body,p,n);sent_body[n]=0; } return fail_step==7?-1:0;
}
Result httpcBeginRequest(httpcContext *c) {
	(void)c;begins++;
	if(cancel_during_begin==1) net_cancel_requests();
	if(cancel_during_begin==2) net_cancel_streams();
	return fail_step==3?-1:0;
}
Result httpcGetResponseStatusCodeTimeout(httpcContext *c,u32 *s,u64 t) { (void)c;CHECK(t>0,"bounded status wait");*s=http_status;return fail_step==4?-1:0; }
Result httpcGetResponseHeader(httpcContext *c,const char *name,char *p,u32 n) {
	(void)c; if (!strcmp(name,"Location") && http_status>=300 && http_status<400) { snprintf(p,n,"https://example.test/redirect");return 0; } return -1;
}
Result httpcDownloadData(httpcContext *c,u8 *p,u32 n,u32 *got) {
	(void)c; size_t remain=strlen(response_body)-downloaded;
	if(stall_body) { *got=0;return HTTPC_RESULTCODE_DOWNLOADPENDING; }
	*got=(u32)(remain<n?remain:n);memcpy(p,response_body+downloaded,*got);downloaded+=*got;
	return fail_step==6?-1:(downloaded<strlen(response_body)?HTTPC_RESULTCODE_DOWNLOADPENDING:0);
}
Result httpcReceiveDataTimeout(httpcContext *c,u8 *p,u32 n,u64 t) { u32 got;CHECK(t>0,"bounded body wait");return httpcDownloadData(c,p,n,&got); }
Result httpcGetDownloadSizeState(httpcContext *c,u32 *got,u32 *total) { (void)c;*got=downloaded;*total=(u32)strlen(response_body);return 0; }
void *linearAlloc(size_t n) { return fail_alloc?NULL:malloc(n); }
void linearFree(void *p) { free(p); }
u64 osGetTime(void) { fake_time+=time_advance;return fake_time; }
void ui_printf(const char *s,...) { (void)s; }
void ui_trace(const char *s,...) { (void)s; }
void ui_log_ascii(const char *s,const char *v,int n) { (void)s;(void)v;(void)n; }
#ifndef TEST_OLD_POST
int tls_init(void) { return -1; }
int tls_get(const char *host,const char *path,const char *cookie,int *status,
            char *headers,size_t hn,char *body,size_t bn) {
	(void)host;(void)path;(void)cookie;(void)status;(void)headers;(void)hn;(void)body;(void)bn;
	CHECK(false,"unrelated TLS/account endpoint must not run in API tests");return -1;
}
#endif

static void reset_transport(void);
Result svcSleepThread(int64_t ns){
 fake_time+=(u64)(ns/1000000);if(cancel_during_pause){cancel_during_pause=false;net_cancel_requests();}return 0;
}
static void test_read_pressure(void){
 const char *k[]={"media_id","pn","ps","platform"},*v[]={"1700184234","1","20","web"};HttpResponse res;BiliApiResult r;
 reset_transport();fake_time=100000;
 r=bili_fetch_json("/x/v3/fav/resource/list",k,v,4,false,&res);CHECK(r.outcome==BILI_API_SUCCESS,"first paced favorite read");net_response_free(&res);u64 first=fake_time;
 r=bili_fetch_json("/x/v3/fav/resource/list",k,v,4,false,&res);CHECK(r.outcome==BILI_API_SUCCESS&&fake_time>=first+1000,"favorite list reads spaced at least one second");net_response_free(&res);
 int old=opens;cancel_during_pause=true;r=bili_fetch_json("/x/v3/fav/resource/list",k,v,4,false,&res);
 CHECK(r.outcome==BILI_API_UNKNOWN&&opens==old,"B cancels favorite pacing without opening a request");net_response_free(&res);
 fake_time+=2000;http_status=412;r=bili_fetch_json("/x/v3/fav/resource/list",k,v,4,false,&res);CHECK(r.http_status==412,"412 classified");net_response_free(&res);old=opens;
 http_status=200;r=bili_fetch_json("/x/web-interface/view",NULL,NULL,0,false,&res);
 CHECK(r.http_status==412&&opens==old,"412 cooldown also blocks immediate metadata request without network");net_response_free(&res);
 fake_time+=60001;r=bili_fetch_json("/x/web-interface/view",NULL,NULL,0,false,&res);
 CHECK(r.outcome==BILI_API_SUCCESS&&opens==old+1,"cooldown permits one read after expiry");net_response_free(&res);
 http_status=429;r=bili_fetch_json("/x/test",NULL,NULL,0,false,&res);net_response_free(&res);old=opens;
 http_status=200;r=bili_fetch_json("/x/test",NULL,NULL,0,false,&res);CHECK(r.http_status==429&&opens==old,"429 cooldown no automatic retry");net_response_free(&res);fake_time+=60001;
}
static void reset_transport(void) {
	opens=begins=resets=closes=0;fail_step=0;http_status=200;
	response_body="{\"code\":0}";sent_body[0]=0;fail_alloc=false;
	stall_body=false;fake_time=1000;time_advance=0;
	cancel_during_begin=api_cancels=stream_cancels=0;
}
static void test_once(void) {
	const char *keys[]={"aid","like","csrf"},*vals[]={"123","2","synthetic +&"};
	HttpResponse r;
#ifdef TEST_OLD_POST
#define net_post_fields_once net_post_fields
#endif
	for(int step=2;step<=6;step++) {
		if(step==5) continue;
		reset_transport();fail_step=step;
		CHECK(net_post_fields_once("https://example.test/like",keys,vals,3,&r)<0,"transport failure returned");
		CHECK(opens==1,"write opens exactly once on transport failure");
		CHECK(begins<=1,"write begins at most once");
		CHECK(resets==0,"write never resets service for retry");
		CHECK(locked==0,"write releases request lock");net_response_free(&r);
	}
	for(int status=301;status<=308;status++) {
		reset_transport();http_status=status;
		CHECK(net_post_fields_once("https://example.test/like",keys,vals,3,&r)==0,"HTTP redirect is delivered for classification");
		CHECK(opens==1 && begins==1,"redirect never followed or replayed");
		CHECK(r.status==status,"original redirect status preserved");net_response_free(&r);
	}
	reset_transport();CHECK(net_post_fields_once("https://example.test/like",keys,vals,3,&r)==0,"success transport");
	CHECK(!strcmp(sent_body,"aid=123&like=2&csrf=synthetic%20%2B%26"),"explicit unlike fields encoded intact");net_response_free(&r);
	reset_transport();fail_alloc=true;
	CHECK(net_post_fields_once("https://example.test/like",keys,vals,3,&r)<0 && begins==0,"allocation failure never submits empty write");
	reset_transport();fail_step=7;
	CHECK(net_post_fields_once("https://example.test/like",keys,vals,3,&r)<0 && begins==0,"body setup failure never begins write");
	reset_transport();fail_step=4;
	CHECK(net_get("https://example.test/read",&r)<0 && opens==2 && resets==1,"existing read retry preserved");
#ifndef TEST_OLD_POST
	reset_transport();stall_body=true;time_advance=5000;
	CHECK(net_post_fields_once("https://example.test/like",keys,vals,3,&r)<0 && opens==1 && locked==0,"stalled response expires without replay");
	reset_transport();char *large=malloc(1024u*1024+2);memset(large,'x',1024u*1024+1);large[1024u*1024+1]=0;response_body=large;
	CHECK(net_post_fields_once("https://example.test/like",keys,vals,3,&r)<0 && opens==1 && !r.data,"oversized write reply bounded without replay");free(large);
	reset_transport();char too_long[800];memset(too_long,'&',sizeof(too_long)-1);too_long[sizeof(too_long)-1]=0;const char *oversized[]={too_long};
	CHECK(net_post_fields_once("https://example.test/like",keys,oversized,1,&r)<0 && begins==0,"form truncation rejected before write");
	reset_transport();s_shutdown=true;memset(&r,0x55,sizeof(r));
	CHECK(net_post_fields_once("https://example.test/like",keys,vals,3,&r)<0 && opens==0 && !r.data && !r.len,"shutdown returns initialized response without request");s_shutdown=false;
#endif
}

#ifndef TEST_OLD_POST
static void test_cancel_isolation(void) {
	NetStream stream;HttpResponse response;
	const char *keys[]={"aid"},*vals[]={"123"};
	reset_transport();opening_stream=true;tracked_stream=&stream.ctx;
	CHECK(ns_open(&stream,"https://example.test/stream",0)==0,"synthetic playback stream opens");
	opening_stream=false;cancel_during_begin=1;
	CHECK(net_post_fields_once("https://example.test/action",keys,vals,1,&response)==0,"synthetic API enters registered begin");
	CHECK(api_cancels==1 && stream_cancels==0,"API modal cancel leaves playback stream alive");
	net_response_free(&response);
	api_cancels=stream_cancels=0;cancel_during_begin=2;
	CHECK(net_post_fields_once("https://example.test/action",keys,vals,1,&response)==0,"synthetic stream cancellation during API");
	CHECK(stream_cancels==1 && api_cancels==0,"stream cancellation does not touch API request");
	net_response_free(&response);cancel_during_begin=0;
	api_cancels=stream_cancels=0;net_cancel_img();
	CHECK(api_cancels==0 && stream_cancels==0,"image cancellation does not touch playback stream");
	net_shutdown_begin();
	CHECK(stream_cancels==1,"shutdown still cancels playback stream");
	ns_close(&stream);s_shutdown=false;tracked_stream=NULL;
	CHECK(s_streams==0 && locked==0,"cancel test releases stream registry and locks");
}
static void test_clients(void) {
	BiliApiResult r;BiliPageInfo info;BiliVideo videos[20];BiliFavFolder folders[2];
	net_clear_cookies();s_logged_in=true;s_mid=123456789012LL;
	net_set_cookie("bili_jct","synthetic-token");
	reset_transport();r=bili_like_set(456,true);
	CHECK(r.outcome==BILI_API_SUCCESS && opens==1 && begins==1,"like client sends one write");
	CHECK(!strcmp(sent_body,"aid=456&like=1&csrf=synthetic-token"),"like has explicit intended command");
	reset_transport();r=bili_like_set(456,false);
	CHECK(r.outcome==BILI_API_SUCCESS && !strcmp(sent_body,"aid=456&like=2&csrf=synthetic-token"),"unlike has explicit intended command");
	reset_transport();fail_step=4;r=bili_like_set(456,true);
	CHECK(r.outcome==BILI_API_UNKNOWN && opens==1 && begins==1,"lost acknowledgement stays unknown without resend");
	reset_transport();response_body="{\"code\":0,\"data\":{\"count\":0,\"list\":null}}";
	r=bili_fav_folders(folders,2,&info);
	CHECK(r.outcome==BILI_API_SUCCESS && !strcmp(request_url,"https://api.bilibili.com/x/v3/fav/folder/created/list-all?up_mid=123456789012"),"folder client uses full own account id");
	reset_transport();response_body="{\"code\":0,\"data\":{\"info\":{\"media_count\":0},\"medias\":null,\"has_more\":false}}";
	r=bili_fav_folder(987654321012LL,2,videos,20,&info);
	CHECK(r.outcome==BILI_API_SUCCESS && !strcmp(request_url,"https://api.bilibili.com/x/v3/fav/resource/list?media_id=987654321012&pn=2&ps=20&platform=web"),"selected folder page uses explicit id and ps20");
	reset_transport();BiliLikeState state;response_body="{\"code\":0,\"data\":0}";
	r=bili_like_status(456,&state);
	CHECK(r.outcome==BILI_API_SUCCESS && state==BILI_LIKE_NOT_REPORTED && begins==1 && !strcmp(request_url,"https://api.bilibili.com/x/web-interface/archive/has/like?aid=456"),"readback never sends POST or claims historical unlike");
	reset_transport();net_clear_cookies();r=bili_like_set(456,true);
	CHECK(r.outcome==BILI_API_REJECTED && opens==0,"missing CSRF rejected before transport");
	reset_transport();s_logged_in=false;r=bili_fav_folders(folders,2,&info);
	CHECK(r.outcome==BILI_API_REJECTED && r.login_required && opens==0 && info.count==0,"logged-out read cannot reuse own-account cache");
	reset_transport();s_logged_in=true;s_mid=123;net_set_cookie("bili_jct","synthetic-token");response_body="{\"code\":-101}";
	r=bili_like_set(456,true);
	CHECK(r.login_required && !bili_logged_in() && s_mid==0,"expired response invalidates account cache");
}
static void test_results(void) {
	struct {int transport,status;const char *body;BiliApiOutcome want;} cases[]={
		{0,200,"{\"code\":0}",BILI_API_SUCCESS},
		{0,200,"{\"code\":-101,\"message\":\"login\"}",BILI_API_REJECTED},
		{0,200,"{\"code\":-111}",BILI_API_REJECTED},
		{0,200,"{\"code\":65006}",BILI_API_REJECTED},
		{0,200,"",BILI_API_UNKNOWN}, {0,200,"{}",BILI_API_UNKNOWN},
		{0,200,"{\"code\":0",BILI_API_UNKNOWN},
		{0,200,"{\"code\":\"0\"}",BILI_API_UNKNOWN},
		{0,200,"{\"code\":0.1}",BILI_API_UNKNOWN},
		{0,200,"{\"code\":0junk}",BILI_API_UNKNOWN},
		{0,200,"{\"code\" 0}",BILI_API_UNKNOWN},
		{0,200,"{\"code\":0,}",BILI_API_UNKNOWN},
		{0,200,"{\"code\":0} trailing",BILI_API_UNKNOWN},
		{0,200,"{\"code\":0,\"code\":-101}",BILI_API_UNKNOWN},
		{0,200,"{\"code\":0,\"data\":{\"v_voucher\":\"challenge\"}}",BILI_API_REJECTED},
		{0,200,"{\"code\":0,\"data\":{\"gaia_voucher\":\"challenge\"}}",BILI_API_REJECTED},
		{-4,200,"{\"code\":0}",BILI_API_UNKNOWN},
		{0,302,"{\"code\":0}",BILI_API_UNKNOWN},
		{0,500,"{\"code\":0}",BILI_API_UNKNOWN},
		{0,403,"<html>denied</html>",BILI_API_REJECTED},
		{0,401,"",BILI_API_REJECTED},
		{0,408,"",BILI_API_UNKNOWN}
		,{0,200,"{\"code\\u0000x\":0}",BILI_API_UNKNOWN}
		,{0,200,"{\"code\":0,\"data\":{},\"data\":{\"v_voucher\":\"x\"}}",BILI_API_UNKNOWN}
		,{0,200,"{\"code\":0,\"v_voucher\":\"\",\"v_voucher\":\"x\"}",BILI_API_UNKNOWN}
		,{0,200,"{\"code\":9223372036854775808}",BILI_API_UNKNOWN}
		,{0,200,"{\"code\":true}",BILI_API_UNKNOWN}
		,{0,200,"{\"code\":null}",BILI_API_UNKNOWN}
	};
	for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
		BiliApiResult r=api_response_result(cases[i].transport,cases[i].status,cases[i].body,strlen(cases[i].body));
		CHECK(r.outcome==cases[i].want,"strict operation outcome");
		if(i==1 || i==20) CHECK(r.login_required,"expired login identified");
	}
}
static void test_nav_identity(void) {
	reset_transport();s_logged_in=true;s_mid=123456789012LL;
	snprintf(s_uname,sizeof(s_uname),"%s","synthetic-user");
	http_status=500;
	CHECK(fetch_nav()<0,"nav server failure is reported");
	CHECK(s_logged_in && s_mid==123456789012LL && !strcmp(s_uname,"synthetic-user"),"nav 5xx does not infer logout");
	reset_transport();fail_step=4;
	CHECK(fetch_nav()<0,"nav transport uncertainty is reported");
	CHECK(s_logged_in && s_mid==123456789012LL && !strcmp(s_uname,"synthetic-user"),"nav transport failure does not infer logout");
	reset_transport();http_status=401;
	CHECK(fetch_nav()<0,"nav definitive unauthorized is reported");
	CHECK(!bili_logged_in(),"nav 401 invalidates logged-in flag");
	CHECK(s_mid==0,"nav 401 clears previous account id");
	CHECK(!s_uname[0],"nav 401 clears previous username");
}
static void test_transport_helpers(void) {
    HttpResponse res;BiliApiResult r;const char *keys[]={"mid","keyword"},*vals[]={"123","a & b"};
    reset_transport();s_logged_in=true;s_mid=123;net_set_cookie("bili_jct","synthetic-token");
    r=bili_fetch_json("/x/test",keys,vals,2,false,&res);
    CHECK(r.outcome==BILI_API_SUCCESS && strstr(request_url,"mid=123&keyword=a%20%26%20b"),"shared GET encoded fields");net_response_free(&res);
    reset_transport();r=bili_fetch_json("https://evil.test/x",NULL,NULL,0,false,&res);
    CHECK(r.outcome==BILI_API_REJECTED && opens==0 && !res.data,"shared GET cannot send session to arbitrary host");
    reset_transport();r=bili_submit_json("/x/test",keys,vals,2,&res);
    CHECK(r.outcome==BILI_API_SUCCESS && opens==1 && !strcmp(sent_body,"mid=123&keyword=a%20%26%20b&csrf=synthetic-token"),"shared mutation adds session csrf once");net_response_free(&res);
    reset_transport();fail_step=4;r=bili_submit_json("/x/test",keys,vals,2,&res);
    CHECK(r.outcome==BILI_API_UNKNOWN && opens==1 && begins==1,"shared mutation never retries lost response");net_response_free(&res);
    reset_transport();const char *badkeys[]={"csrf"};r=bili_submit_json("/x/test",badkeys,vals,1,&res);
    CHECK(r.outcome==BILI_API_REJECTED && !opens,"caller cannot override shared session csrf");
    reset_transport();response_body="{\"code\":-101}";r=bili_fetch_json("/x/test",NULL,NULL,0,false,&res);
    CHECK(r.login_required && !bili_logged_in(),"shared helpers invalidate expired identity");net_response_free(&res);
}
static void test_browse_clients(void) {
    BiliProfile profiles[20];BiliVideo videos[20];BiliPageInfo info;BiliApiResult r;bool found=true;
    reset_transport();s_logged_in=false;
    CHECK(bili_account_id()==0,"logged-out account accessor hides stale mid");
    r=bili_watchlater(1,videos,20,&info);
    CHECK(r.login_required && opens==0 && !info.count,"Watch Later login guard clears output");
    reset_transport();s_logged_in=true;s_mid=123;net_set_cookie("bili_jct","synthetic-token");
    r=bili_watchlater_add(99);
    CHECK(r.outcome==BILI_API_SUCCESS && opens==1 && begins==1 && !strcmp(sent_body,"aid=99&csrf=synthetic-token") && strstr(request_url,"/x/v2/history/toview/add"),"Watch Later exact once add form and endpoint");
    reset_transport();fail_step=4;r=bili_watchlater_add(99);
    CHECK(r.outcome==BILI_API_UNKNOWN && begins==1 && opens==1,"Watch Later lost reply never resubmits");
    reset_transport();r=bili_watchlater_remove(99);
    CHECK(r.outcome==BILI_API_SUCCESS && opens==1 && begins==1 && !strcmp(sent_body,"aid=99&csrf=synthetic-token") && !strcmp(request_url,"https://api.bilibili.com/x/v2/history/toview/del"),"Watch Later removes one aid with exact endpoint and csrf");
    reset_transport();fail_step=4;r=bili_watchlater_remove(99);
    CHECK(r.outcome==BILI_API_UNKNOWN && begins==1 && opens==1,"Watch Later remove lost reply never resubmits");
    reset_transport();response_body="{\"code\":-400}";r=bili_watchlater_remove(99);
    CHECK(r.outcome==BILI_API_REJECTED && begins==1,"Watch Later remove rejection is not success");
    reset_transport();response_body="{}";r=bili_watchlater_remove(99);
    CHECK(r.outcome==BILI_API_UNKNOWN && begins==1,"Watch Later remove malformed reply remains unknown");
    reset_transport();r=bili_watchlater_remove(0);
    CHECK(r.outcome==BILI_API_REJECTED && !opens,"Watch Later remove rejects zero aid");
    reset_transport();r=bili_watchlater_remove(-1);
    CHECK(r.outcome==BILI_API_REJECTED && !opens,"Watch Later remove rejects negative aid");
    reset_transport();s_logged_in=false;r=bili_watchlater_remove(99);
    CHECK(r.login_required && !opens,"Watch Later remove requires login");s_logged_in=true;
    reset_transport();net_clear_cookies();r=bili_watchlater_remove(99);
    CHECK(r.outcome==BILI_API_REJECTED && !opens,"Watch Later remove requires csrf");
    reset_transport();net_clear_cookies();r=bili_watchlater_add(99);
    CHECK(r.outcome==BILI_API_REJECTED && opens==0,"Watch Later missing csrf cannot submit");
    reset_transport();response_body="{\"code\":0,\"data\":{\"count\":1,\"list\":[{\"aid\":99,\"bvid\":\"BV99\",\"title\":\"One\"}]}}";
    r=bili_watchlater_contains(99,&found);
    CHECK(r.outcome==BILI_API_SUCCESS && found && opens==1 && !strcmp(request_url,"https://api.bilibili.com/x/v2/history/toview"),"Watch Later membership uses full read-only list");
    reset_transport();response_body="{\"code\":0,\"data\":{\"isLogin\":true,\"mid\":123,\"uname\":\"Me\",\"level_info\":{\"current_level\":5}}}";
    r=bili_profile(profiles);
    CHECK(bili_account_id()==123,"profile publishes current account id for UI source binding");
    CHECK(r.outcome==BILI_API_SUCCESS && profiles[0].mid==123 && strstr(request_url,"/x/web-interface/nav"),"own center loads nav profile");
    snprintf(s_mixin,sizeof(s_mixin),"0123456789abcdef0123456789abcdef");
    reset_transport();response_body="{\"code\":0,\"data\":{\"page\":2,\"pagesize\":20,\"numResults\":0,\"numPages\":0,\"result\":[]}}";
    r=bili_search_users("A & B",2,profiles,20,&info);
    CHECK(r.outcome==BILI_API_SUCCESS && strstr(request_url,"/x/web-interface/wbi/search/type?") && strstr(request_url,"search_type=bili_user") && strstr(request_url,"keyword=A%20%26%20B") && strstr(request_url,"page=2") && strstr(request_url,"w_rid="),"UP search signs exact encoded keyword and page");
    reset_transport();response_body="{\"code\":0,\"data\":{\"mid\":42,\"name\":\"UP\",\"level\":5}}";
    r=bili_user_profile(42,profiles);
    CHECK(r.outcome==BILI_API_SUCCESS && strstr(request_url,"/x/space/wbi/acc/info?") && strstr(request_url,"mid=42") && strstr(request_url,"w_rid="),"UP profile signed");
    r=bili_user_profile(43,profiles);
    CHECK(r.outcome==BILI_API_UNKNOWN && !profiles[0].mid,"mismatched UP identity rejected");
    reset_transport();response_body="{\"code\":0,\"data\":{\"page\":{\"pn\":1,\"ps\":20,\"count\":0},\"list\":{\"vlist\":[]}}}";
    r=bili_user_videos(123456789012LL,1,videos,20,&info);
    CHECK(r.outcome==BILI_API_SUCCESS && strstr(request_url,"/x/space/wbi/arc/search?") && strstr(request_url,"mid=123456789012") && strstr(request_url,"ps=20") && strstr(request_url,"order=pubdate"),"UP submissions full identity and page size signed");
    reset_transport();response_body="{\"code\":-101}";r=bili_watchlater(1,videos,20,&info);
    CHECK(r.login_required && !bili_logged_in() && !s_mid && !info.count,"Watch Later expiry invalidates own cache");
}
static void test_browse(void) {
    BiliProfile users[2]; BiliVideo videos[20]; BiliPageInfo info; BiliApiResult r; bool found;
    const char *own="{\"code\":0,\"data\":{\"isLogin\":true,\"mid\":123456789012,\"uname\":\"My name\",\"face\":\"//example.test/a.jpg\",\"level_info\":{\"current_level\":6}}}";
    r=api_response_profile(0,200,own,strlen(own),true,users);
    CHECK(r.outcome==BILI_API_SUCCESS && users[0].mid==123456789012LL && users[0].level==6 && users[0].fans==-1,"own profile full id and level; absent counts unknown");
    CHECK(!strcmp(users[0].face,"https://example.test/a.jpg"),"profile image normalized");
    const char *loggedout="{\"code\":0,\"data\":{\"isLogin\":false}}";
    r=api_response_profile(0,200,loggedout,strlen(loggedout),true,users);
    CHECK(r.login_required && r.outcome==BILI_API_REJECTED && !users[0].mid,"logged-out profile clears old identity");
    const char *profile="{\"code\":0,\"data\":{\"mid\":42,\"name\":\"UP\",\"sign\":\"Hello\",\"level\":5}}";
    r=api_response_profile(0,200,profile,strlen(profile),false,users);
    CHECK(r.outcome==BILI_API_SUCCESS && users[0].mid==42 && !strcmp(users[0].sign,"Hello"),"public UP profile");
    const char *empty="{\"code\":0,\"data\":{\"count\":0,\"list\":[]}}";
    r=api_response_watchlater(0,200,empty,strlen(empty),1,videos,20,&info,44,&found);
    CHECK(r.outcome==BILI_API_SUCCESS && !info.count && !info.has_more && !found && !videos[0].aid,"empty Watch Later authoritative");
    char all[16000];size_t used=(size_t)snprintf(all,sizeof(all),"{\"code\":0,\"data\":{\"count\":21,\"list\":[");
    for(int k=1;k<=21;k++) used+=(size_t)snprintf(all+used,sizeof(all)-used,"%s{\"aid\":%d,\"bvid\":\"BV%d\",\"title\":\"Video\",\"owner\":{\"name\":\"UP\"},\"cid\":%d,\"count\":2,\"duration\":60}",k>1?",":"",k,k,k+100);
    snprintf(all+used,sizeof(all)-used,"]}}");
    r=api_response_watchlater(0,200,all,strlen(all),1,videos,20,&info,21,&found);
    CHECK(r.outcome==BILI_API_SUCCESS && info.count==20 && info.has_more && !info.truncated && found,"Watch Later first page and whole-list membership");
    r=api_response_watchlater(0,200,all,strlen(all),2,videos,20,&info,99,&found);
    CHECK(r.outcome==BILI_API_SUCCESS && info.count==1 && !info.has_more && videos[0].aid==21 && videos[0].cid==121 && videos[0].pages==2 && !found,"Watch Later raw 20 to21 pagination");
    const char *partial="{\"code\":0,\"data\":{\"count\":2,\"list\":[{\"aid\":1}]}}";
    r=api_response_watchlater(0,200,partial,strlen(partial),1,videos,20,&info,1,&found);
    CHECK(r.outcome==BILI_API_UNKNOWN && !info.count && !found && !videos[0].aid,"incomplete Watch Later not false final page or membership");
    const char *hidden="{\"code\":0,\"data\":{\"count\":1,\"list\":[{\"aid\":1,\"state\":-4}]}}";
    r=api_response_watchlater(0,200,hidden,strlen(hidden),1,videos,20,&info,1,&found);
    CHECK(r.outcome==BILI_API_SUCCESS && !info.count && info.total==1 && found,"unavailable watch entry preserves raw metadata");
    const char *search="{\"code\":0,\"data\":{\"page\":1,\"pagesize\":20,\"numResults\":21,\"numPages\":2,\"result\":[{\"type\":\"bili_user\",\"mid\":42,\"uname\":\"<em>UP</em>\",\"usign\":\"Hi\",\"fans\":88,\"videos\":3,\"level\":4}]}}";
    r=api_response_users(0,200,search,strlen(search),1,users,2,&info);
    CHECK(r.outcome==BILI_API_SUCCESS && info.count==1 && info.has_more && users[0].mid==42 && !strcmp(users[0].name,"UP") && users[0].fans==88,"UP search typed results and HTML removal");
    r=api_response_users(0,200,search,strlen(search),2,users,2,&info);
    CHECK(r.outcome==BILI_API_UNKNOWN && !info.count && !users[0].mid,"UP response page mismatch clears output");
    const char *sub="{\"code\":0,\"data\":{\"page\":{\"pn\":2,\"ps\":20,\"count\":21},\"list\":{\"vlist\":[{\"aid\":99,\"bvid\":\"BV99\",\"title\":\"Last\",\"author\":\"UP\",\"length\":\"22:11\",\"play\":999,\"pic\":\"//example.test/v.jpg\"}]}}}";
    r=api_response_user_videos(0,200,sub,strlen(sub),2,videos,20,&info);
    CHECK(r.outcome==BILI_API_SUCCESS && info.count==1 && !info.has_more && videos[0].duration==1331 && videos[0].views==999,"UP submission last page and duration");
    r=api_response_user_videos(0,200,"{\"code\":0,\"data\":{\"v_voucher\":\"challenge\"}}",45,1,videos,20,&info);
    CHECK(r.outcome!=BILI_API_SUCCESS && !info.count && !videos[0].aid,"challenge is not empty UP page");
}

static void test_pages(void) {
	BiliFavFolder folders[2];BiliPageInfo info;BiliVideo videos[20];BiliApiResult r;
	const char *empty="{\"code\":0,\"data\":{\"count\":0,\"list\":null}}";
	memset(folders,0x55,sizeof(folders));memset(&info,0x55,sizeof(info));
	r=api_response_folders(0,200,empty,strlen(empty),folders,2,&info);
	CHECK(r.outcome==BILI_API_SUCCESS && info.count==0 && info.total==0 && !info.has_more && !info.truncated,"empty created-folder list valid");
	CHECK(folders[0].id==0,"old folder cleared on empty");
	const char *all="{\"code\":0,\"data\":{\"count\":3,\"list\":[{\"id\":11,\"title\":\"A\",\"media_count\":2},{\"id\":22,\"title\":\"B\",\"media_count\":0},{\"id\":33,\"title\":\"C\",\"media_count\":9}]}}";
	r=api_response_folders(0,200,all,strlen(all),folders,2,&info);
	CHECK(r.outcome==BILI_API_SUCCESS && info.count==2 && info.total==3 && info.truncated,"all folders parsed with explicit capacity truncation");
	CHECK(folders[1].id==22 && !strcmp(folders[1].title,"B"),"second folder preserved");
	const char *page="{\"code\":0,\"data\":{\"info\":{\"media_count\":21},\"medias\":[{\"type\":2,\"id\":99,\"bvid\":\"BV123\",\"title\":\"Test\",\"upper\":{\"name\":\"Maker\"},\"page\":2,\"duration\":60,\"cover\":\"//example.test/p.png\"}],\"has_more\":true}}";
	r=api_response_favorites(0,200,page,strlen(page),1,videos,20,&info);
	CHECK(r.outcome==BILI_API_SUCCESS && info.count==1 && info.total==21 && info.has_more,"server has_more wins over sparse usable item count");
	CHECK(videos[0].aid==99 && videos[0].pages==2 && !strcmp(videos[0].pic,"https://example.test/p.png"),"favorite video metadata parsed");
	const char *last="{\"code\":0,\"data\":{\"info\":{\"media_count\":0},\"medias\":null,\"has_more\":false}}";
	r=api_response_favorites(0,200,last,strlen(last),1,videos,20,&info);
	CHECK(r.outcome==BILI_API_SUCCESS && info.count==0 && !info.has_more && videos[0].aid==0,"empty favorite clears previous page");
	const char *bad="{\"code\":0,\"data\":{\"info\":{\"media_count\":0},\"medias\":null,\"has_more\":true}}";
	r=api_response_favorites(0,200,bad,strlen(bad),1,videos,20,&info);
	CHECK(r.outcome==BILI_API_UNKNOWN && info.count==0,"contradictory empty page rejected");
	const char *false_end="{\"code\":0,\"data\":{\"info\":{\"media_count\":21},\"medias\":[{\"type\":12}],\"has_more\":false}}";
	r=api_response_favorites(0,200,false_end,strlen(false_end),1,videos,20,&info);
	CHECK(r.outcome==BILI_API_UNKNOWN && info.count==0,"false final-page claim conflicts with total");
	const char *beyond="{\"code\":0,\"data\":{\"info\":{\"media_count\":1},\"medias\":[{\"type\":12}],\"has_more\":false}}";
	r=api_response_favorites(0,200,beyond,strlen(beyond),2,videos,20,&info);
	CHECK(r.outcome==BILI_API_UNKNOWN && info.count==0,"nonempty beyond-total page rejected");
	const char *last21="{\"code\":0,\"data\":{\"info\":{\"media_count\":21},\"medias\":[{\"type\":12}],\"has_more\":false}}";
	r=api_response_favorites(0,200,last21,strlen(last21),2,videos,20,&info);
	CHECK(r.outcome==BILI_API_SUCCESS && info.count==0 && info.total==21 && !info.has_more,"last source entry may be non-video without being an error");
	r=api_response_favorites(0,200,"{\"code\":0}",10,1,videos,20,&info);
	CHECK(r.outcome==BILI_API_UNKNOWN && info.count==0 && videos[0].aid==0,"missing page data not empty success");
	BiliLikeState state=BILI_LIKE_CONFIRMED;
	r=api_response_like(0,200,"{\"code\":0,\"data\":0}",19,&state);
	CHECK(r.outcome==BILI_API_SUCCESS && state==BILI_LIKE_NOT_REPORTED,"zero readback is not definitive historical unlike");
	r=api_response_like(0,200,"{\"code\":0,\"data\":1}",19,&state);
	CHECK(r.outcome==BILI_API_SUCCESS && state==BILI_LIKE_CONFIRMED,"one readback confirms like");
	r=api_response_like(0,200,"{\"code\":0,\"data\":2}",19,&state);
	CHECK(r.outcome==BILI_API_UNKNOWN && state==BILI_LIKE_UNKNOWN,"invalid readback clears stale like");
}
#endif
int main(void) {
	net_init();test_once();
#ifndef TEST_OLD_POST
	test_results();test_pages();test_browse();test_browse_clients();test_transport_helpers();test_clients();test_nav_identity();test_cancel_isolation();test_read_pressure();
#endif
	fprintf(stdout,"API tests: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
