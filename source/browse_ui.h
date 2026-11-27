#ifndef BROWSE_UI_H
#define BROWSE_UI_H
#include "bili.h"
typedef int (*BrowseRunner)(int (*fn)(void *),void *arg,const char *message,bool cancellable);
typedef enum { BROWSE_BACK, BROWSE_HISTORY, BROWSE_FAVORITES, BROWSE_WATCHLATER,
    BROWSE_SUBMISSIONS, BROWSE_MESSAGES, BROWSE_FOLLOWING, BROWSE_FANS,
    BROWSE_DYNAMICS, BROWSE_FEED, BROWSE_RELATION } BrowseChoice;
/* Caller stops thumbnails before entering and restores them on return.
 * Runner waits for worker termination and returns failure on read cancellation. */
/* Four folders per page; selected metadata on top, -1 on cancel. */
int browse_folders(const BiliFavFolder *,int count,bool truncated);
int browse_search_kind(void); /* -1 cancelled, 0 video, 1 UP, 2 bangumi */
bool browse_users(const char *keyword,BrowseRunner run,BiliProfile *selected);
BrowseChoice browse_profile(const BiliProfile *profile,bool own,const char *message);
BrowseChoice browse_profile_relationship(const BiliProfile *,bool,const char *,const char *);
/* Membership-based add/remove; true means a write was attempted (reload list). */
bool browse_watchlater_add(const BiliVideo *video,BrowseRunner run,bool (*login)(void));
#endif
