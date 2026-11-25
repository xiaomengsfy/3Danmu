#include "screenshot.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
int main(void) {
 size_t n=54+400*240*3; unsigned char *fb=calloc(400*240,3),*bmp=calloc(n,1);
 /* top left red, bottom right green; rotated framebuffer offset */
 fb[(0*240+239)*3+2]=255;
 fb[(399*240+0)*3+1]=255;
 assert(!screenshot_bmp(fb,400,240,bmp,n-1));
 assert(!screenshot_bmp(fb,800,240,bmp,n));
 assert(screenshot_bmp(fb,400,240,bmp,n));
 assert(bmp[0]=='B' && bmp[1]=='M' && bmp[18]==0x90 && bmp[19]==1);
 assert(bmp[22]==240 && bmp[28]==24 && bmp[10]==54);
 assert(bmp[54+(239*400)*3+2]==255);
 assert(bmp[54+399*3+1]==255);
 assert(bmp[54+2]==0 && bmp[54+(239*400)*3+1]==0);
 free(fb);free(bmp);puts("Screenshot rotation/BMP PASS");
}
