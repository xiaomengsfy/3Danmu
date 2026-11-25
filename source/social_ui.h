#ifndef SOCIAL_UI_H
#define SOCIAL_UI_H
#include "social_api.h"
#include "browse_ui.h"
/* Outside frames; caller stops thumbs; joined runner owns worker lifetime. */
SocialRelation social_relationship_ui(int64_t,const char*,BrowseRunner);
SocialSelection social_people_ui(int64_t,bool,BrowseRunner);
SocialSelection social_dynamics_ui(int64_t,BrowseRunner);
SocialSelection social_messages_ui(BrowseRunner);
#endif
