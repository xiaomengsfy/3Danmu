#include "ime_policy.h"
#include <string.h>
bool ime_confirm(char *text,size_t cap,char *pending,int *n) {
 if (*n <= 0) return true;
 size_t used=strlen(text), count=(size_t)*n;
 if (used < cap && count < cap-used) {
  memcpy(text+used,pending,count+1); pending[0]=0; *n=0;
 }
 return false;
}
void ime_lookup_key(const char *p,size_t len,char *key,size_t cap) {
 if(!cap)return;
 if(len>=cap)len=cap-1;
 for(size_t i=0;i<len;i++) key[i]=p[i]=='v'?'u':p[i];
 key[len]=0;
}
