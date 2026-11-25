#ifndef VIDEO_ACTIONS_API_H
#define VIDEO_ACTIONS_API_H
#include "bili.h"
#define VIDEO_FOLDER_MAX 100
BiliApiResult video_parse_coins(int,int,const char*,size_t,int*);
BiliApiResult video_parse_folders(int,int,const char*,size_t,BiliFavFolder*,int,BiliPageInfo*);
BiliApiResult video_coins(int64_t,int*);
BiliApiResult video_coin_once(int64_t,int,bool);
BiliApiResult video_folders(int64_t,BiliFavFolder*,int,BiliPageInfo*);
BiliApiResult video_favorite_delta_once(int64_t,const BiliFavFolder*,const bool*,int);
#endif
