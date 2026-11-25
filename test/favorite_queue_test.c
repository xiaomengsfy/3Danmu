#include "favorite_queue.h"
#include "bili_transport.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static int64_t account=99;static int calls,requested_page;static bool fail,change_account;static const char *reply;
static int auto_total=-1,fail_page,account_page,shift_ids;static bool duplicate_id;
static bool invalid_slots[1001];
static void page(int,int,int,int);
int64_t bili_account_id(void){return account;}
BiliApiResult bili_fetch_json(const char *path,const char **keys,const char **vals,int n,bool sign,HttpResponse *out){
 assert(!strcmp(path,"/x/v3/fav/resource/list")&&n==4&&!sign);assert(!strcmp(keys[0],"media_id")&&!strcmp(vals[0],"7"));assert(!strcmp(keys[1],"pn"));assert(!strcmp(keys[2],"ps")&&!strcmp(vals[2],"20"));assert(!strcmp(keys[3],"platform")&&!strcmp(vals[3],"web"));calls++;requested_page=atoi(vals[1]);if(change_account)account=100;
 if(auto_total>=0){page(requested_page,auto_total,0,(requested_page-1)*20+1);if(requested_page==account_page)account=100;}
 if(fail||requested_page==fail_page)return (BiliApiResult){.outcome=BILI_API_UNKNOWN};
 out->len=strlen(reply);out->data=malloc(out->len+1);strcpy(out->data,reply);out->status=200;return (BiliApiResult){.outcome=BILI_API_SUCCESS};}
void net_response_free(HttpResponse *r){free(r->data);memset(r,0,sizeof(*r));}
static char fixture[18000];
static void page(int pn,int total,int invalid,int first){int count=total-(pn-1)*20;if(count>20)count=20;if(count<0)count=0;int len=snprintf(fixture,sizeof(fixture),"{\"code\":0,\"data\":{\"info\":{\"media_count\":%d},\"has_more\":%s,\"medias\":[",total,(pn*20<total)?"true":"false");for(int i=0;i<count;i++){int slot=(pn-1)*20+i,id=first+i+shift_ids;if(duplicate_id&&slot==1)id=1;len+=snprintf(fixture+len,sizeof(fixture)-(size_t)len,"%s",i?",":"");if((i>0&&i<=invalid)||(auto_total>=0&&slot<1001&&invalid_slots[slot]))len+=snprintf(fixture+len,sizeof(fixture)-(size_t)len,"{\"type\":%d}",i%2?12:2);else len+=snprintf(fixture+len,sizeof(fixture)-(size_t)len,"{\"type\":2,\"id\":%d,\"bvid\":\"BVtest%d\",\"title\":\"T\",\"upper\":{\"mid\":123,\"name\":\"UP\"}}",id,id);}snprintf(fixture+len,sizeof(fixture)-(size_t)len,"]}}");reply=fixture;}
static void setup_all(int total){auto_total=total;account=99;calls=0;fail_page=account_page=shift_ids=0;duplicate_id=fail=change_account=false;memset(invalid_slots,0,sizeof(invalid_slots));}
/* Catches start-from-selection regressions, premature end, and wrap mistakes. */
static void all_sequential_and_loop(void){FavoriteQueue q;BiliVideo v;setup_all(22);assert(fq_open_all(7,FQ_ORDER_SEQUENTIAL,0,&q,&v)==FQ_NEXT&&v.aid==1);for(int id=2;id<=22;id++)assert(fq_next(&q,&v)==FQ_NEXT&&v.aid==id);assert(calls==2);assert(fq_next(&q,&v)==FQ_END&&!v.aid);assert(fq_next(&q,&v)==FQ_END&&calls==2);
 setup_all(21);assert(fq_open_all(7,FQ_ORDER_LOOP,0,&q,&v)==FQ_NEXT&&v.aid==1);for(int i=1;i<44;i++)assert(fq_next(&q,&v)==FQ_NEXT&&v.aid==i%21+1);assert(calls==5);
 setup_all(0);assert(fq_open_all(7,FQ_ORDER_LOOP,0,&q,&v)==FQ_END&&!v.aid);setup_all(3);invalid_slots[0]=invalid_slots[1]=invalid_slots[2]=true;assert(fq_open_all(7,FQ_ORDER_LOOP,0,&q,&v)==FQ_END&&!v.aid&&calls==1);
 setup_all(8);for(int i=0;i<5;i++)invalid_slots[i]=true;assert(fq_open_all(7,FQ_ORDER_SEQUENTIAL,0,&q,&v)==FQ_NEXT&&v.aid==6);setup_all(8);for(int i=0;i<6;i++)invalid_slots[i]=true;assert(fq_open_all(7,FQ_ORDER_SEQUENTIAL,0,&q,&v)==FQ_NEXT&&v.aid==7);
 setup_all(25);for(int i=18;i<24;i++)invalid_slots[i]=true;assert(fq_open_all(7,FQ_ORDER_SEQUENTIAL,0,&q,&v)==FQ_NEXT);for(int id=2;id<=18;id++)assert(fq_next(&q,&v)==FQ_NEXT&&v.aid==id);assert(fq_next(&q,&v)==FQ_NEXT&&v.aid==25);
}
/* Catches repeated/random omission, seed nondeterminism, and unbounded scans. */
static void all_random(void){FavoriteQueue q;BiliVideo v;int ids[43];bool seen[44]={0};setup_all(43);assert(fq_open_all(7,FQ_ORDER_RANDOM,12345,&q,&v)==FQ_NEXT);for(int i=0;i<43;i++){if(i)assert(fq_next(&q,&v)==FQ_NEXT);assert(v.aid>=1&&v.aid<=43&&!seen[v.aid]);seen[v.aid]=true;ids[i]=(int)v.aid;}assert(fq_next(&q,&v)==FQ_END&&!v.aid);int oldcalls=calls;assert(fq_next(&q,&v)==FQ_END&&calls==oldcalls);
 setup_all(43);assert(fq_open_all(7,FQ_ORDER_RANDOM,12345,&q,&v)==FQ_NEXT);for(int i=0;i<43;i++){if(i)assert(fq_next(&q,&v)==FQ_NEXT);assert(v.aid==ids[i]);}
 setup_all(43);assert(fq_open_all(7,FQ_ORDER_RANDOM,67890,&q,&v)==FQ_NEXT);bool changed=false;for(int i=0;i<43;i++){if(i)assert(fq_next(&q,&v)==FQ_NEXT);if(v.aid!=ids[i])changed=true;}assert(changed);
 setup_all(1);assert(fq_open_all(7,FQ_ORDER_RANDOM,0,&q,&v)==FQ_NEXT&&v.aid==1);assert(fq_next(&q,&v)==FQ_END&&!v.aid);
 setup_all(1001);assert(fq_open_all(7,FQ_ORDER_RANDOM,1,&q,&v)==FQ_LIMIT&&!v.aid&&calls==1);assert(fq_next(&q,&v)==FQ_LIMIT&&calls==1);
 setup_all(1000);assert(fq_open_all(7,FQ_ORDER_RANDOM,1,&q,&v)==FQ_NEXT&&calls>=50);bool boundary_seen[1001]={0};for(int i=0;i<1000;i++){if(i)assert(fq_next(&q,&v)==FQ_NEXT);assert(v.aid>=1&&v.aid<=1000&&!boundary_seen[v.aid]);boundary_seen[v.aid]=true;}assert(fq_next(&q,&v)==FQ_END&&!v.aid);
 setup_all(4);duplicate_id=true;assert(fq_open_all(7,FQ_ORDER_RANDOM,1,&q,&v)==FQ_STOP&&!v.aid);
 setup_all(8);for(int i=1;i<=5;i++)invalid_slots[i]=true;assert(fq_open_all(7,FQ_ORDER_RANDOM,1,&q,&v)==FQ_NEXT);bool got[9]={0};for(int i=0;i<3;i++){if(i)assert(fq_next(&q,&v)==FQ_NEXT);assert((v.aid==1||v.aid==7||v.aid==8)&&!got[v.aid]);got[v.aid]=true;}assert(fq_next(&q,&v)==FQ_END);
 setup_all(8);for(int i=1;i<=6;i++)invalid_slots[i]=true;assert(fq_open_all(7,FQ_ORDER_RANDOM,1,&q,&v)==FQ_NEXT&&(v.aid==1||v.aid==8));
}
/* Account changes/errors stay latched. A completed random snapshot freezes
 * membership; later remote edits cannot substitute new entries in this run. */
static void all_fail_closed(void){FavoriteQueue q;BiliVideo v;setup_all(43);fail_page=2;assert(fq_open_all(7,FQ_ORDER_RANDOM,1,&q,&v)==FQ_ERROR&&!v.aid&&calls==2);fail_page=0;assert(fq_next(&q,&v)==FQ_ERROR&&calls==2);
 setup_all(43);account_page=2;assert(fq_open_all(7,FQ_ORDER_RANDOM,1,&q,&v)==FQ_STOP&&!v.aid&&calls==2);account=99;account_page=0;assert(fq_next(&q,&v)==FQ_STOP&&calls==2);
 setup_all(43);assert(fq_open_all(7,FQ_ORDER_RANDOM,1,&q,&v)==FQ_NEXT);shift_ids=10000;FavoriteQueueResult result=FQ_NEXT;for(int i=0;i<43&&result==FQ_NEXT;i++)result=fq_next(&q,&v);assert(result==FQ_END&&!v.aid);
 setup_all(1);assert(fq_open_all(7,FQ_ORDER_LOOP,1,&q,&v)==FQ_NEXT);auto_total=2;assert(fq_next(&q,&v)==FQ_ERROR&&!v.aid);
 setup_all(2);assert(fq_open_all(7,FQ_ORDER_RANDOM,1,&q,&v)==FQ_NEXT);account=100;assert(fq_next(&q,&v)==FQ_STOP&&!v.aid&&!q.current_aid&&!q.raw.count&&!q.snapshot_total&&!q.snapshot_aids[0]);account=99;assert(fq_next(&q,&v)==FQ_STOP);
 setup_all(43);assert(fq_open_all(7,FQ_ORDER_RANDOM,1,&q,&v)==FQ_NEXT);auto_total=44;FavoriteQueueResult changed=FQ_NEXT;for(int i=0;i<43&&changed==FQ_NEXT;i++)changed=fq_next(&q,&v);assert(changed==FQ_END&&!v.aid);
 setup_all(0);assert(fq_open_all(7,FQ_ORDER_SEQUENTIAL,0,&q,&v)==FQ_END&&!v.aid);assert(fq_open_all(7,FQ_ORDER_RANDOM,0,&q,&v)==FQ_END&&!v.aid);
 setup_all(2);account=0;assert(fq_open_all(7,FQ_ORDER_SEQUENTIAL,0,&q,&v)==FQ_STOP&&!v.aid&&!calls);account=99;
 setup_all(2);assert(fq_open_all(7,(FqOrder)99,1,&q,&v)==FQ_ERROR&&!v.aid&&calls==0);assert(fq_open_all(7,FQ_ORDER_SEQUENTIAL,1,&q,NULL)==FQ_ERROR&&calls==0);auto_total=-1;
}
static void previous_navigation(void){
 FavoriteQueue q;BiliVideo v;setup_all(23);
 assert(fq_open_all(7,FQ_ORDER_SEQUENTIAL,1,&q,&v)==FQ_NEXT&&v.aid==1&&!fq_has_previous(&q));
 assert(fq_previous(&q,&v)==FQ_END&&!v.aid&&q.current_aid==1);
 for(int id=2;id<=21;id++)assert(fq_next(&q,&v)==FQ_NEXT&&v.aid==id);
 assert(fq_has_previous(&q));assert(fq_previous(&q,&v)==FQ_NEXT&&v.aid==20&&q.page==1);
 assert(fq_next(&q,&v)==FQ_NEXT&&v.aid==21&&q.page==2);
 setup_all(23);assert(fq_open_all(7,FQ_ORDER_LOOP,1,&q,&v)==FQ_NEXT&&fq_has_previous(&q));
 assert(fq_previous(&q,&v)==FQ_NEXT&&v.aid==23&&q.page==2);
 assert(fq_next(&q,&v)==FQ_NEXT&&v.aid==1&&q.page==1);
 setup_all(1);assert(fq_open_all(7,FQ_ORDER_LOOP,1,&q,&v)==FQ_NEXT&&!fq_has_previous(&q));
 assert(fq_previous(&q,&v)==FQ_END&&q.current_aid==1);
 setup_all(43);int ids[43];assert(fq_open_all(7,FQ_ORDER_RANDOM,12345,&q,&v)==FQ_NEXT);ids[0]=(int)v.aid;
 for(int i=1;i<43;i++){assert(fq_next(&q,&v)==FQ_NEXT);ids[i]=(int)v.aid;}
 for(int i=41;i>=0;i--){assert(fq_has_previous(&q));assert(fq_previous(&q,&v)==FQ_NEXT&&v.aid==ids[i]);}
 assert(!fq_has_previous(&q));assert(fq_previous(&q,&v)==FQ_END&&!v.aid);
 assert(fq_next(&q,&v)==FQ_NEXT&&v.aid==ids[1]);
 account=100;assert(fq_previous(&q,&v)==FQ_STOP&&!v.aid&&!q.current_aid&&!q.snapshot_total);account=99;
 setup_all(8);for(int i=1;i<=5;i++)invalid_slots[i]=true;
 assert(fq_open_all(7,FQ_ORDER_SEQUENTIAL,1,&q,&v)==FQ_NEXT);assert(fq_next(&q,&v)==FQ_NEXT&&v.aid==7);
 assert(fq_previous(&q,&v)==FQ_NEXT&&v.aid==1);
 setup_all(43);assert(fq_open_all(7,FQ_ORDER_RANDOM,12345,&q,&v)==FQ_NEXT);
 for(int i=1;i<43;i++)assert(fq_next(&q,&v)==FQ_NEXT);
 while(q.random_pos>1 && q.permutation[q.random_pos-2]/20+1==q.page)assert(fq_previous(&q,&v)==FQ_NEXT);
 assert(q.random_pos>1);shift_ids=10000;assert(fq_previous(&q,&v)==FQ_NEXT&&v.aid<10000);
 setup_all(2);assert(fq_open_all(7,FQ_ORDER_SEQUENTIAL,1,&q,&v)==FQ_NEXT);assert(fq_next(&q,&v)==FQ_NEXT);
 assert(fq_previous(&q,&v)==FQ_NEXT&&v.aid==1);
 puts("PASS favorite previous: sequential page boundary/random reverse-forward/loop wrap/account/snapshot/deleted slots");
 auto_total=-1;
}
int main(void){FavoriteQueue q;BiliVideo v;FqPage raw;page(20,401,0,381);assert(fq_parse_page(reply,strlen(reply),20,&raw).outcome==BILI_API_SUCCESS&&raw.count==20&&raw.has_more);assert(raw.entries[0].video.owner_mid==123);
 assert(fq_open(7,20,400,&q)==FQ_NEXT&&q.index==19&&q.account_mid==99);page(21,401,0,401);assert(fq_next(&q,&v)==FQ_NEXT&&v.aid==401&&requested_page==21&&calls==2);assert(fq_next(&q,&v)==FQ_END&&!v.aid);assert(fq_next(&q,&v)==FQ_END&&calls==2);
 page(1,7,5,1);assert(fq_open(7,1,1,&q)==FQ_NEXT);assert(fq_next(&q,&v)==FQ_NEXT&&v.aid==7);
 page(1,8,6,1);assert(fq_open(7,1,1,&q)==FQ_NEXT);assert(fq_next(&q,&v)==FQ_NEXT&&v.aid==8);int oldcalls=calls;assert(fq_next(&q,&v)==FQ_END&&calls==oldcalls);
 page(1,2,0,1);assert(fq_open(7,1,1,&q)==FQ_NEXT);account=100;assert(fq_next(&q,&v)==FQ_STOP&&!v.aid);account=99;
 page(20,401,0,381);assert(fq_open(7,20,400,&q)==FQ_NEXT);fail=true;assert(fq_next(&q,&v)==FQ_ERROR&&q.last_result.outcome==BILI_API_UNKNOWN&&!v.aid);fail=false;oldcalls=calls;assert(fq_next(&q,&v)==FQ_ERROR&&calls==oldcalls);
 reply="{\"code\":-101}";assert(fq_open(7,1,1,&q)==FQ_ERROR&&q.last_result.login_required);
 reply="{\"code\":0,\"data\":{\"info\":{\"media_count\":2},\"has_more\":false,\"medias\":[{\"type\":2}]}}";assert(fq_parse_page(reply,strlen(reply),1,&raw).outcome==BILI_API_SUCCESS&&raw.count==1&&!raw.entries[0].usable);
 page(1,2,0,1);assert(fq_open(7,1,999,&q)==FQ_STOP);assert(fq_next(&q,&v)==FQ_STOP);
 page(20,401,0,381);assert(fq_open(7,20,400,&q)==FQ_NEXT);page(21,401,0,401);change_account=true;assert(fq_next(&q,&v)==FQ_STOP&&!v.aid);change_account=false;account=99;
 page(20,401,0,381);assert(fq_open(7,20,400,&q)==FQ_NEXT);page(21,402,0,401);assert(fq_next(&q,&v)==FQ_ERROR&&!v.aid);
 reply="{\"code\":0,\"data\":{\"info\":{\"media_count\":1},\"has_more\":false,\"medias\":[{\"type\":2,\"id\":\"1x\",\"bvid\":\"BVx\",\"title\":\"T\"}]}}";assert(fq_parse_page(reply,strlen(reply),1,&raw).outcome==BILI_API_UNKNOWN&&!raw.count);
 reply="{\"code\":0,\"data\":{\"info\":{\"media_count\":1},\"has_more\":false,\"has_more\":true,\"medias\":[{\"type\":12}]}}";assert(fq_parse_page(reply,strlen(reply),1,&raw).outcome==BILI_API_UNKNOWN&&!raw.count);
 reply="{\"code\":0,\"data\":{\"info\":{\"media_count\":1},\"has_more\":false,\"medias\":[{\"type\":2,\"attr\":9,\"id\":1,\"bvid\":\"BVx\",\"title\":\"deleted\"}]}}";assert(fq_parse_page(reply,strlen(reply),1,&raw).outcome==BILI_API_SUCCESS&&raw.count==1&&!raw.entries[0].usable);
 reply="{\"code\":0,\"data\":{\"info\":{\"media_count\":0},\"has_more\":false,\"medias\":null}}";assert(fq_parse_page(reply,strlen(reply),1,&raw).outcome==BILI_API_SUCCESS&&!raw.count&&!raw.has_more);
 reply="{\"code\":0,\"data\":{\"info\":{\"media_count\":1},\"has_more\":false,\"\\u0068as_more\":true,\"medias\":[{\"type\":12}]}}";assert(fq_parse_page(reply,strlen(reply),1,&raw).outcome==BILI_API_UNKNOWN&&!raw.count);
 all_sequential_and_loop();all_random();all_fail_closed();previous_navigation();printf("Favorite queue fixtures + play-all PASS (queue=%zu bytes, allocation-free)\n",sizeof(q));return 0;}



