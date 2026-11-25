#ifndef FAVORITE_QUEUE_H
#define FAVORITE_QUEUE_H
#include "bili.h"
typedef enum { FQ_NEXT,FQ_END,FQ_STOP,FQ_ERROR,FQ_LIMIT } FavoriteQueueResult;
typedef enum { FQ_ISSUE_NONE,FQ_ISSUE_REQUEST,FQ_ISSUE_FORMAT,FQ_ISSUE_CHANGED,FQ_ISSUE_ACCOUNT } FqIssue;

typedef enum { FQ_ORDER_SEQUENTIAL,FQ_ORDER_RANDOM,FQ_ORDER_LOOP } FqOrder;
#define FQ_RANDOM_MAX 1000
typedef struct { bool usable; BiliVideo video; } FqEntry;
typedef struct { FqEntry entries[20]; int count; int64_t total; bool has_more; } FqPage;
typedef struct {
 int64_t folder_id,account_mid,current_aid;
 int page,index,last_page;
 FqIssue issue;
 FqPage raw;
 BiliApiResult last_result;
 bool stopped;
 FavoriteQueueResult terminal;
 FqOrder order;
 /* Raw-slot snapshot, not full video metadata. No owned pointers: callers may
  * discard/copy this value only after the worker using it has joined. */
 int64_t snapshot_aids[FQ_RANDOM_MAX];
 char snapshot_bvids[FQ_RANDOM_MAX][16];
 uint16_t permutation[FQ_RANDOM_MAX];
 uint8_t snapshot_counts[FQ_RANDOM_MAX/20];
 int snapshot_total,snapshot_pages,random_count,random_pos;
} FavoriteQueue;
/* Pure parser retains every raw server slot, including nonvideos/deletions. */
BiliApiResult fq_parse_page(const char *,size_t,int,FqPage *);
/* FQ_NEXT means selected item was located and queue is ready AFTER it.
 * Requires a positive runtime account; copies no credential storage. */
FavoriteQueueResult fq_open(int64_t,int,int64_t,FavoriteQueue *);
/* Starts at folder page 1 and returns the first playable video. FQ_NEXT leaves
 * the queue ready AFTER first. Sequential/random end after one pass; LOOP wraps
 * the whole folder. Random snapshots at most 1000 RAW slots, else FQ_LIMIT;
 * deleted/non-video slots count toward that cap. Seed 0 is valid. Metadata only.
 * Random freezes membership for this run; next/previous use AV+BV seeds locally.
 * Callers must resolve/validate live video metadata before opening a stream.
 * Requires joined-worker access and a positive runtime account, like fq_open. */
FavoriteQueueResult fq_open_all(int64_t,FqOrder,uint32_t,FavoriteQueue *,BiliVideo *);
/* Joined worker context only. No retries. END/STOP/ERROR latch until fq_open.
 * Unusable raw slots are skipped. Scans are bounded by server total/pages;
 * an entirely unavailable folder ends, including in loop mode. */
FavoriteQueueResult fq_next(FavoriteQueue *,BiliVideo *);
/* Previous follows folder order, or the preceding entry in the same random
 * permutation. First-item END does not destroy a live current cursor.
 * Caller owns a current cursor, not a cursor already advanced by prefetch. */
bool fq_has_previous(const FavoriteQueue *);
FavoriteQueueResult fq_previous(FavoriteQueue *,BiliVideo *);
#endif
