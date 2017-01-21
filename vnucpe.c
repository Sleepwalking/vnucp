#include "vnucp.h"
#include "external/ciglet/ciglet.h"

int main() {
  char data[500];
  for(int i = 0; i < 500; i ++) data[i] = rand() % 2;

  vnucp_config maincfg = vnucp_new();
  vnucp_esession* mainss = vnucp_encode_begin(maincfg);
  int ny = 0;
  int ycapacity = 100000;
  int ysize = 0;
  FP_TYPE* y = calloc(ycapacity, sizeof(FP_TYPE));
  for(int i = 0; i < 11; i ++) {
    int niy;
    //FP_TYPE* iy = vnucp_encode_append(mainss, data + i * 50, 50, 0.002, & niy);
    FP_TYPE* iy;
    if(i == 10) {
      iy = vnucp_encode_finalize(mainss, & niy);
    } else {
      iy = vnucp_encode_append(mainss, data + i * 50, 50, (FP_TYPE)(rand() % 100) / 5000, & niy);
    }
    if(ysize + niy > ycapacity) {
      ycapacity *= 2;
      y = realloc(y, ycapacity * sizeof(FP_TYPE));
    }
    for(int j = 0; j < niy; j ++)
      y[ysize ++] = iy[j];
    free(iy);
  }
  wavwrite(y, ysize, maincfg.fs, 16, "encoded.wav");
  free(y);
  return 0;
}
