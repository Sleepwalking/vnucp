#ifndef VNUCP_H
#define VNUCP_H

#define VNUCP_FC0         19500
#define VNUCP_FC1         20500
#define VNUCP_BW          500
#define VNUCP_BPFORD      64
#define VNUCP_BPFORD_D    80
#define VNUCP_MAXCHUNK    2048
#define VNUCP_CHUNKTAIL   256
#define VNUCP_CHUNKHEAD   256

// circular buffer
typedef struct {
  int size;
  int wpos;           // read position
  int rpos;           // write position
  FP_TYPE* x;         // data
} vnucp_cbuffer;

typedef struct {
  int fs;             // sampling rate
  FP_TYPE rate_min;   // minimum clock interval (sec)
  FP_TYPE rate_max;   // maximum clock interval (sec)
} vnucp_config;

typedef struct {
  vnucp_config config;
  vnucp_cbuffer* buffer0;
  vnucp_cbuffer* buffer1;
  FP_TYPE rate_curr;
  FP_TYPE osc0;
  FP_TYPE osc1;
  FP_TYPE* h0;
  FP_TYPE* h1;
  FP_TYPE clock;
} vnucp_esession;

vnucp_cbuffer* vnucp_create_cbuffer(int size);
void vnucp_delete_cbuffer(vnucp_cbuffer* dst);
// returns data + offset
FP_TYPE* vnucp_cbuffer_append(vnucp_cbuffer* dst, FP_TYPE* data, FP_TYPE* data_end);
FP_TYPE* vnucp_cbuffer_peek(vnucp_cbuffer* src, int* ny);
FP_TYPE* vnucp_cbuffer_read(vnucp_cbuffer* src, int* n);
int vnucp_cbuffer_getmargin(vnucp_cbuffer* src);

vnucp_config vnucp_new();
vnucp_esession* vnucp_encode_begin(vnucp_config config);
FP_TYPE* vnucp_encode_append(vnucp_esession* session, char* bitdata, int ndata,
  FP_TYPE rate, int* ny);
FP_TYPE* vnucp_encode_finalize(vnucp_esession* session, int* ny);

typedef struct {
  vnucp_config config;
  vnucp_cbuffer* srcbuffer;
  vnucp_cbuffer* buffer0;
  vnucp_cbuffer* buffer1;
  FP_TYPE* h0;
  FP_TYPE* h1;
  FP_TYPE* hs;
} vnucp_dsession;

vnucp_dsession* vnucp_decode_begin(vnucp_config config);
FP_TYPE* vnucp_decode_append(vnucp_dsession* session, FP_TYPE* x, int nx, int* ndata);
FP_TYPE* vnucp_decode_finalize(vnucp_dsession* session, int* ndata);

#endif
