#include <cerrno>
#include <cmath>
#include <fstream>
#include <iostream>
#include <thread>
#include <alsa/asoundlib.h>

//Input Audio File Directory
#define AUDIO_FILE_DIR "/home/user/Documents/ExampleAudio.raw"

//Audio Playback Device. Write "default" to use the system default playback device.
#define AUDIO_DEV "plughw:0,0"

//Buffers' Size in raw 16bit samples
#define BUFFER_SIZE 65536

//Audio FX Parameter: delay time in number of samples.
//88 samples is aprox. 2 ms at 44100 Hz sample rate
#define DSP_N_DELAY 88

//Number of delay feedback loops. Set to 0 for no delay feedback.
#define DSP_N_FEEDBACK_LOOPS 10

//Enable/Disable alternating feedback polarity.
//Comment it to disable. Uncomment it to enable.
#define DSP_FEEDBACK_POL_ALTERNATE

//Feedback Cycle Signal Divider Progression.
//If defined, divider increases by one, as described in the following progression: signal + delay_1/2 + delay_2/3 + delay_3/4 ...
//Else, divider increases exponentially, as described in the following progression: signal + delay_1/2 + delay_2/4 + delay_3/8 ...
#define DSP_CYCLE_DIV_INC_ONE

//Buffers size in bytes
#define BUFFER_SIZE_BYTES (2*BUFFER_SIZE)

//Number of channel samples on a buffer
#define BUFFER_SIZE_PER_CHANNEL (BUFFER_SIZE/2)

//Number of delay cycles (first delay + number of feedback loops)
#define DSP_DELAY_N_CYCLES (DSP_N_FEEDBACK_LOOPS + 1)

std::thread loadthread;
std::thread playthread;

std::fstream audio_file;
unsigned int audio_file_size = 0;
unsigned int audio_file_pos = 0;

snd_pcm_t *audio_dev;
snd_pcm_uframes_t n_frames;
unsigned int audio_buffer_size = 0;
unsigned int buffer_div = 1;
short **pp_startpoint = NULL;

//Static buffers:
short *buffer_input_0 = NULL;
short *buffer_input_1 = NULL;
short *buffer_output_0 = NULL;
short *buffer_output_1 = NULL;
short *buffer_output_2 = NULL;
short *buffer_output_3 = NULL;
short *buffer_dsp = NULL;

unsigned int curr_buf_cycle = 0;

//Dynamic buffers:
short *curr_in = NULL;
short *prev_in = NULL;
short *load_out = NULL;
short *play_out = NULL;

int n_sample = 0;

bool stop = false;

void update_buf_cycle(void)
{
  switch(curr_buf_cycle)
  {
    case 3:
    curr_buf_cycle = 0;
    break;
      
    default:
    curr_buf_cycle++;
    break;
  }
  
  return;
}

void buffer_remap(void)
{
  switch(curr_buf_cycle)
  {
    case 0:
    curr_in = buffer_input_0;
    prev_in = buffer_input_1;
    load_out = buffer_output_0;
    play_out = buffer_output_2;
    break;
    
    case 1:
    curr_in = buffer_input_1;
    prev_in = buffer_input_0;
    load_out = buffer_output_1;
    play_out = buffer_output_3;
    break;
    
    case 2:
    curr_in = buffer_input_0;
    prev_in = buffer_input_1;
    load_out = buffer_output_2;
    play_out = buffer_output_0;
    break;
    
    case 3:
    curr_in = buffer_input_1;
    prev_in = buffer_input_0;
    load_out = buffer_output_3;
    play_out = buffer_output_1;
    break;
  }
  
  return;
}

void buffer_load(void)
{
  if(audio_file_pos >= audio_file_size)
  {
    stop = true;
    return;
  }
  
  audio_file.seekg(audio_file_pos);
  audio_file.read((char*) curr_in, BUFFER_SIZE_BYTES);
  audio_file_pos += BUFFER_SIZE_BYTES;
  
  return;
}

void buffer_play(void)
{
  unsigned int n_div = 0;
  int n_return;
  
  while(n_div < buffer_div)
  {
    n_return = snd_pcm_writei(audio_dev, pp_startpoint[n_div], n_frames);
    n_div++;
  }
  
  return;
}

void load_delay(void)
{
  buffer_dsp[2*n_sample] = 0;
  buffer_dsp[2*n_sample + 1] = 0;
  
  int n_cycle;
  int n_delay;
  int cycle_div;
  int pol;
  
  n_cycle = 1;
  while(n_cycle <= DSP_DELAY_N_CYCLES)
  {
#ifdef DSP_FEEDBACK_POL_ALTERNATE
    if(n_cycle%2) pol = -1;
    else pol = 1;
#else
    pol = 1;
#endif

#ifdef DSP_CYCLE_DIV_INC_ONE
    cycle_div = n_cycle + 1;
#else
    cycle_div = roundf(powf(2, n_cycle));
#endif

    n_delay = n_cycle*DSP_N_DELAY;
    if(n_sample < n_delay)
    {
      buffer_dsp[2*n_sample] += pol*(prev_in[BUFFER_SIZE - 2*(n_delay - n_sample)])/cycle_div;
      buffer_dsp[2*n_sample + 1] += pol*(prev_in[BUFFER_SIZE - 2*(n_delay - n_sample) + 1])/cycle_div;
    }
    else
    {
      buffer_dsp[2*n_sample] += pol*(curr_in[2*(n_sample - n_delay)])/cycle_div;
      buffer_dsp[2*n_sample + 1] += pol*(curr_in[2*(n_sample - n_delay) + 1])/cycle_div;
    }
    
    n_cycle++;
  }
  
  return;
}

void run_dsp(void)
{
  n_sample = 0;
  while(n_sample < BUFFER_SIZE_PER_CHANNEL)
  {
    load_delay();
    load_out[2*n_sample] = ((curr_in[2*n_sample]) + (buffer_dsp[2*n_sample]))/2;
    load_out[2*n_sample + 1] = ((curr_in[2*n_sample + 1]) + (buffer_dsp[2*n_sample + 1]))/2;
    
    n_sample++;
  }
  
  return;
}

void load_startpoints(void)
{
  pp_startpoint[0] = play_out;
  unsigned int n_div = 1;
  while(n_div < buffer_div)
  {
    pp_startpoint[n_div] = &play_out[n_div*audio_buffer_size];
    n_div++;
  }
  
  return;
}

void loadthread_proc(void)
{
  buffer_load();
  run_dsp();
  update_buf_cycle();
  return;
}

void playthread_proc(void)
{
  load_startpoints();
  buffer_play();
  return;
}

void buffer_preload(void)
{
  curr_buf_cycle = 0;
  buffer_remap();
  
  buffer_load();
  run_dsp();
  update_buf_cycle();
  buffer_remap();
  
  buffer_load();
  run_dsp();
  update_buf_cycle();
  buffer_remap();

  return;
}

void playback(void)
{
  buffer_preload();
  while(!stop)
  {
    playthread = std::thread(playthread_proc);
    loadthread = std::thread(loadthread_proc);
    loadthread.join();
    playthread.join();
    buffer_remap();
  }
  
  return;
}

void buffer_malloc(void)
{
  audio_buffer_size = 2*n_frames;
  if(audio_buffer_size < BUFFER_SIZE) buffer_div = BUFFER_SIZE/audio_buffer_size;
  else buffer_div = 1;
  
  pp_startpoint = (short**) malloc(buffer_div*sizeof(short*));
  
  buffer_input_0 = (short*) malloc(BUFFER_SIZE_BYTES);
  buffer_input_1 = (short*) malloc(BUFFER_SIZE_BYTES);
  buffer_output_0 = (short*) malloc(BUFFER_SIZE_BYTES);
  buffer_output_1 = (short*) malloc(BUFFER_SIZE_BYTES);
  buffer_output_2 = (short*) malloc(BUFFER_SIZE_BYTES);
  buffer_output_3 = (short*) malloc(BUFFER_SIZE_BYTES);
  buffer_dsp = (short*) malloc(BUFFER_SIZE_BYTES);
  
  memset(buffer_input_0, 0, BUFFER_SIZE_BYTES);
  memset(buffer_input_1, 0, BUFFER_SIZE_BYTES);
  memset(buffer_output_0, 0, BUFFER_SIZE_BYTES);
  memset(buffer_output_1, 0, BUFFER_SIZE_BYTES);
  memset(buffer_output_2, 0, BUFFER_SIZE_BYTES);
  memset(buffer_output_3, 0, BUFFER_SIZE_BYTES);
  memset(buffer_dsp, 0, BUFFER_SIZE_BYTES);
  
  return;
}

void buffer_free(void)
{
  free(pp_startpoint);
  
  free(buffer_input_0);
  free(buffer_input_1);
  free(buffer_output_0);
  free(buffer_output_1);
  free(buffer_output_2);
  free(buffer_output_3);
  free(buffer_dsp);
  
  return;
}

bool open_audio_file(void)
{
  audio_file.open(AUDIO_FILE_DIR, std::ios_base::in);
  if(audio_file.is_open())
  {
    audio_file.seekg(0, audio_file.end);
    audio_file_size = audio_file.tellg();
    audio_file_pos = 0;
    audio_file.seekg(audio_file_pos);
    return true;
  }
  
  return false;
}

bool audio_hw_init(void)
{
  int n_return;
  snd_pcm_hw_params_t *hw_params;
  
  n_return = snd_pcm_open(&audio_dev, AUDIO_DEV, SND_PCM_STREAM_PLAYBACK, 0);
  if(n_return < 0)
  {
    std::cout << "Error opening audio device\n";
    return false;
  }
  
  snd_pcm_hw_params_malloc(&hw_params);
  snd_pcm_hw_params_any(audio_dev, hw_params);
  
  n_return = snd_pcm_hw_params_set_access(audio_dev, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
  if(n_return < 0)
  {
    std::cout << "Error setting access to read/write interleaved\n";
    return false;
  }
  
  n_return = snd_pcm_hw_params_set_format(audio_dev, hw_params, SND_PCM_FORMAT_S16_LE);
  if(n_return < 0)
  {
    std::cout << "Error setting format to signed 16bit little-endian\n";
    return false;
  }
  
  n_return = snd_pcm_hw_params_set_channels(audio_dev, hw_params, 2);
  if(n_return < 0)
  {
    std::cout << "Error setting channels to stereo\n";
    return false;
  }
  
  unsigned int sample_rate = 44100;
  n_return = snd_pcm_hw_params_set_rate_near(audio_dev, hw_params, &sample_rate, 0);
  if(n_return < 0 || sample_rate < 44100)
  {
    std::cout << "Could not set sample rate to 44100 Hz\nAttempting to set sample rate to 48000 Hz\n";
    sample_rate = 48000;
    n_return = snd_pcm_hw_params_set_rate_near(audio_dev, hw_params, &sample_rate, 0);
    if(n_return < 0 || sample_rate < 48000)
    {
      std::cout << "Error setting sample rate\n";
      return false;
    }
  }
  
  n_return = snd_pcm_hw_params(audio_dev, hw_params);
  if(n_return < 0)
  {
    std::cout << "Error setting audio hardware parameters\n";
    return false;
  }
  
  snd_pcm_hw_params_get_period_size(hw_params, &n_frames, 0);
  snd_pcm_hw_params_free(hw_params);
  return true;
}

int main(int argc, char **argv)
{
  if(!audio_hw_init())
  {
    std::cout << "Error code: " << errno << "\nTerminated\n";
    return 0;
  }
  std::cout << "Audio hardware initialized\n";
  
  if(!open_audio_file())
  {
    std::cout << "Error opening audio file\nError code: " << errno << "\nTerminated\n";
    return 0;
  }
  std::cout << "Audio file is open\n";
  
  buffer_malloc();
  
  std::cout << "Playback started\n";
  playback();
  std::cout << "Playback finished\n";
  
  audio_file.close();
  snd_pcm_drain(audio_dev);
  snd_pcm_close(audio_dev);
  buffer_free();
  std::cout << "Terminated\n";
  
  return 0;
}
