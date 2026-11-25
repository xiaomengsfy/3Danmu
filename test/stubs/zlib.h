/* Host fixtures use JSON. Compression is an external codec; invalid/compressed
 * input fails closed in this test, real zlib is linked by the 3DS build. */
#ifndef TEST_ZLIB_H
#define TEST_ZLIB_H
#include <stddef.h>
typedef unsigned char Bytef;
typedef unsigned int uInt;
typedef struct { Bytef *next_in,*next_out; uInt avail_in,avail_out; unsigned long total_in,total_out; } z_stream;
#define Z_OK 0
#define Z_STREAM_END 1
#define Z_BUF_ERROR (-5)
#define Z_DATA_ERROR (-3)
#define Z_FINISH 4
static inline int inflateInit2(z_stream *s,int bits) { (void)s;(void)bits;return Z_DATA_ERROR; }
static inline int inflate(z_stream *s,int mode) { (void)s;(void)mode;return Z_DATA_ERROR; }
static inline int inflateEnd(z_stream *s) { (void)s;return Z_OK; }
#endif
