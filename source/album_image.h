#ifndef ALBUM_IMAGE_H
#define ALBUM_IMAGE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ALBUM_WIDTH 640
#define ALBUM_HEIGHT 480
/* Pure BMP→top-down BGR canvas, 640x384 centered in640x480 black. */
bool album_image_canvas(const uint8_t *bmp,size_t length,uint8_t *bgr,size_t capacity);
/* Standard JPEG APP1/EXIF injection; thumbnail JPEG optional, <=60KiB.
 * date_time is20-byte YYYY:MM:DD HH:MM:SS (or NULL to omit tag).
 * Caller frees malloc output. No MakerNote or album database edits. */
bool album_image_exif(const uint8_t *jpeg,size_t jpeg_len,const char *date_time,
 const uint8_t *thumbnail,size_t thumbnail_len,uint8_t **out,size_t *out_len);
#ifdef __3DS__
typedef enum {
 ALBUM_IMAGE_OK, ALBUM_IMAGE_INPUT, ALBUM_IMAGE_MEMORY, ALBUM_IMAGE_CODEC
} AlbumImageErrorKind;
typedef struct {
 AlbumImageErrorKind kind;
 const char *stage; /* static text; no allocated error buffers */
 int code;          /* negative FFmpeg error or -errno */
} AlbumImageError;
/* Same bool API retained; detailed variant reports the failing boundary.
 * FFmpeg MJPEG uses standard Huffman tables, one thread, direct BMP scaling.
 * No full RGB canvas; optional160x120 thumbnail preserves EXIF compatibility. */
bool album_image_encode_detailed(const uint8_t *bmp,size_t length,const char *date_time,
 uint8_t **jpeg,size_t *jpeg_len,AlbumImageError *error);
bool album_image_encode(const uint8_t *bmp,size_t length,const char *date_time,
 uint8_t **jpeg,size_t *jpeg_len);
#endif
#endif
