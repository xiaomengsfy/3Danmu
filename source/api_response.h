#ifndef API_RESPONSE_H
#define API_RESPONSE_H
#include "bili.h"

/* Pure response parsers shared by production and host fixtures. Transport
 * failures remain UNKNOWN even if a partial body contains code=0. */
BiliApiResult api_response_result(int transport, int status, const char *body, size_t len);
BiliApiResult api_response_history(int t,int s,const char *body,size_t len,
    BiliVideo *out,int max,int *count);
BiliApiResult api_response_folders(int transport, int status, const char *body, size_t len,
                                   BiliFavFolder *out, int max, BiliPageInfo *info);
BiliApiResult api_response_favorites(int transport, int status, const char *body, size_t len,
                                     int page, BiliVideo *out, int max, BiliPageInfo *info);
BiliApiResult api_response_like(int transport, int status, const char *body, size_t len,
                                BiliLikeState *state);
BiliApiResult api_response_profile(int t,int s,const char *b,size_t n,bool own,BiliProfile *out);
BiliApiResult api_response_watchlater(int t,int s,const char *b,size_t n,int page,
    BiliVideo *out,int max,BiliPageInfo *info,int64_t aid,bool *found);
BiliApiResult api_response_users(int t,int s,const char *b,size_t n,int page,
    BiliProfile *out,int max,BiliPageInfo *info);
BiliApiResult api_response_user_videos(int t,int s,const char *b,size_t n,int page,
    BiliVideo *out,int max,BiliPageInfo *info);
#endif
