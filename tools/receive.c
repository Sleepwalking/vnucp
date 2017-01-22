#include "portaudio.h"
#include <stdio.h>
#include "sndfile.h"
#include <stdint.h>
#include "../vnucp.h"
#include "../external/ciglet/ciglet.h"
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define SAMPLE_RATE (44100)

typedef struct
    {
    	int max_frame;
      	FP_TYPE  * recordedSamples;
      	int frame_location;
      	vnucp_esession* mainss;
      	int circ_buff_use;
      	vnucp_cbuffer* cbuff;
    }
  	paTestData;

static int play_callback(   const void *inputBuffer, void *outputBuffer,
							unsigned long framesPerBuffer,
							const PaStreamCallbackTimeInfo* timeInfo,
							PaStreamCallbackFlags statusFlags,
							void *userData 
						)
{

	paTestData *data = (paTestData*)userData;
	FP_TYPE * in_noise = (FP_TYPE *) inputBuffer;
	unsigned int framesLeft = data->max_frame - data->frame_location;
	FP_TYPE * record_in = &data->recordedSamples[data->frame_location];

	int nread = framesPerBuffer;

	FP_TYPE * read_from_circular;
	int writesLeft = 0;


	if(framesLeft < framesPerBuffer){
		writesLeft = framesLeft;
		finished = paComplete;
	}
	else{
		writesLeft = framesPerBuffer;
		finished = paContinue;		
	}

	for(i = 0; i < framesPerBuffer; i++){
		*out++ = *in_noise++;
	}

	finished = paContinue;

	data->circ_buff_use -= nread;

	return finished;
}


int main(int argc, char * argv[]){

	paTestData  * data = (paTestData*) malloc(sizeof(paTestData));
	data->audio_mask = 0;
	char * audio_name = NULL;

	if (argc == 1 || argc > 3){
		printf("Usage: ./transmit <data binary> <optional embedded audio data>\n");
		exit(0);
	}
	else if(argc == 3){
		audio_name = argv[2];
		data->audio_mask = 1;
	}
	
	char * binary_dat = argv[1];
	PaStream *stream;
 	
	// Init the final thing
	vnucp_config maincfg = vnucp_new();
	data->mainss = vnucp_encode_begin(maincfg);

	FILE * dat_file = fopen(binary_dat, "rb");
	fseek(dat_file, 0L, SEEK_END);
	int dat_frames = 0;
	dat_frames = ftell(dat_file);
	rewind(dat_file);

	SF_INFO file_info_audio;
	SNDFILE* audio_file; 

	data->embedded_data = (char *) malloc(sizeof(char) * dat_frames);
	int reads = fread(data->embedded_data, 1, dat_frames, dat_file);
	data->noise_buffer = (FP_TYPE *) malloc(sizeof(FP_TYPE) * (512 + 1));
	data->noise_index = 0;
	data->frame_location = 0;
	data->max_frame = 0;
	data->circ_buff_use  = 0;
	data->cbuff = vnucp_create_cbuffer(8096);
	data->data_index = 0;
	data->max_data = dat_frames;
	data->average_noise = 0;

	file_info_audio.frames = 0;
	if(data->audio_mask && audio_name != NULL){
		audio_file =  sf_open(audio_name, SFM_READ, &file_info_audio);
		data->recordedSamples = (FP_TYPE *) malloc(sizeof(FP_TYPE) * (file_info_audio.frames + 1));
		data->max_frame = file_info_audio.frames; 
		short * temp = (short *) malloc(sizeof(short) * file_info_audio.frames);
		sf_read_short (audio_file, temp, file_info_audio.frames);
		for(int z = 0; z < file_info_audio.frames; z++){
			data->recordedSamples[z] = temp[z] * 1.0 / 32767;
		}
		free(temp);
	}

	PaError err = 0;
   	err = Pa_Initialize();
 	if(err != paNoError) {
	  printf("PortAudio Config Failed");
	  printf( "PortAudio error: %s\n", Pa_GetErrorText(err));
  	}

	PaDeviceIndex outdev = Pa_GetDefaultOutputDevice();
  	/* Open the Output to transmit binary data */
	err = Pa_OpenDefaultStream (    &stream,
  									1,
  									0,
  									paFloat32,
  									SAMPLE_RATE,
  									512, // Placeholder
  									play_callback,
  									data );
  	if(err != paNoError) {
  		printf( "PortAudio error: %s\n", Pa_GetErrorText(err));
  	}

  	err = Pa_StartStream(stream);
	if(err != paNoError) {
		printf("PortAudio error: %s\n", Pa_GetErrorText(err));
  	}

	while((err = Pa_IsStreamActive(stream)) == 1) Pa_Sleep(100);

	if(err != paNoError) {
		printf("PortAudio error: %s\n", Pa_GetErrorText(err));
	}

  	err = Pa_StopStream( stream );
	if(err != paNoError) {
		printf( "PortAudio error: %s\n", Pa_GetErrorText(err));
	}

	err = Pa_CloseStream( stream );
	if(err != paNoError) {
		printf( "PortAudio error: %s\n", Pa_GetErrorText(err));
	}

	err = Pa_Terminate();
	if(err != paNoError) {
		printf( "PortAudio error: %s\n", Pa_GetErrorText(err));
	}

}