/*
 * Heart of The Alien: Game loop and main
 * Copyright (c) 2004 Gil Megidish
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <assert.h>
#include <SDL.h>
#include <SDL_mixer.h>

#include "vm.h"
#include "rooms.h"
#include "debug.h"
#include "sound.h"
#include "music.h"
#include "common.h"
#include "cd_iso.h"
#include "decode.h"
#include "render.h"
#include "screen.h"
#include "sprites.h"
#include "animation.h"

#ifdef WIN32
#include "getopt.h"
#endif

static char *VERSION = "1.1.0";

static char *QUICKSAVE_FILENAME = "quicksave";
static char *RECORDED_KEYS_FILENAME = "recorded-keys";

///////
extern int script_ptr;
extern int first_sprite, last_sprite, sprite_count;
extern int pc;

extern unsigned char memory[];
extern unsigned short variables[];
extern unsigned short auxvars[];

int quit = 0;
int next_script;
int current_backdrop;
int current_room;

int speed_throttle = 0;
int fullscreen = 0;
int palette_changed = 0;
int current_palette = 0;
SDL_Color palette[256];

int iso_flag = 0;
int double_flag = 0;
int nosound_flag = 0;
int debug_flag = 0;
int test_flag = 0;
int record_flag = 0;
int replay_flag = 0;
int fullscreen_flag = 0;
char game2bin[409600];

short task_pc[64];
short new_task_pc[64];
short enabled_tasks[64];
short new_enabled_tasks[64];

int key_up, key_down, key_left, key_right, key_a, key_b, key_c, key_select;
int key_reset_record;

FILE *record_fp = 0;

SDL_Surface *screen;

void set_palette_rgb12(unsigned char *rgb12)
{
	int i;

	for (i=0; i<256; i++)
	{
		int c, r, g, b;

		c = (rgb12[i*2] << 8) | rgb12[i*2+1];
		r = (c & 0xf) << 4;
		g = ((c >> 4) & 0xf) << 4;
		b = ((c >> 8) & 0xf) << 4;

		palette[i].r = r;
		palette[i].g = g;
		palette[i].b = b;
	}

	palette_changed = 1;
}

void set_palette(int which)
{
	unsigned char rgb12[16*2];
	
	memcpy(rgb12, game2bin + 0x5cb8 + (which * 16 * 2), sizeof(rgb12));

	current_palette = which;
	set_palette_rgb12(rgb12);

	palette[255].r = 255;
	palette[255].g = 0;
	palette[255].b = 255;
}

static int load_room(int index)
{
	char filename[16];

	strcpy(filename, "ROOMS0.BIN");
	filename[5] = (index + '0');

	LOG(("loading %s\n", filename));
	if (read_file(filename, memory + 0xf900) < 0)
	{
		panic("load_room failed");
	}

	script_ptr = get_long(0xf900);
	LOG(("script ptr %x\n", script_ptr));

	sound_flush_cache();

	return 0;
}

void toggle_fullscreen()
{
	fullscreen ^= 1;

	SDL_FreeSurface(screen);
	screen = 0;

	if (fullscreen == 0)
	{
		screen = SDL_SetVideoMode(304*(1+double_flag), 192*(1+double_flag), 8, SDL_SWSURFACE);
		SDL_SetColors(screen, palette, 0, 256);
		SDL_ShowCursor(1);
	}
	else
	{
		screen = SDL_SetVideoMode(320, 200, 8, SDL_SWSURFACE|SDL_HWSURFACE|SDL_FULLSCREEN);
		SDL_SetColors(screen, palette, 0, 256);
		SDL_ShowCursor(0);
	}
}

static void atexit_callback(void)
{
	stop_music();
	SDL_Quit();
}

static int initialize()
{
	if (read_file("GAME2.BIN", game2bin) < 0)
	{
		panic("can't read GAME2.BIN file");
	}

	if (nosound_flag == 0)
	{
		if (Mix_OpenAudio(44100, AUDIO_S16, 2, 4096) < 0) 
		{
			panic("Mix_OpenAudio failed\n");
		}

		music_init();
		sound_init();
	}

	SDL_Init(SDL_INIT_VIDEO|SDL_INIT_CDROM|SDL_INIT_AUDIO);
	atexit(atexit_callback);

	screen = SDL_SetVideoMode(304*(1+double_flag), 192*(1+double_flag), 8, SDL_SWSURFACE);
	if (screen == NULL) 
	{
	        fprintf(stderr, "Unable to set 304x192 video: %s\n", SDL_GetError());
        	exit(1);
	}

	SDL_WM_SetCaption("Heart of The Alien Redux", 0);

	screen_init();
	
	pc = 0;
	memset(variables, '\0', 256*sizeof(variables[0]));
	variables[227] = 1;

	if (fullscreen_flag)
	{
		toggle_fullscreen();
	}

	return 0;
}

void load_room_screen(int room, int index)
{
	int i;
	unsigned char *pixels;
	unsigned char buffer[29184];

	LOG(("loading room screen %d from room file %d\n", index - 1, room));

	unpack_room(buffer, index - 1);           

	/*
	memset(palette, '\0', sizeof(palette));
	SDL_SetColors(screen, palette, 0, 256);	
	*/

	pixels = get_screen_ptr(0);
	for (i=0; i<304*192/2; i++)
	{
		pixels[i*2+0] = buffer[i] >> 4;
		pixels[i*2+1] = buffer[i] & 0xf;
	}

	current_backdrop = index;
}

void read_keys_from_record()
{
	int c = fgetc(record_fp);

	if (c == EOF)
	{
		c = 0;
		LOG(("ERROR: record file ended!\n"));
	}

	key_up = (c >> 7) & 1;
	key_down = (c >> 6) & 1;
	key_left = (c >> 5) & 1;
	key_right = (c >> 4) & 1;
	key_a = (c >> 3) & 1;
	key_b = (c >> 2) & 1;
	key_c = (c >> 1) & 1;
	key_select = (c >> 0) & 1;
}

void write_keys_to_record()
{
	int c;

	if (key_reset_record == 0)
	{	
		c = (key_up << 7) | (key_down << 6);
		c = c | (key_left << 5) | (key_right << 4);
		c = c | (key_a << 3) | (key_b << 2);
		c = c | (key_c << 1) | key_select;
	}
	else
	{
		c = 0xff;
	}

	fputc(c, record_fp);
}

void update_keys()
{
	short flags;

	/* b6c4 */
	toggle_aux(0); // fixme: remove to somewhere else
	set_variable(253, 0);
	set_variable(252, 0);
	set_variable(229, 0);
	set_variable(251, 0);

	flags = 0;

	if (key_right)
	{
		set_variable(252, 1);
		flags |= 1;
	}
	else if (key_left)
	{
		set_variable(252, -1);
		flags |= 2;
	}
	else if (key_down)
	{
		set_variable(251, 1);
		set_variable(229, 1);
		flags |= 4;
	}

	if (key_up)
	{
		set_variable(229, -1);
		flags |= 8;
	}

	if (key_c)
	{
		set_variable(251, -1);
		flags |= 8;
		/* some if  here! */
	}

	set_variable(253, flags);

	/* b748 */
	set_variable(250, 0);
	set_variable(254, get_variable(253));

	if (key_b)
	{
		set_variable(254, get_variable(254) | 0x40);
	}
	else if (key_a)
	{
		set_variable(250, 1);
		set_variable(254, get_variable(254) | 0x80);
	}
}

void quickload()
{
	int i, j;
	FILE *fp;

	fp = fopen(QUICKSAVE_FILENAME, "rb");
	if (fp == NULL)
	{
		perror("failed to load 'quicksave' file\n");
	}

	current_room = fgetc(fp);
	current_backdrop = fgetc(fp);
	current_palette = fgetc(fp);

	load_room(current_room);
	load_room_screen(0, current_backdrop);
	set_palette(current_palette);

	/* must be ran out of thread loop, so no active thread */
	toggle_aux(0);
	for (i=0; i<256; i++)
	{
		set_variable(i, fgetw(fp));
	}

	toggle_aux(1);
	for (j=0; j<MAX_TASKS; j++)
	{
		set_aux_bank(j);
		for (i=0; i<32; i++)	
		{
			set_variable(i, fgetw(fp));
		}
	}

	toggle_aux(0);

	for (i=0; i<MAX_TASKS; i++)
	{
		task_pc[i] = fgetw(fp);
	}

	for (i=0; i<MAX_TASKS; i++)
	{
		new_task_pc[i] = fgetw(fp);
	}
		
	for (i=0; i<MAX_TASKS; i++)
	{
		enabled_tasks[i] = fgetw(fp);
	}

	for (i=0; i<MAX_TASKS; i++)
	{
		new_enabled_tasks[i] = fgetw(fp);
	}

	for (i=0; i<MAX_SPRITES; i++)
	{
		sprite_t *spr = &sprites[i];

		spr->index = fgetc(fp);
		spr->u1 = fgetc(fp);
		spr->next = fgetc(fp);
		spr->frame = fgetc(fp);
		spr->x = fgetw(fp);
		spr->y = fgetw(fp);
		spr->u2 = fgetc(fp);
		spr->u3 = fgetc(fp);
		spr->w4 = fgetw(fp);
		spr->w5 = fgetw(fp);
		spr->u6 = fgetc(fp);
		spr->u7 = fgetc(fp);
	}

	first_sprite = fgetc(fp);
 	sprite_count = fgetc(fp);
	last_sprite = fgetc(fp);

	fclose(fp);
}

void quicksave()
{
	int i, j;
	FILE *fp;

	fp = fopen(QUICKSAVE_FILENAME, "wb");
	if (fp == NULL)
	{
		perror("failed to create 'quicksave' file\n");
	}

	fputc(current_room, fp);
	fputc(current_backdrop, fp);
	fputc(current_palette, fp);

	/* must be ran out of thread loop, so no active thread */
	toggle_aux(0);
	for (i=0; i<256; i++)
	{
		fputw(get_variable(i), fp);
	}

	toggle_aux(1);
	for (j=0; j<MAX_TASKS; j++)
	{
		set_aux_bank(j);
		for (i=0; i<32; i++)	
		{
			fputw(get_variable(i), fp);
		}
	}

	toggle_aux(0);

	for (i=0; i<MAX_TASKS; i++)
	{
		fputw(task_pc[i], fp);
	}

	for (i=0; i<MAX_TASKS; i++)
	{
		fputw(new_task_pc[i], fp);
	}
		
	for (i=0; i<MAX_TASKS; i++)
	{
		fputw(enabled_tasks[i], fp);
	}

	for (i=0; i<MAX_TASKS; i++)
	{
		fputw(new_enabled_tasks[i], fp);
	}

	for (i=0; i<MAX_SPRITES; i++)
	{
		sprite_t *spr = &sprites[i];

		fputc(spr->index, fp);
		fputc(spr->u1, fp);
		fputc(spr->next, fp);
		fputc(spr->frame, fp);
		fputw(spr->x, fp);
		fputw(spr->y, fp);
		fputc(spr->u2, fp);
		fputc(spr->u3, fp);
		fputw(spr->w4, fp);
		fputw(spr->w5, fp);
		fputc(spr->u6, fp);
		fputc(spr->u7, fp);
	}

	fputc(first_sprite, fp);
 	fputc(sprite_count, fp);
	fputc(last_sprite, fp);

	fclose(fp);
}

void check_events()
{
	SDL_Event event;

	SDL_PollEvent(&event);
        switch (event.type) 
	{
		case SDL_KEYUP:
		switch(event.key.keysym.sym)
		{
			case SDLK_RIGHT:
			key_right = 0;
			break;

			case SDLK_LEFT:
			key_left = 0;
			break;

			case SDLK_UP:
			key_up = 0;
			break;

			case SDLK_DOWN:
			key_down = 0;
			break;

			case SDLK_z:
			key_a = 0;
			break;

			case SDLK_x:
			key_b = 0;
			break;

			case SDLK_c:
			key_c = 0;
			break;

			case SDLK_q:
			key_a = 0;
			key_reset_record = 0;
			break;

			case SDLK_SPACE:
			speed_throttle = 0;
			break;

			default:
			/* keep -Wall happy */
			break;
		}
		break;

        	case SDL_KEYDOWN:
		switch(event.key.keysym.sym)
		{
			#ifdef ENABLE_DEBUG
			case SDLK_1:
			case SDLK_2:
			case SDLK_3:
			case SDLK_4:
			case SDLK_5:
			case SDLK_6:
			case SDLK_7:
			case SDLK_8:
			case SDLK_9:
			{
				int tmp = event.key.keysym.sym - SDLK_1 + 1;

				if (event.key.keysym.mod & KMOD_SHIFT)
				{
					tmp = tmp + 10;
				}

				sprites[tmp].u1 ^= 0x80;
			}
			break;
			#endif

			case SDLK_ESCAPE:
			quit = 1;
			break;

			case SDLK_RIGHT:
			key_right = 1;
			break;
			
			case SDLK_LEFT:
			key_left = 1;
			break;

			case SDLK_UP:
			key_up = 1;
			break;
			
			case SDLK_DOWN:
			key_down = 1;
			break;

			case SDLK_z:
			key_a = 1;
			break;

			case SDLK_x:
			key_b = 1;
			break;

			case SDLK_c:
			key_c = 1;
			break;

			case SDLK_d:
			debug_flag ^= 1;
			break;

			case SDLK_F5:
			quicksave();
			break;

			case SDLK_F7:
			quickload();
			break;

			case SDLK_RETURN:
			if (event.key.keysym.mod & KMOD_ALT)
			{
				toggle_fullscreen();
			}
			break;

			case SDLK_q:
			key_a = 1;
			key_reset_record = 1;
			break;

			case SDLK_SPACE:
			speed_throttle = 1;
			break;

			default:
			/* keep -Wall happy */
			break;
		}
		break;

		case SDL_QUIT:
		exit(0);
		break;
	}
}

void rest()
{
	int fps;

	fps = 15;
	if (speed_throttle == 1)
	{
		/* 5 times faster */
		fps = fps*5;
	}

	SDL_Delay(1000 / fps);
}

void init_tasks()
{
	int i;

	for (i=0; i<MAX_TASKS; i++)
	{
		task_pc[i] = -1;
		new_task_pc[i] = -1;

		enabled_tasks[i] = 0;
		new_enabled_tasks[i] = 0;
	}

	toggle_aux(0);
	task_pc[0] = 0;
}

#ifdef ENABLE_DEBUG
short old_var[256+64*32];
#endif

void run()
{
	init_tasks();

	quit = 0;
	while (quit == 0)
	{
		int i;

		if (next_script != 0)
		{
			current_room = next_script;
			reset_sprite_list();
			init_tasks();
			LOG(("loading room %d\n", current_room));
			load_room(current_room);
			next_script = 0;
		}

		check_events();

		if (replay_flag)
		{
			read_keys_from_record();
		}

		update_keys();

		if (record_flag)
		{
			write_keys_to_record();
		}
	
		LOG(("*new frame*\n"));

		for (i=0; i<MAX_TASKS; i++)
		{
			int d0;

			/* 70d0 */
			enabled_tasks[i] = new_enabled_tasks[i];

			d0 = new_task_pc[i];
			if (d0 == -1)
			{
				continue;
			}
			
			if (d0 == -2)
			{
				d0 = -1;
			}		

			task_pc[i] = d0;
			new_task_pc[i] = -1;
		}

		/* call b622 */
		for (i=0; i<MAX_TASKS; i++)
		{
			int pc = task_pc[i];
			if (pc != -1 && enabled_tasks[i] == 0)
			{
				toggle_aux(0);
				set_aux_bank(i);
				LOG(("task %d starts at 0x%x\n", i, pc));
				task_pc[i] = decode(i, pc);
				LOG(("task %d ended at 0x%x\n", i, pc));
			}

			if (next_script != 0)
			{
				/* script has been changed */
				break;
			}
		}

		SDL_UpdateRect(screen, 0, 0, 0, 0);
                music_update();

		rest();

		#if 0
		/* uncomment this block to enable printouts of modified
		 * variables (between two frames.)
		 */
		toggle_aux(0);
		for (i=0; i<240; i++)
		{
			if (old_var[i] != get_variable(i))
			{
				printf("global-var %d changed to 0x%02x\n", i, get_variable(i));
				old_var[i] = get_variable(i);
			}
		}

		toggle_aux(1);
		for (j=0; j<32; j++)
		{
			set_aux_bank(j);
			for (i=0; i<64; i++)
			{
				if (old_var[i+256+(j*64)] != get_variable(i))
				{
					printf("tls %d var %d changed to %02x\n", j, i, get_variable(i));
					old_var[i+256+(j*64)] = get_variable(i);
				}
			}
		}

		toggle_aux(0);
		#endif
	}
}

void animation_test()
{
	play_animation("INTRO1.BIN");
	play_animation("INTRO2.BIN");
	play_animation("INTRO3.BIN");
	play_animation("INTRO4.BIN");
	play_animation("MID1.BIN");
	play_animation("MID2.BIN");
	play_animation("END1.BIN");
	play_animation("END2.BIN");
	play_animation("END3.BIN");
	play_animation("END4.BIN");
}

void sprite_test()
{
	int redraw;

	load_room(next_script);

	sprites[0].index = 0;
	sprites[0].frame = 0;

	quit = 0;

	redraw = 1;
	set_palette(0x11);
	SDL_SetColors(screen, palette, 0, 256);
	while (quit == 0)
	{
		int a4;

		a4 = get_long(0xf904) + (sprites[0].index << 2);
		a4 = get_long(a4);

		if (redraw)
		{
			int selected_screen;
			void *background;

			selected_screen = get_selected_screen();
			background = get_screen_ptr(selected_screen);
			memset(background, 0xff, 304*192);

			render_sprite(0);
			render(background);
			SDL_UpdateRect(screen, 0, 0, 0, 0);
			redraw = 0;
			print_sprite(0);
		}

		check_events();
		rest();

		update_keys();

		if (get_variable(252) == 1)
		{
			int i = (sprites[0].frame & 0x7f) + 1;
			sprites[0].frame = (sprites[0].frame & 0x80) | i;
			if (sprites[0].frame > get_byte(a4))
			{
				sprites[0].frame &= 0x80;
			}

			redraw = 1;
		}

		if (get_variable(252) == -1)
		{
			if (sprites[0].frame > 0)
			{
				int i = (sprites[0].frame & 0x7f) - 1;
				redraw = 1;
				sprites[0].frame = (sprites[0].frame & 0x80) | i;
			}
		}

		if (get_variable(229) == 1)
		{
			redraw = 1;
			sprites[0].index++;
			sprites[0].frame &= 0x80;
		}

		if (get_variable(229) == -1)
		{
			if (sprites[0].index > 0)
			{
				redraw = 1;
				sprites[0].index--;
				sprites[0].frame &= 0x80;
			}
		}

		if (get_variable(250))
		{
			/* flip */
			sprites[0].frame |= 0x80;
		}

		set_variable(229, 0);
		set_variable(252, 0);
	}
}

static void help()
{
	printf("Heart of The Alien Redux %s", VERSION);
	puts("USAGE:");
	#ifdef DEBUG_ENABLED
	puts("\t--debug        turn on debugging");
	#endif
	puts("\t--iso          use iso and mp3s (in current directory)");
	puts("\t--double       double size window");
	puts("\t--fullscreen   start in fullscreen");
	puts("\t--room n       start from a different room");
	puts("\t--sprite-test  run sprite test (use with room)");
	puts("\t--intro-test   play all animations");
	puts("\t--record       record keys");
	puts("\t--replay       replay keys");
	puts("\t--help         this help");
}

static struct option options[] =
{
	{"debug", no_argument, 0, 'd'},
	{"room", required_argument, 0, 'r'}, 
	{"sprite-test", no_argument, &test_flag, 1},
	{"intro-test", no_argument, &test_flag, 2},
	{"help", no_argument, 0, 'h'},
	{"no-sound", no_argument, &nosound_flag, 1},
	{"fullscreen", no_argument, &fullscreen_flag, 1},
	{"record", no_argument, &record_flag, 1},
	{"replay", no_argument, &replay_flag, 1},
	{"double", no_argument, &double_flag, 1},
	{"iso", no_argument, 0, 'i'}
};

int main(int argc, char **argv)
{
	int options_index;

	next_script = 1;

	options_index = 0;
	while (1)
	{
		int c = getopt_long(argc, argv, "hdr:2", options, &options_index);
		if (c == -1)
		{
			/* no more options */
			break;
		}            

		switch(c)
		{
			/* won't do a thing if turned on without ENABLE_DEBUG */
			case 'd':
			debug_flag = 1;
			break;

			case 'r':
			next_script = atoi(optarg);
			break;

			case 'h':
			help();
			return 0;

			case '2':
			double_flag = 1;
			break;

			case 'i':
			toggle_use_iso(1);
			break;
		}
	}

	if (replay_flag && record_flag)
	{
		fprintf(stderr, "cant specify both replay and record\n");
		return 1;
	}

	if (replay_flag)
	{
		record_fp = fopen(RECORDED_KEYS_FILENAME, "rb");
	}
	else if (record_flag)
	{
		record_fp = fopen(RECORDED_KEYS_FILENAME, "wb");
	}

	initialize();

	switch(test_flag)
	{
		case 0:
		run();
		break;

		case 1:
		sprite_test();
		break;

		case 2:
		animation_test();
		break;

		default:
		fprintf(stderr, "unknown test_flag %d\n", test_flag);
		return 1;
	}

	if (record_fp != NULL)
	{
		fclose(record_fp);
	}

	return 0;
}