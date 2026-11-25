#include "app_flow.h"
#include <assert.h>
#include <stdio.h>
int main(void){AppPlaybackSession s;app_session_init(&s,true);int page=1;
 assert(app_session_next(&s,playback_neighbor_result(-1,true),&page,3)&&page==0&&s.paused&&!s.resume_seconds);
 assert(!app_session_next(&s,playback_neighbor_result(-1,false),&page,3)&&page==0);
 assert(app_session_next(&s,playback_neighbor_result(1,false),&page,3)&&page==1&&!s.paused);
 s.resume_seconds=72.25;assert(app_session_next(&s,playback_neighbor_result(1,true),&page,3)&&page==2&&s.paused&&!s.resume_seconds);
 assert(!app_session_next(&s,playback_neighbor_result(1,false),&page,3)&&page==2);
 app_session_init(&s,false);assert(app_session_next(&s,playback_neighbor_result(-1,false),&page,3)&&page==1&&!s.started);
 puts("PASS manual neighbors boundaries/pause/reset/no-autoplay gate");return 0;}
