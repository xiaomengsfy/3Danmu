#define main old_main
#include "../favorite_queue_test.c"
#undef main
int main(void){FavoriteQueue q;BiliVideo v;setup_all(995);
 assert(fq_open_all(7,FQ_ORDER_RANDOM,12345,&q,&v)==FQ_NEXT);
 int scanned=calls;assert(scanned==50);
 int64_t ids[995];ids[0]=v.aid;
 for(int i=1;i<995;i++){assert(fq_next(&q,&v)==FQ_NEXT);ids[i]=v.aid;assert(v.bvid[0]=='B');}
 assert(calls==scanned);assert(fq_previous(&q,&v)==FQ_NEXT&&v.aid==ids[993]);assert(calls==scanned);
 account=100;assert(fq_next(&q,&v)==FQ_STOP&&!v.aid&&!q.snapshot_total);
 puts("PASS 995 random items plus previous: 50 initial list reads, zero post-snapshot list reads, account guard");return 0;}
