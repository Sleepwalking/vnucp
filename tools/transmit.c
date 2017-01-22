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



typedef short SAMPLE;


typedef struct
    {
    	int max_frame;
    	char * embedded_data;
      	FP_TYPE    *     recordedSamples;
      	FP_TYPE *  noise_buffer;
      	int frame_location;
      	FP_TYPE average_noise;
      	FP_TYPE previous_energy;
      	FP_TYPE rate;
      	int noise_index;
      	vnucp_esession* mainss;
      	int circ_buff_use;
      	vnucp_cbuffer* cbuff;
      	int audio_mask;
      	int data_index;
      	int max_data;

    }
  	paTestData;


static int play_callback( const void *inputBuffer, void *outputBuffer,
							unsigned long framesPerBuffer,
							const PaStreamCallbackTimeInfo* timeInfo,
							PaStreamCallbackFlags statusFlags,
							void *userData 

					){



	paTestData *data = (paTestData*)userData;
	FP_TYPE * out = (FP_TYPE *) outputBuffer;
	FP_TYPE * in_noise = (FP_TYPE *) inputBuffer;
	FP_TYPE * curr_noise = (FP_TYPE *) data->noise_buffer;

	int finished;
	unsigned int framesLeft = data->max_frame - data->frame_location;

	//printf("frames lefT : %d\n", framesLeft);

	unsigned int dataLeft = data->max_data - data->data_index;
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
	FP_TYPE sum = 0.0;
	for(k = 0; k < 512; k++){
		sum += filtered_noise[k] * filtered_noise[k];
	}

	data->previous_energy = data->average_noise;

	// Get the average noise.
	FP_TYPE current_energy = sqrt(sum);
	data->average_noise = .1 * current_energy + .9 * data->previous_energy; 


	//printf("Smoothed Energy: %f\n", data->average_noise);

	///////////////////////////////////////////////////// the good stuff
	int niy = 0;

	int num_og_samples = 50;

	int final = 0;

	FP_TYPE * add_to_circ;


	int niz = 0;


	int just_be_done = 0;

	while((data->circ_buff_use < 512) && (final != 1)){


		if(dataLeft <= 8 && dataLeft != 0){

			// Currently assume audio is longer than data.
			if(!data->audio_mask){
				final = 1;
			}
			

			// Add encoded samples to the circular buffer
				add_to_circ = vnucp_encode_append(data->mainss, 
												&data->embedded_data[data->data_index],
												dataLeft,
												.01, //(FP_TYPE)(rand() % 100) / 5000, // need noise TODO: 
												&niy);



				//printf("NIYFINALS:  %d\n", niy);

			vnucp_encode_finalize(data->mainss, & niz);


			//	printf("NIYFasdasdasdasdINALS:  %d\n", niy);

			// Update that pointer
			data->data_index += dataLeft;
		}
		else if (dataLeft != 0){

				// Add encoded samples to the circular buffer
				add_to_circ = vnucp_encode_append(data->mainss, 
												&data->embedded_data[data->data_index],
												8,
												.01, //(FP_TYPE)(rand() % 100) / 5000, // need noise TODO: 
												&niy);

				// Update that pointer
				data->data_index += 8;

		}


		if(data->audio_mask){

			if(dataLeft > 0){

				for(int z = 0; z < niy; z++){
					data->recordedSamples[data->frame_location + z] +=  add_to_circ[z];
					data->recordedSamples[data->frame_location + z] *= .6;
				}

			}
			else{


				if(framesLeft <= 1000) {
					niy = framesLeft;

					final = 1;
				}
				else{
					niy = 1000;
			
				}

			}

			add_to_circ = &data->recordedSamples[data->frame_location];		
			data->frame_location += niy;

		}

		// Have the items to add to circular buffer and the amount of them
		FP_TYPE* add_p = add_to_circ;

		// FP_TYPE diff = add_to_circ; might need this idk...



		int x = 0;

		// Writes the buffer of encoded samples to the circular buffer 
		while((add_p = vnucp_cbuffer_append(data->cbuff, add_p, add_to_circ + niy)) != add_to_circ + niy){

			printf("ADDP: %p\n", (void *)add_p);
			x++;
			if(x > 10){
				printf("This is stuck here you dingus...\n");
				exit(0);
			}

		}

		// Now we know we have niy more samples
		data->circ_buff_use += niy;
		
		if(final == 1){
			printf("Final: %d\n", final);
			just_be_done = 1;
			//	printf("meeeeeeeeeeeeeeek\n");
		}

	}

	//printf("no idea how this happen %d\n", data->circ_buff_use);

	// Now we can pipe this through as audio.

	(void) timeInfo;
	(void) statusFlags;
	//(void) userData;


	int nread = framesPerBuffer;

	FP_TYPE * read_from_circular;


	int writesLeft = data->circ_buff_use;

	if(data->audio_mask){
		just_be_done = 0;
		writesLeft = framesLeft;
	}


	if( writesLeft < framesPerBuffer || just_be_done == 1)
	{

		nread = writesLeft;

		//printf("nread %d\n", nread);

		read_from_circular = vnucp_cbuffer_read(data->cbuff, & nread);


		for(i = 0; i < 200 ; i++){
		
			*out++ = *read_from_circular++;
			
		}

		for(;i<framesPerBuffer;i++){
			*out++=0;
		}

		//data->frame_location += framesLeft;
		printf("Finishing\n");

		finished = paComplete;

	}
	else
	{

		read_from_circular = vnucp_cbuffer_read(data->cbuff, & nread);

		for(i = 0; i < framesPerBuffer; i++){
			*curr_noise++ = (*in_noise++); //* 1.0 / 32767;
			

			*out++ = *read_from_circular++;
			
	
			//printf("Finishing - fake\n");

		}
		//data->frame_location += framesPerBuffer;
		finished = paContinue;
	}

	data->circ_buff_use -= nread;

	//printf("or this....... %d\n", data->circ_buff_use);	

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


	//SF_INFO file_info_dat;
	//SNDFILE* dat_file =  sf_open(binary_dat, SFM_READ, &file_info_dat);

	FILE * dat_file = fopen(binary_dat, "rb");

	fseek(dat_file, 0L, SEEK_END);
	int dat_frames = 0;

	dat_frames = ftell(dat_file);
	rewind(dat_file);


	printf("datframes: %d\n", dat_frames);

	SF_INFO file_info_audio;
	SNDFILE* audio_file; 

	file_info_audio.frames = 0;

	if(data->audio_mask && audio_name != NULL){
		audio_file =  sf_open(audio_name, SFM_READ, &file_info_audio);
	}

	//printf("Num frames = %d\n", (int) file_info.frames);

	data->embedded_data = (char *) malloc(sizeof(char) * dat_frames);

	if(data->audio_mask) {
		data->recordedSamples = (FP_TYPE *) malloc(sizeof(FP_TYPE) * (file_info_audio.frames + 1));
	}

	data->noise_buffer = (FP_TYPE *) malloc(sizeof(FP_TYPE) * (512 + 1));
	data->noise_index = 0;

	//printf("here -  before : %hd\n", data.recordedSamples[400]);
	//printf("here -  before : %f\n", data.noise_buffer[400]);


	//printf("here : %hd\n", data.recordedSamples[400]);

	////////////////////////////////////////////////////////////////////////
	data->frame_location = 0;
	data->max_frame = 0;
	if(data->audio_mask){
		data->max_frame = file_info_audio.frames; 
	}
	
	data->circ_buff_use  = 0;
	data->cbuff = vnucp_create_cbuffer(8096);
	data->data_index = 0;
	data->max_data = dat_frames;

	data->average_noise = 0;

	int reads = fread(data->embedded_data, 1, dat_frames, dat_file);

	printf("READS: %d\n",reads);

	//sf_read_short (dat_file, data.embedded_data, file_info_dat.frames); //

	// Need to change to read in chars and get same amount of info
	//

	if(data->audio_mask){
		short * temp = (short *) malloc(sizeof(short) * file_info_audio.frames);
		sf_read_short (audio_file, temp, file_info_audio.frames);
		for(int z = 0; z < file_info_audio.frames; z++){
			data->recordedSamples[z] = temp[z] * 1.0 / 32767;
		}
		free(temp);
	}

	// At this point we have the audio/binary file
	// If we want to use something other than audio, we can
	// just read binary files.  this is just easier i think in
	// terms of easily reading audio files.
	///////////////////////////////////////////////////////////////////////


	PaError err = 0;

   err = Pa_Initialize();
  if(err != paNoError) {
	  printf("PortAudio Config Failed");
	  printf( "PortAudio error: %s\n", Pa_GetErrorText(err));

  }


  PaDeviceIndex outdev = Pa_GetDefaultOutputDevice();

  printf("size of FP_TYPE: %ld\n", sizeof(FP_TYPE));


  
  /* Open the Output to transmit binary data */
  err = Pa_OpenDefaultStream ( &stream,
  									1,
  									1,
  									paFloat32,
  									SAMPLE_RATE,
  									512, // Placeholder
  									play_callback,
  									data );



  	if(err != paNoError) {
  		printf( "PortAudio error: %s\n", Pa_GetErrorText(err));

  	}


  	err = Pa_StartStream( stream );
	if(err != paNoError) {
	  printf( "PortAudio error: %s\n", Pa_GetErrorText(err));
  	}

  	//printf("Waiting for playback to finish.\n"); fflush(stdout);

	while( ( err = Pa_IsStreamActive( stream ) ) == 1 ) Pa_Sleep(100);

	if(err != paNoError) {
	  printf( "fukkt PortAudio error: %s\n", Pa_GetErrorText(err));
  }

  	printf("stopping stream\n");

  	err = Pa_StopStream( stream );
	if(err != paNoError) {
	  printf( "PortAudio error: %s\n", Pa_GetErrorText(err));
  }


  printf("closing stream\n");

err = Pa_CloseStream( stream );
if(err != paNoError) {
	  printf( "PortAudio error: %s\n", Pa_GetErrorText(err));
  }


  printf("Done transmitting sound\n");

  printf("Average noise at end was %f\n",  data->average_noise);


  printf("here -  after : %lf\n", data->noise_buffer[400]);

  err = Pa_Terminate();
  if(err != paNoError) {
	  printf( "PortAudio error: %s\n", Pa_GetErrorText(err));
  }


}