#ifndef SOCIAL_API_H
#define SOCIAL_API_H
#include "bili.h"
#define SOCIAL_BODY 8192
#define SOCIAL_CURSOR 128
typedef struct { bool known,following,mutual; int attribute; } SocialRelation;
typedef enum { SOCIAL_NONE, SOCIAL_UP, SOCIAL_VIDEO, SOCIAL_PGC } SocialKind;
typedef struct { SocialKind kind; BiliProfile profile; BiliVideo video; int64_t ep_id; } SocialSelection;
typedef struct { char url[384]; int width,height; } SocialImage;
typedef struct { char id[32],type[40],body[SOCIAL_BODY]; BiliProfile author; SocialSelection target; SocialImage images[9]; int image_count; bool truncated; } SocialDynamic;
typedef struct { BiliPageInfo page; char next[SOCIAL_CURSOR]; } SocialPage;
typedef enum { SOCIAL_REPLIES, SOCIAL_AT, SOCIAL_LIKES, SOCIAL_SYSTEM, SOCIAL_UNREAD } SocialMessageKind;
typedef struct { char title[128],body[4096]; int64_t id,time; SocialSelection target; } SocialMessage;
BiliApiResult social_parse_relation(int,int,const char*,size_t,SocialRelation*);
BiliApiResult social_parse_people(int,int,const char*,size_t,int,BiliProfile*,int,BiliPageInfo*);
BiliApiResult social_parse_dynamics(int,int,const char*,size_t,SocialDynamic*,int,SocialPage*);
BiliApiResult social_parse_messages(int,int,const char*,size_t,SocialMessageKind,SocialMessage*,int,SocialPage*);
BiliApiResult social_relation(int64_t,SocialRelation*);
/* Once only; caller must explicitly confirm unfollow and never replay UNKNOWN. */
BiliApiResult social_relation_set(int64_t,bool);
BiliApiResult social_people(int64_t,bool,int,BiliProfile*,int,BiliPageInfo*);
/* mid=0 following feed; positive mid own/UP feed. Cursor is opaque text. */
BiliApiResult social_dynamics(int64_t,const char*,bool,SocialDynamic*,int,SocialPage*);
/* NULL when schema verified. Non-NULL means intentionally unsupported:
 * no service request is sent and caller should display the concrete note. */
const char *social_message_support_note(SocialMessageKind);
BiliApiResult social_messages(SocialMessageKind,const char*,SocialMessage*,int,SocialPage*);
#endif
