#include "playback_policy.h"
#include <math.h>

PlayerEndMode playback_end_mode(int stored) {
	return stored >= PLAYER_END_PAUSE && stored <= PLAYER_END_LOOP_CURRENT
		? (PlayerEndMode)stored : PLAYER_END_PAUSE;
}
PlaybackEndAction playback_at_end(PlayerEndMode mode, bool has_next) {
	if (mode == PLAYER_END_LOOP_CURRENT) return PLAYBACK_REPLAY;
	if (mode == PLAYER_END_CONTINUE && has_next) return PLAYBACK_FINISH;
	return PLAYBACK_HOLD;
}
double playback_seek_target(double position, double duration, int direction) {
	double target = position + (direction < 0 ? -5.0 : 5.0);
	if (target < 0.0) target = 0.0;
	if (duration > 0.0 && target > duration) target = duration;
	return target;
}
PlayerReason playback_terminal_reason(bool system_exit, bool user_back,
	int page_index, bool natural_eof, bool network_error, int error_code) {
	if (system_exit) return PLAYER_SYSTEM_EXIT;
	if (user_back) return PLAYER_BACK;
	if (page_index >= 0) return PLAYER_PAGE;
	if (network_error) return PLAYER_NETWORK_ERROR;
	if (error_code < 0 || !natural_eof) return PLAYER_DECODE_ERROR;
	return PLAYER_FINISHED;
}
PlayerResult playback_page_result(int page_index, bool prior_paused) {
	PlayerResult r = { .reason=PLAYER_PAGE, .page_index=page_index, .paused=prior_paused }; return r;
}
PlayerResult playback_neighbor_result(int direction, bool prior_paused) {
	return (PlayerResult){ .reason=direction<0?PLAYER_PREVIOUS:PLAYER_NEXT,
		.page_index=-1, .paused=prior_paused };
}
double playback_resume_seconds(double seconds) {
	return isfinite(seconds) && seconds > 0.0 ? seconds : 0.0;
}
PlayerResult playback_quality_result(double seconds, bool prior_paused) {
	return (PlayerResult){ .reason=PLAYER_QUALITY, .page_index=-1,
		.paused=prior_paused, .resume_seconds=playback_resume_seconds(seconds) };
}
bool playback_codec_failed(int status, int again_code, int eof_code, bool draining) {
	return status < 0 && status != again_code && !(draining && status == eof_code);
}

PlaybackInputAction playback_input(PlaybackInput *s, uint64_t now,
	bool active, bool down, bool held, bool up, int sd, int sh, bool modal) {
	PlaybackInputAction a = {0};
	if (!active || modal) { *s = (PlaybackInput){0}; return a; }
	if (down) { s->a_since = now; s->a_pending = true; s->a_long = false; }
	if (s->a_pending && held && now - s->a_since >= 350) s->a_long = true;
	if (s->a_pending && up) {
		a.pause = !s->a_long && now - s->a_since < 350;
		s->a_pending = s->a_long = false;
	}
	if (!held && !down) s->a_pending = false;
	a.temporary_double = s->a_pending && held && s->a_long;
	if (sd) {
		s->seek_direction = sd; s->seek_next = now + 350; a.seek = sd;
	} else if (!sh || sh != s->seek_direction) s->seek_direction = 0;
	else if (now >= s->seek_next) { a.seek = sh; s->seek_next = now + 200; }
	return a;
}
PlaybackModal playback_modal_route(bool active, bool x, bool up) {
	return !active ? PLAYBACK_MODAL_NONE : x ? PLAYBACK_MODAL_ACTIONS :
		up ? PLAYBACK_MODAL_UP : PLAYBACK_MODAL_NONE;
}
int playback_speed(int stored) {
	return stored == 125 || stored == 150 || stored == 200 ? stored : 100;
}
int playback_next_speed(int stored) {
	int rate = playback_speed(stored);
	return rate == 100 ? 125 : rate == 125 ? 150 : rate == 150 ? 200 : 100;
}
void playback_clock_reset(PlaybackClock *c, uint64_t now, double seconds, int rate, bool frozen) {
	*c = (PlaybackClock){ seconds, now, playback_speed(rate), frozen };
}
double playback_clock_update(PlaybackClock *c, uint64_t now, int rate, bool frozen) {
	if (!c->frozen && now >= c->wall_ms)
		c->seconds += (double)(now - c->wall_ms) * c->rate / 100000.0;
	c->wall_ms = now; c->rate = playback_speed(rate); c->frozen = frozen;
	return c->seconds;
}
