#include <SDL2/SDL.h>
#include <stdio.h>
#include <time.h>

/*
Copyright (c) 2021 Devine Lu Linvega

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE.
*/

#include "uxn.h"

int initapu(Uxn *u, Uint8 id);
void stepapu(Uxn *u);

#define HOR 48
#define VER 32
#define PAD 2
#define RES (HOR * VER * 16)

typedef struct {
	Uint8 reqdraw, bg[RES], fg[RES];
	Uint16 x1, y1, x2, y2;
} Screen;

int WIDTH = 8 * HOR + 8 * PAD * 2;
int HEIGHT = 8 * VER + 8 * PAD * 2;
int FPS = 30, GUIDES = 0, ZOOM = 2;

Uint32 *pixels, theme[4];

Uint8 font[][8] = {
	{0x00, 0x3c, 0x46, 0x4a, 0x52, 0x62, 0x3c, 0x00},
	{0x00, 0x18, 0x28, 0x08, 0x08, 0x08, 0x3e, 0x00},
	{0x00, 0x3c, 0x42, 0x02, 0x3c, 0x40, 0x7e, 0x00},
	{0x00, 0x3c, 0x42, 0x1c, 0x02, 0x42, 0x3c, 0x00},
	{0x00, 0x08, 0x18, 0x28, 0x48, 0x7e, 0x08, 0x00},
	{0x00, 0x7e, 0x40, 0x7c, 0x02, 0x42, 0x3c, 0x00},
	{0x00, 0x3c, 0x40, 0x7c, 0x42, 0x42, 0x3c, 0x00},
	{0x00, 0x7e, 0x02, 0x04, 0x08, 0x10, 0x10, 0x00},
	{0x00, 0x3c, 0x42, 0x3c, 0x42, 0x42, 0x3c, 0x00},
	{0x00, 0x3c, 0x42, 0x42, 0x3e, 0x02, 0x3c, 0x00},
	{0x00, 0x3c, 0x42, 0x42, 0x7e, 0x42, 0x42, 0x00},
	{0x00, 0x7c, 0x42, 0x7c, 0x42, 0x42, 0x7c, 0x00},
	{0x00, 0x3c, 0x42, 0x40, 0x40, 0x42, 0x3c, 0x00},
	{0x00, 0x78, 0x44, 0x42, 0x42, 0x44, 0x78, 0x00},
	{0x00, 0x7e, 0x40, 0x7c, 0x40, 0x40, 0x7e, 0x00},
	{0x00, 0x7e, 0x40, 0x40, 0x7c, 0x40, 0x40, 0x00}};

static SDL_Window *gWindow;
static SDL_Renderer *gRenderer;
static SDL_Texture *gTexture;
SDL_AudioDeviceID audio_id;
static Screen screen;
static Device *devsystem, *devscreen, *devmouse, *devkey, *devctrl;

#pragma mark - Helpers

int
clamp(int val, int min, int max)
{
	return (val >= min) ? (val <= max) ? val : max : min;
}

void
setflag(Uint8 *a, char flag, int b)
{
	if(b)
		*a |= flag;
	else
		*a &= (~flag);
}

int
getflag(Uint8 *a, char flag)
{
	return *a & flag;
}

#pragma mark - Paint

void
paintpixel(Uint8 *dst, Uint16 x, Uint16 y, Uint8 color)
{
	Uint16 row = (y % 8) + ((x / 8 + y / 8 * HOR) * 16), col = 7 - (x % 8);
	if(x >= HOR * 8 || y >= VER * 8 || row > RES - 8)
		return;
	if(color == 0 || color == 2)
		dst[row] &= ~(1UL << col);
	else
		dst[row] |= 1UL << col;
	if(color == 0 || color == 1)
		dst[row + 8] &= ~(1UL << col);
	else
		dst[row + 8] |= 1UL << col;
}

#pragma mark - Helpers

void
clear(Uint32 *dst)
{
	int v, h;
	for(v = 0; v < HEIGHT; v++)
		for(h = 0; h < WIDTH; h++)
			dst[v * WIDTH + h] = theme[0];
}

void
drawpixel(Uint32 *dst, Uint16 x, Uint16 y, Uint8 color)
{
	if(x >= screen.x1 && x <= screen.x2 && y >= screen.x1 && y <= screen.y2)
		dst[y * WIDTH + x] = theme[color];
}

void
drawchr(Uint32 *dst, Uint16 x, Uint16 y, Uint8 *sprite, Uint8 alpha)
{
	Uint8 v, h;
	for(v = 0; v < 8; v++)
		for(h = 0; h < 8; h++) {
			Uint8 ch1 = ((sprite[v] >> h) & 0x1);
			Uint8 ch2 = (((sprite[v + 8] >> h) & 0x1) << 1);
			if(!alpha || (alpha && ch1 + ch2 != 0))
				drawpixel(dst, x + 7 - h, y + v, ch1 + ch2);
		}
}

void
drawicn(Uint32 *dst, Uint16 x, Uint16 y, Uint8 *sprite, Uint8 fg, Uint8 bg)
{
	Uint8 v, h;
	for(v = 0; v < 8; v++)
		for(h = 0; h < 8; h++) {
			Uint8 ch1 = (sprite[v] >> (7 - h)) & 0x1;
			drawpixel(dst, x + h, y + v, ch1 ? fg : bg);
		}
}

#pragma mark - Core

int
error(char *msg, const char *err)
{
	printf("Error %s: %s\n", msg, err);
	return 0;
}

void
loadtheme(Uint8 *addr)
{
	int i;
	for(i = 0; i < 4; ++i) {
		Uint8
			r = (*(addr + i / 2) >> (!(i % 2) << 2)) & 0x0f,
			g = (*(addr + 2 + i / 2) >> (!(i % 2) << 2)) & 0x0f,
			b = (*(addr + 4 + i / 2) >> (!(i % 2) << 2)) & 0x0f;
		theme[i] = (r << 20) + (r << 16) + (g << 12) + (g << 8) + (b << 4) + b;
	}
	screen.reqdraw = 1;
}

void
drawdebugger(Uint32 *dst, Uxn *u)
{
	Uint8 i, x, y, b;
	for(i = 0; i < 0x10; ++i) { /* memory */
		x = ((i % 8) * 3 + 3) * 8, y = screen.x1 + 8 + i / 8 * 8, b = u->wst.dat[i];
		drawicn(dst, x, y, font[(b >> 4) & 0xf], 1 + (u->wst.ptr == i), 0);
		drawicn(dst, x + 8, y, font[b & 0xf], 1 + (u->wst.ptr == i), 0);
	}
	for(x = 0; x < 32; ++x) {
		drawpixel(dst, x, HEIGHT / 2, 2);
		drawpixel(dst, WIDTH - x, HEIGHT / 2, 2);
		drawpixel(dst, WIDTH / 2, HEIGHT - x, 2);
		drawpixel(dst, WIDTH / 2, x, 2);
		drawpixel(dst, WIDTH / 2 - 16 + x, HEIGHT / 2, 2);
		drawpixel(dst, WIDTH / 2, HEIGHT / 2 - 16 + x, 2);
	}
}

void
redraw(Uint32 *dst, Uxn *u)
{
	Uint16 x, y;
	for(y = 0; y < VER; ++y)
		for(x = 0; x < HOR; ++x) {
			Uint16 key = (y * HOR + x) * 16;
			drawchr(dst, (x + PAD) * 8, (y + PAD) * 8, &screen.bg[key], 0);
			drawchr(dst, (x + PAD) * 8, (y + PAD) * 8, &screen.fg[key], 1);
		}
	if(GUIDES)
		drawdebugger(dst, u);
	SDL_UpdateTexture(gTexture, NULL, dst, WIDTH * sizeof(Uint32));
	SDL_RenderClear(gRenderer);
	SDL_RenderCopy(gRenderer, gTexture, NULL, NULL);
	SDL_RenderPresent(gRenderer);
	screen.reqdraw = 0;
}

void
toggledebug(Uxn *u)
{
	GUIDES = !GUIDES;
	redraw(pixels, u);
}

void
togglezoom(Uxn *u)
{
	ZOOM = ZOOM == 3 ? 1 : ZOOM + 1;
	SDL_SetWindowSize(gWindow, WIDTH * ZOOM, HEIGHT * ZOOM);
	redraw(pixels, u);
}

void
quit(void)
{
	free(pixels);
	SDL_DestroyTexture(gTexture);
	gTexture = NULL;
	SDL_DestroyRenderer(gRenderer);
	gRenderer = NULL;
	SDL_DestroyWindow(gWindow);
	gWindow = NULL;
	SDL_Quit();
	exit(0);
}

int
init(void)
{
	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
		return error("Init", SDL_GetError());
	gWindow = SDL_CreateWindow("Uxn", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, WIDTH * ZOOM, HEIGHT * ZOOM, SDL_WINDOW_SHOWN);
	if(gWindow == NULL)
		return error("Window", SDL_GetError());
	gRenderer = SDL_CreateRenderer(gWindow, -1, 0);
	if(gRenderer == NULL)
		return error("Renderer", SDL_GetError());
	gTexture = SDL_CreateTexture(gRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, WIDTH, HEIGHT);
	if(gTexture == NULL)
		return error("Texture", SDL_GetError());
	if(!(pixels = (Uint32 *)malloc(WIDTH * HEIGHT * sizeof(Uint32))))
		return error("Pixels", "Failed to allocate memory");
	clear(pixels);
	SDL_StartTextInput();
	SDL_ShowCursor(SDL_DISABLE);
	screen.x1 = PAD * 8;
	screen.x2 = WIDTH - PAD * 8 - 1;
	screen.y1 = PAD * 8;
	screen.y2 = HEIGHT - PAD * 8 - 1;
	return 1;
}

void
domouse(Uxn *u, SDL_Event *event)
{
	Uint8 flag = 0x00;
	Uint16 addr = devmouse->addr + 2;
	Uint16 x = clamp(event->motion.x / ZOOM - PAD * 8, 0, HOR * 8 - 1);
	Uint16 y = clamp(event->motion.y / ZOOM - PAD * 8, 0, VER * 8 - 1);
	u->ram.dat[addr + 0] = (x >> 8) & 0xff;
	u->ram.dat[addr + 1] = x & 0xff;
	u->ram.dat[addr + 2] = (y >> 8) & 0xff;
	u->ram.dat[addr + 3] = y & 0xff;
	u->ram.dat[addr + 5] = 0x00;
	switch(event->button.button) {
	case SDL_BUTTON_LEFT: flag = 0x01; break;
	case SDL_BUTTON_RIGHT: flag = 0x10; break;
	}
	switch(event->type) {
	case SDL_MOUSEBUTTONUP:
		setflag(&u->ram.dat[addr + 4], flag, 0);
		break;
	case SDL_MOUSEBUTTONDOWN:
		setflag(&u->ram.dat[addr + 4], flag, 1);
		if(flag == 0x10 && getflag(&u->ram.dat[addr + 4], 0x01))
			u->ram.dat[addr + 5] = 0x01;
		if(flag == 0x01 && getflag(&u->ram.dat[addr + 4], 0x10))
			u->ram.dat[addr + 5] = 0x10;
		break;
	}
}

void
dotext(Uxn *u, SDL_Event *event)
{
	int i;
	Uint16 addr = devkey->addr + 2;
	if(SDL_GetModState() & KMOD_LCTRL || SDL_GetModState() & KMOD_RCTRL)
		return;
	for(i = 0; i < SDL_TEXTINPUTEVENT_TEXT_SIZE; ++i) {
		char c = event->text.text[i];
		if(c < ' ' || c > '~')
			break;
		u->ram.dat[addr] = c;
	}
}

void
doctrl(Uxn *u, SDL_Event *event, int z)
{
	Uint8 flag = 0x00;
	Uint16 addr = devctrl->addr + 2;
	if(z && event->key.keysym.sym == SDLK_h) {
		if(SDL_GetModState() & KMOD_LCTRL)
			toggledebug(u);
		if(SDL_GetModState() & KMOD_LALT)
			togglezoom(u);
	}
	switch(event->key.keysym.sym) {
	case SDLK_LCTRL: flag = 0x01; break;
	case SDLK_LALT: flag = 0x02; break;
	case SDLK_BACKSPACE:
		flag = 0x04;
		if(z) u->ram.dat[devkey->addr + 2] = 0x08;
		break;
	case SDLK_RETURN:
		flag = 0x08;
		if(z) u->ram.dat[devkey->addr + 2] = 0x0d;
		break;
	case SDLK_UP: flag = 0x10; break;
	case SDLK_DOWN: flag = 0x20; break;
	case SDLK_LEFT: flag = 0x40; break;
	case SDLK_RIGHT: flag = 0x80; break;
	}
	setflag(&u->ram.dat[addr], flag, z);
}

#pragma mark - Devices

Uint8
console_poke(Uxn *u, Uint16 ptr, Uint8 b0, Uint8 b1)
{
	Uint8 *m = u->ram.dat;
	switch(b0) {
	case 0x08: printf("%c", b1); break;
	case 0x09: printf("0x%02x\n", b1); break;
	case 0x0b: printf("0x%04x\n", (m[ptr + 0x0a] << 8) + b1); break;
	case 0x0d: printf("%s\n", &m[(m[ptr + 0x0c] << 8) + b1]); break;
	}
	fflush(stdout);
	(void)m;
	(void)ptr;
	(void)b0;
	return b1;
}

Uint8
screen_poke(Uxn *u, Uint16 ptr, Uint8 b0, Uint8 b1)
{
	Uint8 *m = u->ram.dat;
	ptr += 8;
	if(b0 == 0x0c) {
		Uint16 x = (m[ptr] << 8) + m[ptr + 1];
		Uint16 y = (m[ptr + 2] << 8) + m[ptr + 3];
		paintpixel(b1 >> 4 & 0xf ? screen.fg : screen.bg, x, y, b1 & 0xf);
		screen.reqdraw = 1;
	}
	return b1;
}

Uint8
sprite_poke(Uxn *u, Uint16 ptr, Uint8 b0, Uint8 b1)
{
	Uint8 *m = u->ram.dat;
	ptr += 8;
	if(b0 == 0x0e) {
		Uint16 v, h;
		Uint16 x = (m[ptr] << 8) + m[ptr + 1];
		Uint16 y = (m[ptr + 2] << 8) + m[ptr + 3];
		Uint8 blend = b1 & 0xf;
		Uint8 *layer = ((b1 >> 4) & 0xf) % 2 ? screen.fg : screen.bg;
		Uint8 *sprite = &m[(m[ptr + 4] << 8) + m[ptr + 5]];
		for(v = 0; v < 8; v++)
			for(h = 0; h < 8; h++) {
				Uint8 ch1 = ((sprite[v] >> (7 - h)) & 0x1);
				if(ch1 == 0 && (blend == 0x05 || blend == 0x0a || blend == 0x0f))
					continue;
				paintpixel(layer, x + h, y + v, ch1 ? blend % 4 : blend / 4);
			}
		screen.reqdraw = 1;
	}
	return b1;
}

Uint8
file_poke(Uxn *u, Uint16 ptr, Uint8 b0, Uint8 b1)
{
	Uint8 *m = u->ram.dat;
	char *name = (char *)&m[(m[ptr + 8] << 8) + m[ptr + 8 + 1]];
	Uint16 length = (m[ptr + 8 + 2] << 8) + m[ptr + 8 + 3];
	Uint16 offset = (m[ptr + 0] << 8) + m[ptr + 1];
	if(b0 == 0x0d) {
		Uint16 addr = (m[ptr + 8 + 4] << 8) + b1;
		FILE *f = fopen(name, "r");
		if(f && fseek(f, offset, SEEK_SET) != -1 && fread(&m[addr], length, 1, f)) {
			fclose(f);
			printf("Loaded %d bytes, at %04x from %s\n", length, addr, name);
		}
	} else if(b0 == 0x0f) {
		Uint16 addr = (m[ptr + 8 + 6] << 8) + b1;
		FILE *f = fopen(name, (m[ptr + 2] & 0x1) ? "a" : "w");
		if(f && fseek(f, offset, SEEK_SET) != -1 && fwrite(&m[addr], length, 1, f)) {
			fclose(f);
			printf("Saved %d bytes, at %04x from %s\n", length, addr, name);
		}
	}
	return b1;
}

Uint8
midi_poke(Uxn *u, Uint16 ptr, Uint8 b0, Uint8 b1)
{
	(void)u;
	printf("%04x - %02x,%02x\n", ptr, b0, b1);
	return b1;
}

Uint8
datetime_poke(Uxn *u, Uint16 ptr, Uint8 b0, Uint8 b1)
{
	Uint8 *m = u->ram.dat;
	time_t seconds = time(NULL);
	struct tm *t = localtime(&seconds);
	t->tm_year += 1900;
	m[ptr + 0] = (t->tm_year & 0xff00) >> 8;
	m[ptr + 1] = t->tm_year & 0xff;
	m[ptr + 2] = t->tm_mon;
	m[ptr + 3] = t->tm_mday;
	m[ptr + 4] = t->tm_hour;
	m[ptr + 5] = t->tm_min;
	m[ptr + 6] = t->tm_sec;
	m[ptr + 7] = t->tm_wday;
	m[ptr + 8] = (t->tm_yday & 0xff00) >> 8;
	m[ptr + 9] = t->tm_yday & 0xff;
	m[ptr + 10] = t->tm_isdst;
	(void)b0;
	return b1;
}

Uint8
system_poke(Uxn *u, Uint16 ptr, Uint8 b0, Uint8 b1)
{
	Uint8 *m = u->ram.dat;
	m[PAGE_DEVICE + b0] = b1;
	loadtheme(&m[PAGE_DEVICE + 0x0008]);
	(void)ptr;
	return b1;
}

Uint8
ppnil(Uxn *u, Uint16 ptr, Uint8 b0, Uint8 b1)
{
	(void)u;
	(void)ptr;
	(void)b0;
	return b1;
}

#pragma mark - Generics

int
start(Uxn *u)
{
	inituxn(u, 0x0200);
	redraw(pixels, u);
	while(1) {
		SDL_Event event;
		double elapsed, start = SDL_GetPerformanceCounter();
		SDL_LockAudioDevice(audio_id);
		while(SDL_PollEvent(&event) != 0) {
			switch(event.type) {
			case SDL_QUIT:
				SDL_UnlockAudioDevice(audio_id);
				quit();
				break;
			case SDL_MOUSEBUTTONUP:
			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEMOTION:
				domouse(u, &event);
				evaluxn(u, devmouse->vector);
				break;
			case SDL_KEYDOWN:
			case SDL_KEYUP:
				doctrl(u, &event, event.type == SDL_KEYDOWN);
				evaluxn(u, devctrl->vector);
				break;
			case SDL_TEXTINPUT:
				dotext(u, &event);
				evaluxn(u, devkey->vector);
				break;
			case SDL_WINDOWEVENT:
				if(event.window.event == SDL_WINDOWEVENT_EXPOSED)
					redraw(pixels, u);
				break;
			}
		}
		evaluxn(u, devscreen->vector);
		SDL_UnlockAudioDevice(audio_id);
		if(screen.reqdraw)
			redraw(pixels, u);
		elapsed = (SDL_GetPerformanceCounter() - start) / (double)SDL_GetPerformanceFrequency() * 1000.0f;
		SDL_Delay(clamp(16.666f - elapsed, 0, 1000));
	}
	return 1;
}

int
main(int argc, char **argv)
{
	Uxn u;

	if(argc < 2)
		return error("Input", "Missing");
	if(!bootuxn(&u))
		return error("Boot", "Failed");
	if(!loaduxn(&u, argv[1]))
		return error("Load", "Failed");
	if(!init())
		return error("Init", "Failed");

	devsystem = portuxn(&u, 0x00, "system", system_poke);
	portuxn(&u, 0x01, "console", console_poke);
	devscreen = portuxn(&u, 0x02, "screen", screen_poke);
	portuxn(&u, 0x03, "sprite", sprite_poke);
	devctrl = portuxn(&u, 0x04, "controller", ppnil);
	devkey = portuxn(&u, 0x05, "key", ppnil);
	devmouse = portuxn(&u, 0x06, "mouse", ppnil);
	portuxn(&u, 0x07, "file", file_poke);
	if(!initapu(&u, 0x08))
		return 1;
	portuxn(&u, 0x09, "midi", ppnil);
	portuxn(&u, 0x0a, "datetime", datetime_poke);
	portuxn(&u, 0x0b, "---", ppnil);
	portuxn(&u, 0x0c, "---", ppnil);
	portuxn(&u, 0x0d, "---", ppnil);
	portuxn(&u, 0x0e, "---", ppnil);
	portuxn(&u, 0x0f, "---", ppnil);

	/* Write screen size to dev/screen */
	u.ram.dat[devscreen->addr + 2] = (HOR * 8 >> 8) & 0xff;
	u.ram.dat[devscreen->addr + 3] = HOR * 8 & 0xff;
	u.ram.dat[devscreen->addr + 4] = (VER * 8 >> 8) & 0xff;
	u.ram.dat[devscreen->addr + 5] = VER * 8 & 0xff;

	start(&u);
	quit();
	return 0;
}
