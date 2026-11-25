#ifndef APP_IDENTITY_H
#define APP_IDENTITY_H
/* Permanent fork namespace. Keep alongside cia/3danmu.rsf's fixed identity.
 * No migration from upstream: login/settings/cache/logs must remain separate. */
#define APP_DATA_DIR "sdmc:/3ds/3danmu-kina"
#define APP_TRACE_PATH APP_DATA_DIR "/trace.log"
#endif
