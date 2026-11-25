#ifndef PGC_H
#define PGC_H
#include "bili.h"
typedef struct { int64_t season_id; char title[200],cover[192]; } PgcSeries;
typedef struct { int64_t season_id,ep_id,aid,cid,duration_ms; bool extra; char bvid[16],title[200],group[96]; } PgcEpisode;
typedef struct { PgcSeries series; PgcEpisode *episodes; int count; bool truncated; } PgcSeason;
typedef enum { PGC_PLAY_INVALID,PGC_PLAY_FULL,PGC_PLAY_PREVIEW,PGC_PLAY_DRM,PGC_PLAY_MULTISEG,PGC_PLAY_DASH,PGC_PLAY_UNSUPPORTED } PgcPlayStatus;
typedef struct { PgcPlayStatus status; char url[2048]; int64_t duration_ms; int quality; } PgcPlayback;
BiliApiResult pgc_parse_search(const char *,size_t,int,PgcSeries *,int,BiliPageInfo *);
BiliApiResult pgc_parse_season(const char *,size_t,PgcSeason *);
BiliApiResult pgc_parse_sections(const char *,size_t,PgcSeason *);
BiliApiResult pgc_parse_playurl(const char *,size_t,const PgcEpisode *,PgcPlayback *);
BiliApiResult pgc_search(const char *,int,PgcSeries *,int,BiliPageInfo *);
BiliApiResult pgc_season(int64_t,PgcSeason *);
BiliApiResult pgc_episode_season(int64_t,PgcSeason *);
BiliApiResult pgc_playurl(const PgcEpisode *,PgcPlayback *);
BiliApiResult pgc_playurl_quality(const PgcEpisode *,int qn,PgcPlayback *);
void pgc_season_free(PgcSeason *);
const char *pgc_play_message(PgcPlayStatus);
#endif


