#include <stdio.h>
#include "../source/playback_policy.h"

static int failures;
#define CHECK(cond, name) do { \
	if (!(cond)) { printf("FAIL: %s\n", name); failures++; } \
	else printf("PASS: %s\n", name); \
} while (0)

int main(void) {
	/* Catch accidental next-P dispatch on default EOF or on last-P EOF. */
	CHECK(playback_at_end(PLAYER_END_PAUSE, true) == PLAYBACK_HOLD, "default EOF retains final screen");
	CHECK(playback_at_end(PLAYER_END_CONTINUE, false) == PLAYBACK_HOLD, "last P holds at EOF");
	CHECK(playback_at_end(PLAYER_END_CONTINUE, true) == PLAYBACK_FINISH, "continue advances only with next P");
	CHECK(playback_at_end(PLAYER_END_LOOP_CURRENT, false) == PLAYBACK_REPLAY, "loop returns replay even for single P");
	CHECK(playback_end_mode(123) == PLAYER_END_PAUSE, "corrupt preference safely pauses at EOF");
	CHECK(playback_end_mode(-1) == PLAYER_END_PAUSE, "negative preference safely pauses at EOF");
	CHECK(playback_end_mode(1) == PLAYER_END_CONTINUE, "saved continue mode restored");
	CHECK(playback_end_mode(2) == PLAYER_END_LOOP_CURRENT, "saved loop mode restored");
	/* Catch backward underflow, overshooting duration, and incorrect 5s delta. */
	CHECK(playback_seek_target(3.0, 90.0, -1) == 0.0, "backward seek clamps at start");
	CHECK(playback_seek_target(40.0, 90.0, -1) == 35.0, "backward seek is five seconds");
	CHECK(playback_seek_target(40.0, 90.0, 1) == 45.0, "forward seek is five seconds");
	CHECK(playback_seek_target(88.0, 90.0, 1) == 90.0, "forward seek clamps at end");
	CHECK(playback_seek_target(40.0, 0.0, 1) == 45.0, "unknown duration permits forward seek");
	/* Read failure must not be interpreted as natural EOF or dispatch next P. */
	CHECK(playback_terminal_reason(false, false, -1, true, true, -5) == PLAYER_NETWORK_ERROR, "network failure cannot advance next P");
	CHECK(playback_terminal_reason(false, false, -1, false, false, -22) == PLAYER_DECODE_ERROR, "decode failure remains an error");
	CHECK(playback_terminal_reason(false, true, -1, true, false, 0) == PLAYER_BACK, "back at EOF is still user back");
	CHECK(playback_terminal_reason(true, false, -1, true, false, 0) == PLAYER_SYSTEM_EXIT, "system exit is not EOF");
	CHECK(playback_terminal_reason(false, false, 2, true, false, 0) == PLAYER_PAGE, "page pick is not natural advance");
	CHECK(playback_terminal_reason(false, false, -1, true, false, 0) == PLAYER_FINISHED, "clean drained EOF is natural finish");
	/* Catch decoding failures hidden as 'no frame' after an earlier good frame. */
	CHECK(playback_codec_failed(-1094995529, -11, -541478725, false), "corrupt codec packet is a real failure");
	CHECK(playback_codec_failed(-12, -11, -541478725, true), "codec allocation error is fatal during drain");
	CHECK(!playback_codec_failed(-11, -11, -541478725, false), "codec EAGAIN is not corruption");
	CHECK(!playback_codec_failed(-541478725, -11, -541478725, true), "codec EOF is expected while draining");
	CHECK(playback_codec_failed(-541478725, -11, -541478725, false), "unexpected codec EOF during packets is an error");
	CHECK(!playback_codec_failed(0, -11, -541478725, false), "successful codec frame is not an error");
	CHECK(playback_terminal_reason(false, false, -1, true, false, -1094995529) == PLAYER_DECODE_ERROR,
	      "corrupt decoder after earlier frames cannot become natural finish");
	PlayerResult r = playback_page_result(4, true);
	CHECK(r.reason == PLAYER_PAGE && r.page_index == 4 && r.paused && !r.error_code, "paused page switch keeps paused intent");
	r = playback_page_result(1, false);
	CHECK(r.reason == PLAYER_PAGE && r.page_index == 1 && !r.paused, "running page switch restores running intent");
	printf("%s: %d failures\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
