/*
 * Heart of The Alien: Music player (cd and mp3)
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
#include <SDL.h>
#include <SDL_mixer.h>
#include "debug.h"
#include "music.h"

///
extern int nosound_flag;
/////

static SDL_CD *sdl_cd;
static Mix_Music *current_track;

void stop_music_cd()
{
	SDL_CDStop(sdl_cd);
}

void stop_music_mp3()
{
	if (current_track != NULL)
	{
		Mix_FreeMusic(current_track);
		current_track = NULL;
	}
}

void stop_music()
{
	if (nosound_flag)
	{
		return;
	}

	if (get_iso_toggle())
	{
		stop_music_mp3();	
	}
	else
	{
		stop_music_cd();
	}
}

void play_music_track_cd(int track, int loop)
{
	if (CD_INDRIVE(SDL_CDStatus(sdl_cd)))
	{
		printf("SDL_CDPlay returned %d\n", SDL_CDPlayTracks(sdl_cd, track, 0, 1, 0));
	}
}

void play_music_track_mp3(int track, int loop)
{
	char filename[256];

	stop_music_mp3();

	sprintf(filename, "heartalien/Heart Of The Alien (U) %02d.mp3", track + 1);
	printf("playing mp3 %s\n", filename);

	current_track = Mix_LoadMUS(filename);
	Mix_PlayMusic(current_track, loop);
}

void play_music_track(int track, int loop)
{
	if (nosound_flag)
	{
		return;
	}

	stop_music();

	if (get_iso_toggle())
	{
		play_music_track_mp3(track, loop);
	}
	else
	{
		play_music_track_cd(track, loop);
	}
}

void music_update()
{
}

void music_init()
{
	if (get_iso_toggle() == 0)
	{
		/* initialize SDL CDROM, for playing audio tracks */
		sdl_cd = SDL_CDOpen(0);
	}
}
