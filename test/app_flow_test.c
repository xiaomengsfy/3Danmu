#include <stdio.h>
#include "app_flow.h"
static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL line %d: %s\n",__LINE__,#x); } } while(0)
int main(void) {
    AppPlaybackSession s;
    app_session_init(&s,false);
    CHECK(!s.started && !s.paused);
    int page=0;
    CHECK(app_session_next(&s,(PlayerResult){PLAYER_PAGE,2,true,0,0},&page,4));
    CHECK(page==2 && !s.started && !s.paused);
    CHECK(app_session_next(&s,(PlayerResult){PLAYER_PAGE,1,false,0,0},&page,4));
    CHECK(page==1 && !s.started);
    CHECK(!app_session_next(&s,(PlayerResult){PLAYER_REPLAY,-1,false,0,0},&page,4));
    app_session_begin(&s);
    CHECK(s.started && !s.paused);
    CHECK(app_session_next(&s,(PlayerResult){PLAYER_PAGE,3,true,0,0},&page,4));
    CHECK(page==3 && s.paused);
    CHECK(app_session_next(&s,(PlayerResult){PLAYER_REPLAY,-1,true,0,0},&page,4));
    CHECK(page==3 && !s.paused);
    CHECK(!app_session_next(&s,(PlayerResult){PLAYER_FINISHED,-1,false,0,0},&page,4));
    page=1;
    CHECK(app_session_next(&s,(PlayerResult){PLAYER_FINISHED,-1,false,0,0},&page,4));
    CHECK(page==2 && !s.paused);
    CHECK(!app_session_next(&s,(PlayerResult){PLAYER_PAGE,4,false,0,0},&page,4));
    CHECK(page==2);
    PlayerReason stops[]={PLAYER_BACK,PLAYER_NETWORK_ERROR,PLAYER_DECODE_ERROR,PLAYER_SYSTEM_EXIT};
    for(unsigned i=0;i<sizeof(stops)/sizeof(stops[0]);i++) {
        CHECK(!app_session_next(&s,(PlayerResult){stops[i],-1,false,-1,0},&page,4));
        CHECK(page==2);
    }
    app_session_init(&s,true);
    CHECK(s.started && !s.paused);
    AppRequestKey a={7,123,4,2}, b=a;
    CHECK(app_request_matches(a,b));
    b.generation++; CHECK(!app_request_matches(a,b));
    b=a; b.folder_id++; CHECK(!app_request_matches(a,b));
    b=a; b.mode++; CHECK(!app_request_matches(a,b));
    b=a; b.page++; CHECK(!app_request_matches(a,b));
    CHECK(!app_list_at_end(true,true,0)); /* Filtered-empty page still has a next page. */
    CHECK(app_list_at_end(true,false,20));
    CHECK(app_list_at_end(true,false,0));
    CHECK(app_list_at_end(false,false,0));
    CHECK(!app_list_at_end(false,false,20));
    printf("app flow: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
