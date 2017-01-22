#define _POSIX_C_SOURCE 200809L

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
  FP_TYPE* blank = calloc(VNUCP_MAXCHUNK / 2, sizeof(FP_TYPE));
  vnucp_cbuffer_append(ret -> buffer0, blank, blank + VNUCP_MAXCHUNK / 2);
  vnucp_cbuffer_append(ret -> buffer1, blank, blank + VNUCP_MAXCHUNK / 2);
  free(blank);
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
  FP_TYPE* blank = calloc(VNUCP_MAXCHUNK / 2, sizeof(FP_TYPE));
  vnucp_cbuffer_append(session -> buffer0, blank, blank + VNUCP_MAXCHUNK / 2);
  vnucp_cbuffer_append(session -> buffer1, blank, blank + VNUCP_MAXCHUNK / 2);
  free(blank);
  FP_TYPE* y = vnucp_encode_update_buffer(session, ny);
  vnucp_delete_cbuffer(session -> buffer0);
  vnucp_delete_cbuffer(session -> buffer1);
  free(session -> h0);
  free(session -> h1);
  free(session);
  return y;
}

vnucp_dsession* vnucp_decode_begin(vnucp_config config) {
  vnucp_dsession* ret = malloc(sizeof(vnucp_dsession));
  ret -> config = config;
  ret -> srcbuffer = vnucp_create_cbuffer(VNUCP_MAXCHUNK);
  ret -> buffer0 = vnucp_create_cbuffer(VNUCP_MAXCHUNK * 2);
  ret -> buffer1 = vnucp_create_cbuffer(VNUCP_MAXCHUNK * 2);
  ret -> h0 = fir1bp(VNUCP_BPFORD_D, (FP_TYPE)(VNUCP_FC0 - 10) / config.fs * 2,
    (FP_TYPE)(VNUCP_FC0 + 10) / config.fs * 2, "hamming");
  ret -> h1 = fir1bp(VNUCP_BPFORD_D, (FP_TYPE)(VNUCP_FC1 - 10) / config.fs * 2,
    (FP_TYPE)(VNUCP_FC1 + 10) / config.fs * 2, "hamming");
  ret -> hs = fir1(VNUCP_BPFORD, 500.0 / config.fs * 2, "lowpass", "hamming");
  FP_TYPE* blank = calloc(VNUCP_CHUNKTAIL * 2, sizeof(FP_TYPE));
  vnucp_cbuffer_append(ret -> srcbuffer, blank, blank + VNUCP_CHUNKTAIL * 2);
  vnucp_cbuffer_append(ret -> buffer0, blank, blank + VNUCP_CHUNKTAIL * 2);
  vnucp_cbuffer_append(ret -> buffer1, blank, blank + VNUCP_CHUNKTAIL * 2);
  free(blank);
  return ret;
}

static void vnucp_decode_update_bpf(vnucp_dsession* session) {
  int nx;
  FP_TYPE* x = vnucp_cbuffer_peek(session -> srcbuffer, & nx);
  FP_TYPE a[1] = {1.0};
  FP_TYPE* x0 = filtfilt(session -> h0, VNUCP_BPFORD, a, 1, x, nx);
  FP_TYPE* x1 = filtfilt(session -> h1, VNUCP_BPFORD, a, 1, x, nx);
  int nz = nx - VNUCP_CHUNKHEAD - VNUCP_CHUNKTAIL;
  for(int i = 0; i < nx; i ++) {
    x0[i] = x0[i] * x0[i];
    x1[i] = x1[i] * x1[i];
  }

  FP_TYPE* xp0 = x0 + VNUCP_CHUNKHEAD;
  FP_TYPE* xp1 = x1 + VNUCP_CHUNKHEAD;
  xp0 = vnucp_cbuffer_append(session -> buffer0, xp0, x0 + nx - VNUCP_CHUNKTAIL);
  xp1 = vnucp_cbuffer_append(session -> buffer1, xp1, x1 + nx - VNUCP_CHUNKTAIL);
  session -> srcbuffer -> rpos = (session -> srcbuffer -> rpos + xp0 - x0 - VNUCP_CHUNKHEAD) %
    session -> srcbuffer -> size;
  free(x0);
  free(x1);
  free(x);
}

static FP_TYPE diffsum(FP_TYPE* x, int t, int w) {
  FP_TYPE ret = 0;
  int i;
  for(i = 0; i < w; i ++)
    ret += (x[i] - x[t + i]) * (x[i] - x[t + i]);
  return ret;
}

static FP_TYPE yincorr(FP_TYPE* x, int nx, int w) {
  FP_TYPE normalizer = 0;
  FP_TYPE maxcorr = -1;
  for(int i = 1; i < nx - w; i ++) {
    FP_TYPE diff = diffsum(x, i, w);
    normalizer += diff;
    FP_TYPE d = - diff * (FP_TYPE)i / (normalizer <= 1e-15 ? 1e-15 : normalizer);
    if(i >= 5)
      maxcorr = fmax(maxcorr, d);
  }
  return maxcorr;
}

// assume that x1 starts (at VNUCP_CHUNKHEAD) at a zero-crossing
static FP_TYPE* vnucp_decode_update_smoother(vnucp_dsession* session, int* ndata) {
  int nx;
  FP_TYPE* x0 = vnucp_cbuffer_peek(session -> buffer0, & nx);
  FP_TYPE* x1 = vnucp_cbuffer_peek(session -> buffer1, & nx);
  FP_TYPE a[1] = {1.0};
  FP_TYPE* y0 = filtfilt(session -> hs, VNUCP_BPFORD, a, 1, x0, nx);
  FP_TYPE* y1 = filtfilt(session -> hs, VNUCP_BPFORD, a, 1, x1, nx);
  int nz = nx - VNUCP_CHUNKHEAD - VNUCP_CHUNKTAIL;
  for(int i = 0; i < nx; i ++) {
    y0[i] = sqrt(fmax(1e-10, y0[i]));
    y1[i] = sqrt(fmax(1e-10, y1[i]));
  }
  FP_TYPE threshold = meanfp(y1, nx);
  FP_TYPE data_threshold = max(threshold, maxfp(y0, nx) / 2);

  // downsample and periodicity detection
  FP_TYPE* y1d = calloc(nx / 8, sizeof(FP_TYPE));
  for(int i = 0; i < nx / 8; i ++) y1d[i] = y1[i * 8];
  FP_TYPE ycorr = yincorr(y1d, nx / 8, nx / 8 / 4);
  free(y1d);

  int intervalmin = session -> config.rate_min * session -> config.fs * 0.8;
  int zcridx[100]; zcridx[0] = VNUCP_CHUNKHEAD; int nzcr = 1;
  for(int i = VNUCP_CHUNKHEAD + intervalmin; i < nx - VNUCP_CHUNKTAIL; i ++)
    if((y1[i] > threshold) != (y1[i - 1] > threshold)) {
      zcridx[nzcr ++] = i;
    }

  FP_TYPE* data = NULL;
  if(ycorr > -0.2) {
    data = calloc(nzcr - 1, sizeof(FP_TYPE));
    for(int i = 0; i < nzcr - 1; i ++)
      data[i] = (medianfp(y0 + zcridx[i], zcridx[i + 1] - zcridx[i]) - data_threshold)
        / data_threshold;
    *ndata = nzcr - 1;
  } else
    *ndata = 0;

  nz = nzcr > 1 ? zcridx[nzcr - 1] - VNUCP_CHUNKHEAD : nz;
  session -> buffer0 -> rpos = (session -> buffer0 -> rpos + nz) %
    session -> buffer0 -> size;
  session -> buffer1 -> rpos = session -> buffer0 -> rpos;
  free(x0);
  free(x1);
  free(y0);
  free(y1);
  return data;
}

FP_TYPE* vnucp_decode_append(vnucp_dsession* session, FP_TYPE* x, int nx, int* ndata) {
  int data_capacity = 10;
  int data_size = 0;
  FP_TYPE* data = calloc(data_capacity, sizeof(FP_TYPE));

  FP_TYPE* x_end = x + nx;
  // keep feeding the buffer
  while((x = vnucp_cbuffer_append(session -> srcbuffer, x, x_end)) != x_end) {
    if(vnucp_cbuffer_getmargin(session -> srcbuffer) >= VNUCP_MAXCHUNK - VNUCP_CHUNKTAIL)
      vnucp_decode_update_bpf(session);
    if(vnucp_cbuffer_getmargin(session -> buffer0) >= VNUCP_MAXCHUNK * 1.5) {
      int csize;
      FP_TYPE* chunk = vnucp_decode_update_smoother(session, & csize);
      if(data_size + csize > data_capacity) {
        data_capacity = (data_size + csize) * 2;
        data = realloc(data, data_capacity * sizeof(FP_TYPE));
      }
      for(int i = 0; i < csize; i ++)
        data[data_size ++] = chunk[i];
      free(chunk);
    }
  }
  *ndata = data_size;
  return data;
}

FP_TYPE* vnucp_decode_finalize(vnucp_dsession* session, int* ndata) {
  FP_TYPE* blank = calloc(VNUCP_MAXCHUNK, sizeof(FP_TYPE));
  vnucp_cbuffer_append(session -> srcbuffer, blank, blank + VNUCP_MAXCHUNK);
  free(blank);
  vnucp_decode_update_bpf(session);
  FP_TYPE* chunk = vnucp_decode_update_smoother(session, ndata);
  vnucp_delete_cbuffer(session -> srcbuffer);
  vnucp_delete_cbuffer(session -> buffer0);
  vnucp_delete_cbuffer(session -> buffer1);
  free(session -> h0); free(session -> h1);
  free(session -> hs);
  free(session);
  return chunk;
}
