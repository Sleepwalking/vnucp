#include "../vnucp.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  vnucp_cbuffer* buff = vnucp_create_cbuffer(16);
  FP_TYPE idata[30];
  FP_TYPE count = 0;
  for(int i = 0; i < 20; i ++) {
    int len = rand() % 20 + 5;
    for(int j = 0; j < len; j ++) {
      idata[j] = count;
      count ++;
    }
    FP_TYPE* idatap = idata;
    while((idatap = vnucp_cbuffer_append(buff, idatap, idata + len)) != idata + len) {
      int nread = 5;
      FP_TYPE* iread = vnucp_cbuffer_read(buff, & nread);
      for(int j = 0; j < nread; j ++)
        printf("%f\n", iread[j]);
      free(iread);
    }
  }
  vnucp_delete_cbuffer(buff);
  return 0;
}
