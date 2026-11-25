#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <process.h>
#define test_pid _getpid
#else
#include <unistd.h>
#define test_pid getpid
#endif

/* Keep the real file I/O, but never try creating device directories on a host.
 * Including the implementation also adapts POSIX mkdir's signature on Windows.
 * Run from the workspace root; only this synthetic fixture is overwritten. */
#define TEST_SETTINGS_PATH "build/host-tests/settings-test-preferences.txt"
#define SETTINGS_PATH TEST_SETTINGS_PATH
static int settings_test_mkdir(const char *path, int mode) {
	(void)path;
	(void)mode;
	return 0;
}
#define mkdir settings_test_mkdir
#include "../source/settings.c"
#undef mkdir

static int failures;
#define CHECK(condition, name) do { \
	if (condition) printf("PASS: %s\n", name); \
	else { printf("FAIL: %s\n", name); failures++; } \
} while (0)

static int fixture(const char *contents) {
	FILE *f = fopen(TEST_SETTINGS_PATH, "w");
	if (!f) {
		perror("test fixture open");
		return 0;
	}
	int wrote = fputs(contents, f) >= 0;
	int closed = fclose(f) == 0;
	if (!wrote || !closed) fprintf(stderr, "FAIL: test fixture write/close\n");
	return wrote && closed;
}

static void check_preserved(void) {
	/* Catch save() dropping an unrelated preference, including a prior off value. */
	CHECK(settings_get("danmaku", 1) == 0, "prior danmaku off survives preference writes");
	CHECK(settings_get("future_option", -1) == 913, "unknown integer key survives preference writes");
	CHECK(settings_get("volume", -1) == 37, "another existing preference survives preference writes");
}

int main(int argc, char **argv) {
	/* A failed override must stop before settings_init() can access a real file. */
	if (strcmp(SETTINGS_PATH, TEST_SETTINGS_PATH)) {
		fprintf(stderr, "FAIL: SETTINGS_PATH override ignored; refusing SD path access\n");
		return 1;
	}
	if (argc != 2) {
		fprintf(stderr, "usage: settings_test defaults|write|read|reset|read-reset\n");
		return 2;
	}
	printf("process=%ld mode=%s\n", (long)test_pid(), argv[1]);
	if (!strcmp(argv[1], "defaults")) {
		if (!fixture("")) return 2;
		settings_init();
		/* A new installation has neither key, so playback must receive safe defaults. */
		CHECK(settings_get("autoplay", 0) == 0, "absent autoplay defaults to off");
		CHECK(settings_get("end_mode", 0) == 0, "absent end mode defaults to pause");
		CHECK(settings_get("danmaku", 1) == 1, "absent danmaku retains existing default");
	} else if (!strcmp(argv[1], "write")) {
		if (!fixture("danmaku=0\nfuture_option=913\nvolume=37\n")) return 2;
		settings_init();
		CHECK(settings_get("autoplay", 0) == 0, "legacy file defaults autoplay to off");
		CHECK(settings_get("end_mode", 0) == 0, "legacy file defaults end mode to pause");
		check_preserved();
		settings_set("autoplay", 1);
		settings_set("end_mode", 2);
		CHECK(settings_get("autoplay", 0) == 1, "autoplay changes in writer process");
		CHECK(settings_get("end_mode", 0) == 2, "end mode changes in writer process");
	} else if (!strcmp(argv[1], "read")) {
		settings_init();
		/* Separate executable invocation: static memory cannot satisfy these checks. */
		CHECK(settings_get("autoplay", 0) == 1, "autoplay restored after process restart");
		CHECK(settings_get("end_mode", 0) == 2, "loop mode restored after process restart");
		check_preserved();
	} else if (!strcmp(argv[1], "reset")) {
		settings_init();
		CHECK(settings_get("autoplay", -1) == 1, "reset begins with persisted autoplay on");
		CHECK(settings_get("end_mode", -1) == 2, "reset begins with persisted loop mode");
		settings_set("autoplay", 0);
		settings_set("end_mode", 0);
	} else if (!strcmp(argv[1], "read-reset")) {
		settings_init();
		/* Nonzero sentinels distinguish saved zero from a missing key. */
		CHECK(settings_get("autoplay", -1) == 0, "explicit autoplay off restored after restart");
		CHECK(settings_get("end_mode", -1) == 0, "explicit pause mode restored after restart");
		check_preserved();
	} else {
		fprintf(stderr, "FAIL: unknown test mode\n");
		return 2;
	}
	printf("%s: %d failures\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
