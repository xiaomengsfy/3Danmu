#ifndef PGC_UI_H
#define PGC_UI_H
#include "pgc.h"
#include "browse_ui.h"
/* Caller stops thumbnails. Runner must join cancelled workers before return.
 * Returned episode is an independent value, season storage is freed here. */
bool pgc_browse_search(const char *,BrowseRunner,PgcSeries *,PgcEpisode *);
bool pgc_browse_season(int64_t,BrowseRunner,PgcSeries *,PgcEpisode *);
bool pgc_browse_episode(int64_t,BrowseRunner,PgcSeries *,PgcEpisode *);
#endif

