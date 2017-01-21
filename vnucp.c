#include "vnucp.h"
#include "external/ciglet/ciglet.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

vnucp_cbuffer* vnucp_create_cbuffer(int size) {
  vnucp_cbuffer* ret = malloc(sizeof(vnucp_cbuffer));
  ret -> size = size;
  ret -> wpos = 0;
  ret -> rpos = 0;
  ret -> x = calloc(size, sizeof(FP_TYPE));
  return ret;
}

void vnucp_delete_cbuffer(vnucp_cbuffer* dst) {
  if(dst == NULL) return;
  free(dst -> x);
  free(dst);
}

// returns data + offset
FP_TYPE* vnucp_cbuffer_append(vnucp_cbuffer* dst, FP_TYPE* data, FP_TYPE* data_end) {
  //printf("Append %x %x\n", data, data_end);
  while(data != data_end && (dst -> wpos + 1) % dst -> size != dst -> rpos) {
    dst -> x[dst -> wpos] = *data;
    dst -> wpos = (dst -> wpos + 1) % dst -> size;
    data ++;
  }
  //printf("At the end of append, wpos = %d, rpos = %d\n", dst -> wpos, dst -> rpos);
  return data;
}

FP_TYPE* vnucp_cbuffer_peek(vnucp_cbuffer* src, int* ny) {
  FP_TYPE* y = calloc(src -> size, sizeof(FP_TYPE));
  *ny = 0;
  int rpos = src -> rpos;
  while(rpos != src -> wpos) {
    y[*ny] = src -> x[rpos];
    rpos = (rpos + 1) % src -> size;
    (*ny) ++;
  }
  return y;
}

FP_TYPE* vnucp_cbuffer_read(vnucp_cbuffer* src, int* n) {
  FP_TYPE* y = calloc(*n, sizeof(FP_TYPE));
  int ny = 0;
  while(ny != *n && src -> rpos != src -> wpos) {
    y[ny] = src -> x[src -> rpos];
    src -> rpos = (src -> rpos + 1) % src -> size;
    ny ++;
  }
  *n = ny;
  //printf("At the end of read, wpos = %d, rpos = %d\n", src -> wpos, src -> rpos);
  return y;
}

int vnucp_cbuffer_getmargin(vnucp_cbuffer* src) {
  if(src -> wpos > src -> rpos)
    return src -> wpos - src -> rpos;
  return src -> wpos + src -> size - src -> rpos;
}

vnucp_config vnucp_new() {
  vnucp_config ret;
  ret.fs = 44100;
  ret.rate_min = 0.002;
  ret.rate_max = 0.05;
  return ret;
}

vnucp_esession* vnucp_encode_begin(vnucp_config config) {
  vnucp_esession* ret = malloc(sizeof(vnucp_esession));
  ret -> config = config;
  ret -> rate_curr = sqrt(config.rate_min * config.rate_max); // geometric mean
  ret -> buffer0 = vnucp_create_cbuffer(VNUCP_MAXCHUNK);
  ret -> buffer1 = vnucp_create_cbuffer(VNUCP_MAXCHUNK);
  ret -> osc0 = 0;
  ret -> osc1 = 0;
  ret -> clock = 1;
  ret -> h0 = fir1bp(VNUCP_BPFORD, (FP_TYPE)(VNUCP_FC0 - VNUCP_BW) / config.fs * 2,
    (FP_TYPE)(VNUCP_FC0 + VNUCP_BW) / config.fs * 2, "hamming");
  ret -> h1 = fir1bp(VNUCP_BPFORD, (FP_TYPE)(VNUCP_FC1 - VNUCP_BW) / config.fs * 2,
    (FP_TYPE)(VNUCP_FC1 + VNUCP_BW) / config.fs * 2, "hamming");
  FP_TYPE* white = calloc(VNUCP_MAXCHUNK / 2, sizeof(FP_TYPE));
  vnucp_cbuffer_append(ret -> buffer0, white, white + VNUCP_MAXCHUNK / 2);
  vnucp_cbuffer_append(ret -> buffer1, white, white + VNUCP_MAXCHUNK / 2);
  free(white);
  return ret;
}

static FP_TYPE* vnucp_encode_update_buffer(vnucp_esession* session, int* nz) {
  int nx;
  FP_TYPE* x0 = vnucp_cbuffer_peek(session -> buffer0, & nx);
  FP_TYPE* x1 = vnucp_cbuffer_peek(session -> buffer1, & nx);
  FP_TYPE a[1] = {1.0};
  FP_TYPE* y0 = filtfilt(session -> h0, VNUCP_BPFORD, a, 1, x0, nx);
  FP_TYPE* y1 = filtfilt(session -> h1, VNUCP_BPFORD, a, 1, x1, nx);
  free(x0); free(x1);

  *nz = vnucp_cbuffer_getmargin(session -> buffer0);
  FP_TYPE* z = calloc(*nz, sizeof(FP_TYPE));
  int i = 0;
  for(i = VNUCP_CHUNKHEAD; i < *nz - VNUCP_BPFORD * 2; i ++) {
    z[i - VNUCP_CHUNKHEAD] = (y0[i] + y1[i]) * 0.5;
    session -> buffer0 -> rpos = (session -> buffer0 -> rpos + 1) %
      session -> buffer0 -> size;
    session -> buffer1 -> rpos = session -> buffer0 -> rpos;
  }
  *nz = i - VNUCP_CHUNKHEAD;

  free(y0); free(y1);
  return z;
}

FP_TYPE* vnucp_encode_append(vnucp_esession* session, char* bitdata, int ndata,
    FP_TYPE rate, int* ny_) {
  FP_TYPE* chunk = calloc(VNUCP_MAXCHUNK, sizeof(FP_TYPE));
  FP_TYPE** parts = calloc(ndata, sizeof(FP_TYPE**));
  int* partsn = calloc(ndata, sizeof(int));
  int nparts = 0;
  int ny = 0;
  for(int i = 0; i < ndata; i ++) {
    FP_TYPE currbit = bitdata[i];
    int currinterval = round(session -> config.fs * session -> rate_curr);

    // update channel 0 (data)
    for(int j = 0; j < currinterval; j ++) {
      chunk[j] = sin(session -> osc0) * currbit;
      session -> osc0 += 2.0 * M_PI / session -> config.fs * VNUCP_FC0;
    }
    vnucp_cbuffer_append(session -> buffer0, chunk, chunk + currinterval);
    
    // update channel 1 (clock)
    for(int j = 0; j < currinterval; j ++) {
      chunk[j] = sin(session -> osc1) * (session -> clock > 0 ? 1.0 : 0);
      session -> osc1 += 2.0 * M_PI / session -> config.fs * VNUCP_FC1;
    }
    vnucp_cbuffer_append(session -> buffer1, chunk, chunk + currinterval);

    // BPF & mixing
    if(vnucp_cbuffer_getmargin(session -> buffer0) >= VNUCP_MAXCHUNK - VNUCP_CHUNKTAIL) {
      int nout = 0;
      FP_TYPE* out = vnucp_encode_update_buffer(session, & nout);
      partsn[nparts] = nout;
      parts[nparts ++] = out;
      ny += nout;
    }

    session -> rate_curr = session -> rate_curr * 0.98 + rate * 0.02;
    session -> clock = -session -> clock;
  }
  free(chunk);

  // concatenate
  FP_TYPE* y = calloc(ny, sizeof(FP_TYPE));
  ny = 0;
  for(int i = 0; i < nparts; i ++) {
    for(int j = 0; j < partsn[i]; j ++)
      y[ny ++] = parts[i][j];
    free(parts[i]);
  }
  free(parts); free(partsn);
  *ny_ = ny;
  return y;
}

FP_TYPE* vnucp_encode_finalize(vnucp_esession* session, int* ny) {
  FP_TYPE* white = calloc(VNUCP_MAXCHUNK / 2, sizeof(FP_TYPE));
  vnucp_cbuffer_append(session -> buffer0, white, white + VNUCP_MAXCHUNK / 2);
  vnucp_cbuffer_append(session -> buffer1, white, white + VNUCP_MAXCHUNK / 2);
  free(white);
  FP_TYPE* y = vnucp_encode_update_buffer(session, ny);
  vnucp_delete_cbuffer(session -> buffer0);
  vnucp_delete_cbuffer(session -> buffer1);
  free(session -> h0);
  free(session -> h1);
  free(session);
  return y;
}
