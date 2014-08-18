#include <iostream>
#include <cstring>
#include <cstdlib>
#include "libretro.h"
#include "file.h"
#include "jagbios.h"
#include "jagbios2.h"
#include "jaguar.h"
#include "dac.h"
#include "dsp.h"
#include "joystick.h"
#include "log.h"
#include "memory.h"
#include "settings.h"
#include "tom.h"

#define STARTWIDTH 320
#define STARTHEIGHT 240
#define MAXWIDTH 1400
#define MAXHEIGHT 576
#define FNTSC 60
#define FPAL 50
#define SAMPLERATE 48000
#define BUFPAL  (SAMPLERATE/FPAL*2)
#define BUFNTSC (SAMPLERATE/FNTSC*2)
#define BUFMAX 2048

static bool failed_init;
int videoWidth, videoHeight;
uint32_t *videoBuffer = NULL;
int game_width;
int game_height;

static retro_video_refresh_t video_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_environment_t environ_cb;
static retro_audio_sample_batch_t audio_batch_cb;

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

static struct retro_system_av_info g_av_info;

int doom_res_hack=0; // Doom Hack to double pixel if pwidth==8 (163*2)
int log_enabled=0;

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;

   struct retro_variable variables[] = {
      {
         "virtualjaguar_usefastblitter",
         "Fast Blitter; enabled|disabled",
      },
      {
         "virtualjaguar_doom_res_hack",
         "Doom Hack; disabled|enabled",
      },
      {
         "virtualjaguar_pal (Restart)",
         "Pal; disabled|enabled",
      },
      {
         "virtualjaguar_bios",
         "Bios; disabled|enabled",
      },
      {
         "virtualjaguar_audio",
         "Audio; enabled|disabled",
      },
      {
         "virtualjaguar_gpu",
         "Gpu; enabled|disabled",
      },
      {
         "virtualjaguar_dsp",
         "Dsp; enabled|disabled",
      },
      {
         "virtualjaguar_log",
         "Log; disabled|enabled",
      },
      { NULL, NULL },
   };

   cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
}

static void check_variables(void)
{
   struct retro_variable var;
   var.key = "virtualjaguar_usefastblitter";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         vjs.useFastBlitter=1;
      if (strcmp(var.value, "disabled") == 0)
         vjs.useFastBlitter=0;
   }
   else
      vjs.useFastBlitter=1;

   var.key = "virtualjaguar_doom_res_hack";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         doom_res_hack=1;
      if (strcmp(var.value, "disabled") == 0)
         doom_res_hack=0;
   }
   else
      doom_res_hack=0;

   var.key = "virtualjaguar_pal";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         vjs.hardwareTypeNTSC=0;
      if (strcmp(var.value, "disabled") == 0)
         vjs.hardwareTypeNTSC=1;
   }
   else
      vjs.hardwareTypeNTSC=1;

   var.key = "virtualjaguar_bios";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         vjs.useJaguarBIOS = true;
      if (strcmp(var.value, "disabled") == 0)
         vjs.useJaguarBIOS = false;
   }
   else
      vjs.useJaguarBIOS = false;

   var.key = "virtualjaguar_gpu";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         vjs.GPUEnabled = true;
      if (strcmp(var.value, "disabled") == 0)
         vjs.GPUEnabled = false;
   }
   else
      vjs.GPUEnabled = true;

   var.key = "virtualjaguar_audio";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         vjs.audioEnabled = true;
      if (strcmp(var.value, "disabled") == 0)
         vjs.audioEnabled = false;
   }
   else
      vjs.audioEnabled = true;

   var.key = "virtualjaguar_dsp";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         vjs.DSPEnabled = true;
      if (strcmp(var.value, "disabled") == 0)
         vjs.DSPEnabled = false;
   }
   else
      vjs.DSPEnabled = true;

   var.key = "virtualjaguar_log";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         log_enabled = 1;
      if (strcmp(var.value, "disabled") == 0)
         log_enabled = 0;
   }
   else
      log_enabled = 0;
} 
  
static void update_input(void)
{
   if (!input_poll_cb)
      return;

   input_poll_cb();

   input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP) ? joypad0Buttons[BUTTON_U] = 0xff : joypad0Buttons[BUTTON_U] = 0x00;
   input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN) ? joypad0Buttons[BUTTON_D] = 0xff : joypad0Buttons[BUTTON_D] = 0x00;
   input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT) ? joypad0Buttons[BUTTON_L] = 0xff : joypad0Buttons[BUTTON_L] = 0x00;
   input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT) ? joypad0Buttons[BUTTON_R] = 0xff : joypad0Buttons[BUTTON_R] = 0x00;
   input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A) ? joypad0Buttons[BUTTON_A] = 0xff : joypad0Buttons[BUTTON_A] = 0x00;
   input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B) ? joypad0Buttons[BUTTON_B] = 0xff : joypad0Buttons[BUTTON_B] = 0x00;
   input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y) ? joypad0Buttons[BUTTON_C] = 0xff : joypad0Buttons[BUTTON_C] = 0x00;
   input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START) ? joypad0Buttons[BUTTON_PAUSE] = 0xff : joypad0Buttons[BUTTON_PAUSE] = 0x00;
   input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT) ? joypad0Buttons[BUTTON_OPTION] = 0xff : joypad0Buttons[BUTTON_OPTION] = 0x00;

// others   
 input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X) ? joypad0Buttons[BUTTON_0] = 0xff : joypad0Buttons[BUTTON_0] = 0x00;
 input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R) ? joypad0Buttons[BUTTON_d] = 0xff : joypad0Buttons[BUTTON_d] = 0x00;
 input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L) ? joypad0Buttons[BUTTON_s] = 0xff : joypad0Buttons[BUTTON_s] = 0x00;
 input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2) ? joypad0Buttons[BUTTON_1] = 0xff : joypad0Buttons[BUTTON_1] = 0x00;
 input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2) ? joypad0Buttons[BUTTON_2] = 0xff : joypad0Buttons[BUTTON_2] = 0x00;
 input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3) ? joypad0Buttons[BUTTON_3] = 0xff : joypad0Buttons[BUTTON_3] = 0x00;
 input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3) ? joypad0Buttons[BUTTON_3] = 0xff : joypad0Buttons[BUTTON_3] = 0x00;

 input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0,RETROK_KP0) ? joypad0Buttons[BUTTON_0] = 0xff : joypad0Buttons[BUTTON_0] = 0x00;
 input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0,RETROK_KP1) ? joypad0Buttons[BUTTON_1] = 0xff : joypad0Buttons[BUTTON_1] = 0x00;
 input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0,RETROK_KP2) ? joypad0Buttons[BUTTON_2] = 0xff : joypad0Buttons[BUTTON_2] = 0x00;
 input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0,RETROK_KP3) ? joypad0Buttons[BUTTON_3] = 0xff : joypad0Buttons[BUTTON_3] = 0x00;
 input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0,RETROK_KP4) ? joypad0Buttons[BUTTON_4] = 0xff : joypad0Buttons[BUTTON_4] = 0x00;
 input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0,RETROK_KP5) ? joypad0Buttons[BUTTON_5] = 0xff : joypad0Buttons[BUTTON_5] = 0x00;
 input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0,RETROK_KP6) ? joypad0Buttons[BUTTON_6] = 0xff : joypad0Buttons[BUTTON_6] = 0x00;
 input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0,RETROK_KP7) ? joypad0Buttons[BUTTON_7] = 0xff : joypad0Buttons[BUTTON_7] = 0x00;
 input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0,RETROK_KP8) ? joypad0Buttons[BUTTON_8] = 0xff : joypad0Buttons[BUTTON_8] = 0x00;
 input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0,RETROK_KP9) ? joypad0Buttons[BUTTON_9] = 0xff : joypad0Buttons[BUTTON_9] = 0x00;


}

/************************************
 * libretro implementation
 ************************************/


void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name = "Virtual Jaguar";
   info->library_version = "v2.1.0+";
   info->need_fullpath = false;
   info->valid_extensions = "j64|jag|bin|rom";
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   float maspect=(float)game_width/(float)game_height;
   printf("start res:%dx%d %f\n",game_width,game_height,maspect);

   memset(info, 0, sizeof(*info));
   info->timing.fps            = FNTSC;
   info->timing.sample_rate    = SAMPLERATE;
   info->geometry.base_width   = game_width;
   info->geometry.base_height  = game_height;
   info->geometry.max_width    = MAXWIDTH;
   info->geometry.max_height   = MAXHEIGHT;
   info->geometry.aspect_ratio = maspect;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   (void)port;
   (void)device;
}

size_t retro_serialize_size(void)
{
   return 0;
}

bool retro_serialize(void *data, size_t size)
{
   return false;
}

bool retro_unserialize(const void *data, size_t size)
{
   return false;
}

void retro_cheat_reset(void)
{}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   (void)index;
   (void)enabled;
   (void)code;
}

bool retro_load_game(const struct retro_game_info *info)
{
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      fprintf(stderr, "Pixel format XRGB8888 not supported by platform, cannot use.\n");
      return false;
   }

   const char *full_path;

   if(failed_init)
      return false;

   full_path = info->path;

   vjs.renderType = 0;
   check_variables();

	if (log_enabled)
	{
		bool success = (bool)LogInit("./virtualjaguar.log");	// Init logfile

		if (!success)
			printf("Failed to open virtualjaguar.log for writing!\n");
	}

   //strcpy(vjs.EEPROMPath, "/path/to/eeproms/");   // battery saves
   JaguarInit();                                             // set up hardware
   memcpy(jagMemSpace + 0xE00000, (vjs.biosType == BT_K_SERIES ? jaguarBootROM : jaguarBootROM2), 0x20000); // Use the stock BIOS

   JaguarSetScreenPitch(videoWidth);
   JaguarSetScreenBuffer(videoBuffer);
   //Init video
   for (int i = 0; i < videoWidth * videoHeight; ++i)
      videoBuffer[i] = 0xFF00FFFF;

   SET32(jaguarMainRAM, 0, 0x00200000);                      // set up stack
   JaguarLoadFile((char *)full_path);                // load rom
   JaguarReset();

   return true;
}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
{
   (void)game_type;
   (void)info;
   (void)num_info;
   return false;
}

void retro_unload_game(void)
{
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC;
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void *retro_get_memory_data(unsigned id)
{
   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
   return 0;
}

void retro_init(void)
{
   unsigned level = 18;

   videoWidth = MAXWIDTH;
   videoHeight = MAXHEIGHT;
   videoBuffer = (uint32_t *)calloc(sizeof(uint32_t), MAXWIDTH * MAXHEIGHT);
   sampleBuffer = (uint16_t *)malloc(BUFMAX * sizeof(uint16_t)); //found in dac.h
   memset(sampleBuffer, 0, BUFMAX * sizeof(uint16_t));

   game_width = STARTWIDTH;
   game_height = STARTHEIGHT;

   environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);
}

void retro_deinit(void)
{
   JaguarDone();
   if (log_enabled)LogDone();									// Close logfile
   free(videoBuffer);
   free(sampleBuffer); //found in dac.h
}

void retro_reset(void)
{
   JaguarReset();
}

void retro_run(void)
{
   bool updated = false;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      check_variables();

   update_input();

   JaguarExecuteNew();

	static int lastw=STARTWIDTH;

	if(lastw!=TOMGetVideoModeWidth()){
		retro_get_system_av_info(&g_av_info);

		printf("width change:%d-" ,lastw);
		lastw=TOMGetVideoModeWidth();
		printf(">%d\n" ,lastw);

	    game_width = TOMGetVideoModeWidth();
	    game_height = TOMGetVideoModeHeight();
		tomWidth = game_width; tomHeight = game_height;

		printf("new res:%dx%d %f\n",game_width,game_height,(float)game_width/game_height);

		bool ret;
		ret = environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &g_av_info.geometry);
	}

   SDLSoundCallback(NULL,sampleBuffer, vjs.hardwareTypeNTSC==1?BUFNTSC:BUFPAL);

   video_cb(videoBuffer, game_width, game_height, MAXWIDTH << 2);
   audio_batch_cb((int16_t *)sampleBuffer, vjs.hardwareTypeNTSC==1?BUFNTSC/2:BUFPAL/2);
}
