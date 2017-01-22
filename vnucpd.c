#include "vnucp.h"
#include "external/ciglet/ciglet.h"

int main() {
  int fs, nbit, nx;
  FP_TYPE* x = wavread("../encoded2c.wav", & fs, & nbit, & nx);

  vnucp_config maincfg = vnucp_new(); maincfg.fs = fs;
  vnucp_dsession* mainss = vnucp_decode_begin(maincfg);

  int ndata;
  FP_TYPE* data = vnucp_decode_append(mainss, x, nx, & ndata);
  for(int i = 0; i < ndata; i ++)
    printf("%d", data[i] > 0 ? 0 : 1);
  free(data);
  FP_TYPE* last = vnucp_decode_finalize(mainss, & ndata);
  for(int i = 0; i < ndata; i ++)
    printf("%d", last[i] > 0 ? 0 : 1);
  puts("");
  return 0;
}
