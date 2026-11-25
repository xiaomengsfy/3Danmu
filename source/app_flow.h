#ifndef APP_FLOW_H
#define APP_FLOW_H
#include <stdbool.h>
#include <stdint.h>
#include "playback_policy.h"

typedef struct { bool started, paused; double resume_seconds; } AppPlaybackSession;
void app_session_init(AppPlaybackSession *session, bool autoplay);
void app_session_begin(AppPlaybackSession *session);
/* PAGE may select before first Play, but must not start the session. */
bool app_session_next(AppPlaybackSession *session, PlayerResult result,
                      int *page, int page_count);

typedef struct {
    uint32_t generation;
    int64_t folder_id;
    int mode, page;
} AppRequestKey;
bool app_request_matches(AppRequestKey request, AppRequestKey current);
bool app_list_at_end(bool server_paged, bool has_more, int usable_count);
#endif
