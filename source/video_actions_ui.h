#ifndef VIDEO_ACTIONS_UI_H
#define VIDEO_ACTIONS_UI_H
#include "browse_ui.h"
typedef struct { BiliLikeState liked;int coins;bool favorite,favorite_known; } VideoAccountState;
void video_account_state(const BiliVideo*,BrowseRunner,VideoAccountState*);
/* kind0 like,1 coin,2 favorite; opens chosen modal directly. */
void video_account_action_ui(const BiliVideo*,int,BrowseRunner,bool(*login)(void));
void video_account_actions_ui(const BiliVideo*,BrowseRunner,bool(*login)(void));
#endif
