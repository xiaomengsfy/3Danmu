#define bili_fetch_json legacy_fetch
#define main legacy_fixture_main
#include "../favorite_queue_test.c"
#undef main
#undef bili_fetch_json
static int scenario,variant;
BiliApiResult bili_fetch_json(const char *path,const char **keys,const char **vals,int n,bool sign,HttpResponse *out){
 if(!scenario)return legacy_fetch(path,keys,vals,n,sign,out);
 int pn=atoi(vals[1]);calls++;assert(pn>=1&&pn<=3);
 int count=pn==1?2:pn==2?0:1;
 if(variant==1&&pn==1)count=1;
 bool more=pn<3;if(variant==2&&pn==1)more=false;
 int used=snprintf(fixture,sizeof fixture,"{\"code\":0,\"data\":{\"info\":{\"media_count\":60},\"has_more\":%s,\"medias\":[",more?"true":"false");
 for(int i=0;i<count;i++){int id=(pn-1)*20+i+1;used+=snprintf(fixture+used,sizeof(fixture)-(size_t)used,"%s{\"type\":2,\"id\":%d,\"bvid\":\"BVtest%d\",\"title\":\"T\"}",i?",":"",id,id);}
 used+=snprintf(fixture+used,sizeof(fixture)-(size_t)used,"]}}");
 out->data=malloc((size_t)used+1);memcpy(out->data,fixture,(size_t)used+1);out->len=(size_t)used;return (BiliApiResult){.outcome=BILI_API_SUCCESS};
}
static void sparse_traversal(void){
 FavoriteQueue q;BiliVideo v;setup_all(60);scenario=1;
 assert(fq_open_all(7,FQ_ORDER_SEQUENTIAL,1,&q,&v)==FQ_NEXT&&v.aid==1);
 assert(fq_next(&q,&v)==FQ_NEXT&&v.aid==2);
 assert(fq_next(&q,&v)==FQ_NEXT&&v.aid==41);
 assert(fq_previous(&q,&v)==FQ_NEXT&&v.aid==2);
 assert(fq_next(&q,&v)==FQ_NEXT&&v.aid==41);
 assert(fq_next(&q,&v)==FQ_END);
 assert(fq_open_all(7,FQ_ORDER_LOOP,1,&q,&v)==FQ_NEXT&&v.aid==1);
 assert(fq_previous(&q,&v)==FQ_NEXT&&v.aid==41);
 assert(fq_next(&q,&v)==FQ_NEXT&&v.aid==1);
 assert(fq_open_all(7,FQ_ORDER_RANDOM,1,&q,&v)==FQ_NEXT);bool got[42]={0};
 for(int i=0;i<3;i++){if(i)assert(fq_next(&q,&v)==FQ_NEXT);assert((v.aid==1||v.aid==2||v.aid==41)&&!got[v.aid]);got[v.aid]=true;}
 assert(fq_next(&q,&v)==FQ_END);
 for(int change=1;change<=2;change++){
  variant=0;assert(fq_open_all(7,FQ_ORDER_RANDOM,1,&q,&v)==FQ_NEXT);
  variant=change;q.page=0; /* force a refetch of the next shuffled destination */
  FavoriteQueueResult result=FQ_NEXT;
  for(int i=0;i<3&&result==FQ_NEXT;i++)result=fq_next(&q,&v);
  assert(result==FQ_END&&!v.aid);
 }
 scenario=variant=0;puts("PASS sparse/empty middle pages sequential/backward/loop/random and frozen random membership unaffected by later count/pagination");
}
static void nullable(void){FqPage raw;
 reply="{\"code\":0,\"data\":{\"info\":{\"media_count\":2},\"has_more\":false,\"medias\":[{\"type\":2,\"id\":1,\"bvid\":null,\"title\":null},{\"type\":2,\"id\":2,\"bvid\":\"BVx\",\"title\":\"T\",\"cover\":null,\"duration\":null,\"page\":null,\"upper\":null,\"cnt_info\":null}]}}";
 assert(fq_parse_page(reply,strlen(reply),1,&raw).outcome==BILI_API_SUCCESS);
 assert(raw.count==2&&!raw.entries[0].usable&&raw.entries[1].usable&&raw.entries[1].video.aid==2);
 puts("PASS nullable unavailable row and optional metadata");
}
static void deleted_blocks(void){FavoriteQueue q;BiliVideo v;
 setup_all(43);for(int i=5;i<42;i++)invalid_slots[i]=true;
 assert(fq_open_all(7,FQ_ORDER_SEQUENTIAL,1,&q,&v)==FQ_NEXT);
 for(int id=2;id<=5;id++)assert(fq_next(&q,&v)==FQ_NEXT&&v.aid==id);
 assert(fq_next(&q,&v)==FQ_NEXT&&v.aid==43);
 assert(fq_previous(&q,&v)==FQ_NEXT&&v.aid==5);
 setup_all(43);for(int i=5;i<42;i++)invalid_slots[i]=true;
 assert(fq_open_all(7,FQ_ORDER_RANDOM,1,&q,&v)==FQ_NEXT);bool seen[44]={0};
 for(int i=0;i<6;i++){if(i)assert(fq_next(&q,&v)==FQ_NEXT);assert((v.aid<=5||v.aid==43)&&!seen[v.aid]);seen[v.aid]=true;}
 assert(fq_next(&q,&v)==FQ_END);
 setup_all(1000);for(int i=0;i<1000;i++)invalid_slots[i]=true;
 assert(fq_open_all(7,FQ_ORDER_RANDOM,1,&q,&v)==FQ_END&&calls==50);
 setup_all(43);for(int i=0;i<43;i++)invalid_slots[i]=true;
 assert(fq_open_all(7,FQ_ORDER_LOOP,1,&q,&v)==FQ_END&&calls<=4);
 puts("PASS long deleted blocks sequential/random/backward and finite all-deleted loop");
}
static void previous_end(void){FavoriteQueue q;BiliVideo v;
 setup_all(21);for(int i=0;i<20;i++)invalid_slots[i]=true;
 assert(fq_open(7,2,21,&q)==FQ_NEXT);
 assert(fq_previous(&q,&v)==FQ_END&&!v.aid&&q.page==2&&q.index==0&&q.current_aid==21);
 assert(fq_next(&q,&v)==FQ_END);
 puts("PASS unavailable predecessors preserve live current cursor");
}
static void sparse(void){FqPage p;
 reply="{\"code\":0,\"data\":{\"info\":{\"media_count\":20},\"has_more\":false,\"medias\":[{\"type\":2,\"id\":1,\"bvid\":\"BVx\",\"title\":\"T\"}]}}";
 assert(fq_parse_page(reply,strlen(reply),1,&p).outcome==BILI_API_SUCCESS&&p.count==1&&!p.has_more);
 FavoriteQueue q;BiliVideo v;auto_total=-1;account=99;
 assert(fq_open_all(7,FQ_ORDER_RANDOM,1,&q,&v)==FQ_NEXT&&v.aid==1);
 assert(fq_next(&q,&v)==FQ_END);
 puts("PASS sparse final page keeps server raw slots");
}
int main(int argc,char**argv){assert(argc==2);if(!strcmp(argv[1],"nullable"))nullable();else if(!strcmp(argv[1],"deleted"))deleted_blocks();else if(!strcmp(argv[1],"traversal"))sparse_traversal();else if(!strcmp(argv[1],"previous-end"))previous_end();else sparse();return 0;}
