#ifndef PLAYBACK_POLICY_H
#define PLAYBACK_POLICY_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	PLAYER_END_PAUSE = 0,
	PLAYER_END_CONTINUE = 1,
	PLAYER_END_LOOP_CURRENT = 2
} PlayerEndMode;

typedef enum {
	PLAYER_BACK = 0,
	PLAYER_FINISHED,
	PLAYER_PAGE,
	PLAYER_REPLAY,
	PLAYER_NETWORK_ERROR,
	PLAYER_DECODE_ERROR,
	PLAYER_SYSTEM_EXIT,
	PLAYER_QUALITY,
	PLAYER_PREVIOUS,
	PLAYER_NEXT
} PlayerReason;

typedef enum { PLAYER_NAV_PAGES, PLAYER_NAV_EPISODES, PLAYER_NAV_ITEMS } PlayerNavigationKind;
typedef struct { PlayerNavigationKind kind; bool previous, next; } PlayerNavigation;

typedef struct { bool paused; bool has_next; double start_seconds; } PlayerStartOptions;
typedef struct {
	PlayerReason reason;
	int page_index;       /* Zero based, -1 except PLAYER_PAGE. */
	bool paused;         /* User intent, before the page picker/modal paused. */
	int error_code;      /* Native negative error where available, otherwise 0. */
	double resume_seconds; /* Content position, only for PLAYER_QUALITY. */
} PlayerResult;

typedef enum { PLAYBACK_HOLD, PLAYBACK_FINISH, PLAYBACK_REPLAY } PlaybackEndAction;

PlayerEndMode playback_end_mode(int stored);
PlaybackEndAction playback_at_end(PlayerEndMode mode, bool has_next);
double playback_seek_target(double position, double duration, int direction);
PlayerReason playback_terminal_reason(bool system_exit, bool user_back,
	int page_index, bool natural_eof, bool network_error, int error_code);
PlayerResult playback_page_result(int page_index, bool prior_paused);
PlayerResult playback_neighbor_result(int direction, bool prior_paused);
double playback_resume_seconds(double seconds);
PlayerResult playback_quality_result(double seconds, bool prior_paused);
/* Native codec codes supplied by the caller keep this policy host portable. */
bool playback_codec_failed(int status, int again_code, int eof_code, bool draining);

/* Input state is discarded whenever a child screen/modal owns the keys. */
typedef struct {
	uint64_t a_since, seek_next;
	bool a_pending, a_long;
	int seek_direction;
} PlaybackInput;
typedef struct { bool pause, temporary_double; int seek; } PlaybackInputAction;
PlaybackInputAction playback_input(PlaybackInput *state, uint64_t now,
	bool active, bool a_down, bool a_held, bool a_up,
	int seek_down, int seek_held, bool opening_modal);
typedef enum { PLAYBACK_MODAL_NONE, PLAYBACK_MODAL_ACTIONS, PLAYBACK_MODAL_UP } PlaybackModal;
PlaybackModal playback_modal_route(bool active, bool x_pressed, bool up_pressed);
int playback_speed(int stored);
int playback_next_speed(int stored);
typedef struct { double seconds; uint64_t wall_ms; int rate; bool frozen; } PlaybackClock;
void playback_clock_reset(PlaybackClock *clock, uint64_t now, double seconds,
	int rate, bool frozen);
double playback_clock_update(PlaybackClock *clock, uint64_t now, int rate, bool frozen);

#endif
