#define _POSIX_C_SOURCE 200809L

#include <portaudio.h>
#include <pthread.h>
#include "vnucp.h"
#include "external/ciglet/ciglet.h"
#include <unistd.h>

typedef struct {
  vnucp_dsession* session;
} playdata;

int cnt = 0;

static int play_callback(const void *inputBuffer, void *outputBuffer,
  unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo* timeInfo,
  PaStreamCallbackFlags statusFlags, void *userData) {
  playdata* env = (playdata*)userData;
  int ndata;
  FP_TYPE* data = vnucp_decode_append(env -> session, (FP_TYPE*)inputBuffer,
    framesPerBuffer, & ndata);
  for(int i = 0; i < ndata; i ++) {
    printf("%d", data[i] > 0 ? 1 : 0);
    if((cnt ++) % 8 == 0)
      puts("");
  }
  fflush(stdout);
  free(data);
  return 0;
}

int main() {
  vnucp_config maincfg = vnucp_new();
  vnucp_dsession* mainss = vnucp_decode_begin(maincfg);

  playdata maindat;
  maindat.session = mainss;

  #define check_port_error() \
    if(err != paNoError) { \
      printf("PortAudio error: %s\n", Pa_GetErrorText(err)); \
      exit(1); \
    }
  PaError err = 0;
  PaStream* stream;
  err = Pa_Initialize();
  if(err != paNoError) {
    printf("PortAudio Config Failed");
    printf("PortAudio error: %s\n", Pa_GetErrorText(err));
    exit(1);
  }
  PaDeviceIndex outdev = Pa_GetDefaultOutputDevice();
  err = Pa_OpenDefaultStream(& stream, 1, 0, paFloat32, maincfg.fs, 512,
    play_callback, & maindat);
  check_port_error();
  err = Pa_StartStream(stream);
  check_port_error();
  while((err = Pa_IsStreamActive(stream)) == 1) Pa_Sleep(100);
  check_port_error();
  err = Pa_StopStream(stream);
  check_port_error();
  err = Pa_CloseStream(stream);
  check_port_error();
  err = Pa_Terminate();
  check_port_error();
  return 0;
/*
  int fs, nbit, nx;
  FP_TYPE* x = wavread("../encoded4.wav", & fs, & nbit, & nx);

  vnucp_config maincfg = vnucp_new(); maincfg.fs = fs;
  vnucp_dsession* mainss = vnucp_decode_begin(maincfg);

  for(int n = 0; n < nx - 512; n += 512) {
    int ndata;
    FP_TYPE* data = vnucp_decode_append(mainss, x + n, 512, & ndata);
    for(int i = 0; i < ndata; i ++)
      printf("%d", data[i] > 0 ? 1 : 0);
    free(data);
  }
  int ndata;
  FP_TYPE* last = vnucp_decode_finalize(mainss, & ndata);
  for(int i = 0; i < ndata; i ++)
    printf("%d", last[i] > 0 ? 1 : 0);
  puts("");
  free(last);
  free(x);
*/
}
