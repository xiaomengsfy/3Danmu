#ifndef VIDEO_INFO_H
#define VIDEO_INFO_H
#include "bili.h"
#define VIDEO_INFO_EPISODE_LIMIT 500
typedef struct {
 BiliVideo video;
 int64_t collection_id;
 char collection_title[200];
 BiliVideo *episodes; /* ordered explicit aid+cid; never auto-expand P */
 int count;
 int64_t expected_count; /* ugc_season.ep_count, -1 if absent */
 int raw_count, duplicate_count, skipped_count;
 bool partial, truncated;
} VideoInfo;
/* Initialize once before parse/get; free before reuse or call parse/get which
 * replaces the initialized output. All failure outputs are cleared. */
void video_info_init(VideoInfo *out);
void video_info_free(VideoInfo *out);
BiliApiResult video_info_parse(int transport,int status,const char *body,size_t len,VideoInfo *out);
/* Synchronous GET; caller owns worker/account lifetime. bvid required. */
BiliApiResult video_info_get(const BiliVideo *seed,VideoInfo *out);
#endif
