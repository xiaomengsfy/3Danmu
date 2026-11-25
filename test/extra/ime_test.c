#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "ime_policy.h"
int main(void) {
 char text[16]="中", py[25]="love"; int n=4;
 assert(!ime_confirm(text,sizeof text,py,&n));
 assert(!strcmp(text,"中love") && !strcmp(py,"") && n==0);
 assert(ime_confirm(text,sizeof text,py,&n));
 char key[25]; ime_lookup_key("nve",3,key,sizeof key);
 assert(!strcmp(key,"nue"));
 strcpy(py,"v"); n=1; assert(!ime_confirm(text,sizeof text,py,&n));
 assert(!strcmp(text,"中lovev"));
 char tiny[3]="ab"; strcpy(py,"v"); n=1;
 assert(!ime_confirm(tiny,sizeof tiny,py,&n));
 assert(n==1 && !strcmp(py,"v") && !strcmp(tiny,"ab"));
 puts("IME policy PASS");
}
