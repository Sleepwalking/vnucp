#include "portaudio.h"
#include <stdio.h>
#include "sndfile.h"
#include <stdint.h>
#include "../vnucp.h"
#include "../external/ciglet/ciglet.h"
#include <math.h>

#define SAMPLE_RATE (44100)



typedef short SAMPLE;


typedef struct
    {
    	int max_frame;
      	SAMPLE    *     recordedSamples;
      	FP_TYPE *  noise_buffer;
      	int frame_location;
      	FP_TYPE average_noise;
      	FP_TYPE previous_energy;
      	FP_TYPE rate;
      	int noise_index;
      	vnucp_esession* mainss;
      	int circ_buff_use;
      	vnucp_cbuffer* cbuff;

    }
  	paTestData;


static int play_callback( const void *inputBuffer, void *outputBuffer,
							unsigned long framesPerBuffer,
							const PaStreamCallbackTimeInfo* timeInfo,
							PaStreamCallbackFlags statusFlags,
							void *userData 

					){



	paTestData *data = (paTestData*)userData;
	SAMPLE * data_to_blast = &data->recordedSamples[data->frame_location];
	SAMPLE * out = (SAMPLE *) outputBuffer;
	SAMPLE * in_noise = (SAMPLE *) inputBuffer;
	FP_TYPE * curr_noise = (FP_TYPE *) data->noise_buffer;

	int finished;
	unsigned int framesLeft = data->max_frame - data->frame_location;
	int i = 0;

	FP_TYPE fc_low = 1.0*17000/22050.0;
	FP_TYPE fc_high = 1.0*19000/22050.0;

	FP_TYPE a[1] = {1.0};

	// Create the filter.
	FP_TYPE * noise_bp = fir1bp(64, fc_low, fc_high, "hamming");

	int k = 0;

	// Window the signal
	for(k = 0; k < 25; k++){
		curr_noise[k] * 1.0 / (25-k);
	}

	for(k = 487; k < 512; k++){
		curr_noise[k] * -1.0 / (487-k);
	}


	// Convolve the signal
	FP_TYPE* filtered_noise = filtfilt(noise_bp, 64, a, 1, curr_noise, 512);

	// Sum the square of the signal values
	FP_TYPE sum = 0;
	for(k = 0; k < 512; k++){
		sum += filtered_noise[k] * filtered_noise[k];
	}

	data->previous_energy = data->average_noise;

	// Get the average noise.
	FP_TYPE current_energy = sqrt(sum);
	data->average_noise = .1 * current_energy + .9 * data->previous_energy; 


	//printf("Smoothed Energy: %f\n", data->average_noise);


	/////////////////////////////////////////////////////
	int niy = 0;

	int num_og_samples = 50;

	int final = 0;

	FP_TYPE * add_to_circ;

	while(data->circ_buff_use < 512 && final != 1){

		if(framesLeft < 50){
			final = 1;
			add_to_circ = vnucp_encode_finalize(mainss, & niy);
			// Update that pointer
			data->frame_location += framesLeft;
		}
		else{

				// Add encoded samples to the circular buffer
				add_to_circ = vnucp_encode_append(data->mainss, 
												&data->recordedSamples[data->frame_location],
												num_og_samples,
												.01, //(FP_TYPE)(rand() % 100) / 5000, // need noise TODO: 
												&niy);

				// Update that pointer
				data->frame_location += num_og_samples;
		}

		// Have the items to add to circular buffer and the amount of them
		FP_TYPE add_p = add_to_circ;

//		FP_TYPE diff = add_to_circ; might need this idk...

		// Writes the buffer of encoded samples to the circular buffer
		while(add_p = vnucp_cbuffer_append(data->cbuff, add_p, add_to_circ + niy) != add_to_circ + niy);

		// Now we know we have niy more samples
		data->circ_buff_use += niy;
		

	}

	// Now we can pipe this through as audio.

	(void) timeInfo;
	(void) statusFlags;
	//(void) userData;


	int nread = framesPerBuffer;

	SAMPLE * read_from_circular;

	if( framesLeft < framesPerBuffer)
	{

		nread = framesLeft;

		read_from_circular = vnucp_cbuffer_read(data->cbuff, & nread);


		for(i = 0; i < framesLeft; i++){
		
			*out++ = *read_from_circular++;
			
		}
		for(;i<framesPerBuffer;i++){
			*out++=0;
		}

		//data->frame_location += framesLeft;
		finished = paComplete;

	}
	else
	{

		read_from_circular = vnucp_cbuffer_read(data->cbuff, & nread);

		for(i = 0; i < framesPerBuffer; i++){
			*curr_noise++ = (*in_noise++) * 1.0 / 32677;
			
			*out++ = *read_from_circular++;
			
	
		}
		//data->frame_location += framesPerBuffer;
		finished = paContinue;
	}

	data->circ_buff_use -= nread;

	return finished;

}


int main(int argc, char * argv[]){


	if (argc != 2){
		printf("Usage: ./transmit <data>\n");
		exit(0);
	}

	PaStream *stream;
 	PaError err;

	static paTestData data;

	// Init the final thing
	vnucp_config maincfg = vnucp_new();
	data->mainss = vnucp_encode_begin(maincfg);


	SF_INFO file_info;
	SNDFILE* wav_file =  sf_open(argv[1], SFM_READ, &file_info);

	//printf("Num frames = %d\n", (int) file_info.frames);

	data.recordedSamples = (short *) malloc(sizeof(short) * file_info.frames);

	data.noise_buffer = (FP_TYPE *) malloc(sizeof(FP_TYPE) * 512);
	data.noise_index = 1;

	//printf("here -  before : %hd\n", data.recordedSamples[400]);
	//printf("here -  before : %f\n", data.noise_buffer[400]);

	

	//printf("here : %hd\n", data.recordedSamples[400]);

	/////////////////////////////////////////////////////////
	data.frame_location = 0;
	data.max_frame = file_info.frames;
	data.circ_buff_use  = 0;
	data.cbuff = vnucp_create_cbuffer(2048);
	sf_read_short (wav_file, data.recordedSamples, file_info.frames);
	// At this point we have the audio/binary file
	// If we want to use something other than audio, we can
	// just read binary files.  this is just easier i think in
	// terms of easily reading audio files.


   err = Pa_Initialize();
  if(err != paNoError) {
	  printf("PortAudio Config Failed");
	  printf( "PortAudio error: %s\n", Pa_GetErrorText(err));

  }


  PaDeviceIndex outdev = Pa_GetDefaultOutputDevice();

 // printf("out device number: %d\n", (int) outdev);

  
  /* Open the Output to transmit binary data */
  err = Pa_OpenDefaultStream ( &stream,
  									1,
  									1,
  									paInt16,
  									SAMPLE_RATE,
  									512, // Placeholder
  									play_callback,
  									&data );



  	if(err != paNoError) {
  		printf( "PortAudio error: %s\n", Pa_GetErrorText(err));

  	}


  	err = Pa_StartStream( stream );
	if(err != paNoError) {
	  printf( "PortAudio error: %s\n", Pa_GetErrorText(err));
  	}

  	printf("Waiting for playback to finish.\n"); fflush(stdout);

	while( ( err = Pa_IsStreamActive( stream ) ) == 1 ) Pa_Sleep(100);



  	err = Pa_StopStream( stream );
	if(err != paNoError) {
	  printf( "PortAudio error: %s\n", Pa_GetErrorText(err));
  }

err = Pa_CloseStream( stream );
if(err != paNoError) {
	  printf( "PortAudio error: %s\n", Pa_GetErrorText(err));
  }


  printf("Done transmitting sound\n");

  printf("Average noise at end was %f\n", (float) data.average_noise);


  printf("here -  after : %f\n", data.noise_buffer[400]);

  err = Pa_Terminate();
  if(err != paNoError) {
	  printf( "PortAudio error: %s\n", Pa_GetErrorText(err));
  }


}