#ifndef SCREENSHOT_H
#define SCREENSHOT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Pure conversion from rotated 3DS BGR8 to bottom-up BMP. */
bool screenshot_bmp(const uint8_t *fb, unsigned width, unsigned height,
                    uint8_t *out, size_t capacity);
#ifdef __3DS__
/* Album-only async owned snapshot. Main-thread status/result polling reaps
 * the joined writer; success reported only after JPEG write+close success.
 * One128KiB worker; thread failure frees snapshot without main-thread encoding.
 * Never overwrites/deletes existing files or touches the Camera index. */
const char *screenshot_status(void);
int screenshot_take_result(void); /* +1 saved, -1 failure, 0 no new result */
void screenshot_prepare(void);
void screenshot_finish(void);
void screenshot_shutdown(void);
#endif
#endif
