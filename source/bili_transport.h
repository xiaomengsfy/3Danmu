#ifndef BILI_TRANSPORT_H
#define BILI_TRANSPORT_H
#include "bili.h"
#include "net.h"
/* Synchronous worker-only helpers. Caller always net_response_free(out).
 * Relative paths use https://api.bilibili.com; absolute HTTPS URLs allowed
 * only for known *.bilibili.com read endpoints. GET gzip bounded at2MiB.
 * sign=true requires a relative WBI endpoint. Envelope validates code and
 * invalidates current identity on definitive login rejection. No JSON cache. */
BiliApiResult bili_fetch_json(const char *path,const char **keys,const char **vals,
                             int count,bool sign,HttpResponse *out);
/* Explicit account mutation, adds csrf from runtime session, once/no redirect
 * or retry. Caller must not supply csrf. Login required, n<=15. */
BiliApiResult bili_submit_json(const char *path,const char **keys,const char **vals,
                              int count,HttpResponse *out);
#endif
