#ifndef NUMERIC_TEXT_H
#define NUMERIC_TEXT_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
/* No newlib printf long-long dependency; needs21 bytes incl NUL. */
static inline bool numeric_i64(int64_t value,char*out,size_t cap) {
    char reverse[20];size_t n=0,pos=0;bool negative=value<0;
    uint64_t magnitude=negative?(uint64_t)(-(value+1))+1:(uint64_t)value;
    do {reverse[n++]=(char)('0'+magnitude%10);magnitude/=10;}while(magnitude);
    if(!out||cap<n+(negative?1u:0u)+1)return false;
    if(negative)out[pos++]='-';
    while(n)out[pos++]=reverse[--n];
    out[pos]=0;
    return true;
}
#endif
