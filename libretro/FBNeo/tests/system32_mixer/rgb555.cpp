#include <assert.h>
#include <stdio.h>
int main() {
 for(unsigned color=0;color<65536;color++)for(unsigned shadow=0;shadow<2;shadow++) {
  unsigned r=color&31,g=(color>>5)&31,b=(color>>10)&31;
  if(shadow){r>>=1;g>>=1;b>>=1;}
  unsigned expected=(r<<10)|(g<<5)|b;
  unsigned pixel=color;if(shadow)pixel=(pixel>>1)&0x3def;
  unsigned actual=((pixel&31)<<10)|(pixel&0x3e0)|((pixel>>10)&31);
  assert(actual==expected);
 }
 puts("PASS 131072 RGB555 colors/shadow cases");
}
