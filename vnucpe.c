#define _POSIX_C_SOURCE 200809L

#include <portaudio.h>
#include <pthread.h>
#include "vnucp.h"
#include "external/ciglet/ciglet.h"
#include <unistd.h>

static void print_usage() {
  fprintf(stderr, "vnucpe [-r rate] [-o output-wav-file] < input-binary-text\n");
  exit(0);
}

pthread_mutex_t nextchar_mx = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t nextchar_cv;
pthread_t fetch_thread;
vnucp_cbuffer* nextchar = NULL;

typedef struct {
  vnucp_esession* session;
  vnucp_cbuffer* outbuffer;
} playdata;

static void* fetch_data(void* x) {
  char d;
  while((d = fgetc(stdin)) != EOF) {
    pthread_mutex_lock(& nextchar_mx);
    while(vnucp_cbuffer_getmargin(nextchar) > 100) {
      pthread_cond_wait(& nextchar_cv, & nextchar_mx);
    }
    for(int i = 0; i < 8; i ++) {
      FP_TYPE next = (d >> i) & 0x01;
      vnucp_cbuffer_append(nextchar, & next, (& next) + 1);
    }
    //FP_TYPE next = d == '0' ? 0.0 : 1.0;
    pthread_mutex_unlock(& nextchar_mx);
  }
  return NULL;
}

static int play_callback(const void *inputBuffer, void *outputBuffer,
  unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo* timeInfo,
  PaStreamCallbackFlags statusFlags, void *userData) {
  playdata* env = (playdata*)userData;
  FP_TYPE* outbuffer = (FP_TYPE*)outputBuffer;
  while(vnucp_cbuffer_getmargin(env -> outbuffer) < framesPerBuffer * 2) {
    pthread_mutex_lock(& nextchar_mx);
    if(vnucp_cbuffer_getmargin(nextchar) <= 1) {
      FP_TYPE* blank = calloc(framesPerBuffer, sizeof(FP_TYPE));
      vnucp_cbuffer_append(env -> outbuffer, blank, blank + framesPerBuffer);
      free(blank);
      pthread_mutex_unlock(& nextchar_mx);
      break;
    }
    int len = 1;
    FP_TYPE* cread = vnucp_cbuffer_read(nextchar, & len);
    char c = cread[0];
    FP_TYPE* chunk = vnucp_encode_append(env -> session, & c, 1,
      env -> session -> rate_curr, & len);
    if(chunk != NULL && len > 0)
      vnucp_cbuffer_append(env -> outbuffer, chunk, chunk + len);
    free(cread);
    free(chunk);
    pthread_cond_broadcast(& nextchar_cv);
    pthread_mutex_unlock(& nextchar_mx);
  }
  int nread = framesPerBuffer;
  FP_TYPE* out = vnucp_cbuffer_read(env -> outbuffer, & nread);
  for(int i = 0; i < nread; i ++)
    outbuffer[i] = out[i];
  free(out);
  return 0;
}

int main(int argc, char** argv) {
  vnucp_config maincfg = vnucp_new();
  vnucp_esession* mainss = vnucp_encode_begin(maincfg);
  char* outpath = NULL;

  char opt;
  while((opt = getopt(argc, argv, "r:o:")) != -1) {
    switch(opt) {
      case 'r':
        mainss -> rate_curr = atof(optarg);
        break;
      case 'o':
        outpath = strdup(optarg);
        break;
    }
  }

  if(outpath != NULL) { // to file
    int ycapacity = 10000;
    int ysize = 0;
    FP_TYPE* y = malloc(ycapacity * sizeof(FP_TYPE));
    char d;
    while((d = fgetc(stdin)) != EOF) {
      d = d == '0' ? 0 : 1;
      int len = 0;
      FP_TYPE* chunk = vnucp_encode_append(mainss, & d, 1, mainss -> rate_curr, & len);
      if(ysize + len > ycapacity) {
        ycapacity = (ysize + len) * 2;
        y = realloc(y, ycapacity * sizeof(FP_TYPE));
      }
      for(int i = 0; i < len; i ++)
        y[ysize ++] = chunk[i];
      free(chunk);
    }
    int len = 0;
    FP_TYPE* chunk = vnucp_encode_finalize(mainss, & len);
    if(ysize + len > ycapacity) {
      ycapacity = (ysize + len) * 2;
      y = realloc(y, ycapacity * sizeof(FP_TYPE));
    }
    for(int i = 0; i < len; i ++)
      y[ysize ++] = chunk[i];
    wavwrite(y, ysize, maincfg.fs, 16, outpath);
    free(y);
    free(chunk);
    free(outpath);
    return 0;
  }

  playdata maindat;
  maindat.session = mainss;
  maindat.outbuffer = vnucp_create_cbuffer(8092);
  maindat.outbuffer -> wpos ++;
  nextchar = vnucp_create_cbuffer(1024);
  nextchar -> wpos ++;
  pthread_cond_init(& nextchar_cv, NULL);
  pthread_create(& fetch_thread, NULL, fetch_data, NULL);

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
  err = Pa_OpenDefaultStream(& stream, 1, 1, paFloat32, maincfg.fs, 512,
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

  vnucp_delete_cbuffer(maindat.outbuffer);
  vnucp_delete_cbuffer(nextchar);
  return 0;
}
