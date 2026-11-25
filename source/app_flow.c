#include "app_flow.h"
#include <math.h>
void app_session_init(AppPlaybackSession *s, bool autoplay) {
    s->started=autoplay;
    s->paused=false;
    s->resume_seconds=0;
}
void app_session_begin(AppPlaybackSession *s) {
    s->started=true;
    s->paused=false;
    s->resume_seconds=0;
}
bool app_session_next(AppPlaybackSession *s, PlayerResult r, int *page, int count) {
    if (!s || !page || *page<0 || *page>=count) return false;
    if (r.reason==PLAYER_QUALITY) {
        if (!s->started) return false;
        s->paused=r.paused;
        s->resume_seconds=isfinite(r.resume_seconds)&&r.resume_seconds>0?r.resume_seconds:0;
        return true;
    }
    s->resume_seconds=0;
    if (r.reason==PLAYER_PREVIOUS || r.reason==PLAYER_NEXT) {
        int next=*page+(r.reason==PLAYER_PREVIOUS?-1:1);
        if(next<0 || next>=count)return false;
        *page=next;
        if(s->started)s->paused=r.paused;
        return true;
    }
    if (r.reason==PLAYER_PAGE) {
        if (r.page_index<0 || r.page_index>=count) return false;
        *page=r.page_index;
        if (s->started) s->paused=r.paused;
        return true;
    }
    if (!s->started) return false;
    if (r.reason==PLAYER_REPLAY) {
        s->paused=false;
        return true;
    }
    if (r.reason==PLAYER_FINISHED && *page+1<count) {
        ++*page;
        s->paused=false;
        return true;
    }
    return false;
}
bool app_request_matches(AppRequestKey a, AppRequestKey b) {
    return a.generation==b.generation && a.folder_id==b.folder_id &&
           a.mode==b.mode && a.page==b.page;
}
bool app_list_at_end(bool server_paged, bool has_more, int usable_count) {
    return server_paged ? !has_more : usable_count==0;
}
