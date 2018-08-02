/**
 *
 * Copyright 2018 David Aylaian
 * https://github.com/davidaylaian/
 * Licensed under the MIT License
 *
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <SDL2/SDL.h>

/////////////////////////////
// OPTIONS START
/////////////////////////////

/*
 * The number of cycles run by the emulator every frame.
 * Different games will expect this to be set to different things.
 */
size_t ticks_per_frame = 15;

/*
 * To change the speed of this game, multiply this number by how
 * much faster or slower you would like the game to be.
 */
double speed = 1.0;

/*
 * CHIP-8 has a 64x32 pixel display. This is multiplied by
 * the below variable in order to make the size of the window.
 */
#define scale 15

// foreground color
#define fgcolor 0xAAEEFF

// background color
#define bgcolor 0x0066FF

//////////////////////////////
// OPTIONS END
//////////////////////////////

// memory
uint8_t mem[4096];
uint16_t stack[16];
uint8_t sp;

// cpu registers
uint8_t v[16];		// general purpose registers
uint16_t i;		// index register
uint16_t pc;		// program counter

// timers
uint8_t dt;		// 60 Hz, counts down to 0
uint8_t st;		// 60 Hz, counts down to 0, buzz when st > 0

// devices
#define width 64
#define height 32
bool screen[width][height];
bool keys[16];

// chip8 font (http://devernay.free.fr/hacks/chip8/C8TECH10.HTM#font)
const uint8_t font[] = {
	0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
	0x20, 0x60, 0x20, 0x20, 0x70, // 1
	0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
	0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
	0x90, 0x90, 0xF0, 0x10, 0x10, // 4
	0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
	0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
	0xF0, 0x10, 0x20, 0x40, 0x40, // 7
	0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
	0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
	0xF0, 0x90, 0xF0, 0x90, 0x90, // A
	0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
	0xF0, 0x80, 0x80, 0x80, 0xF0, // C
	0xE0, 0x90, 0x90, 0x90, 0xE0, // D
	0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
	0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

// suspension
bool guru_meditation = false;
bool user_pause = false;

// dumps the contents of memory to stdout
void dump_mem()
{
	for (size_t n = 0; n < sizeof mem; n++)
	{
		if (n % 32 == 0) {
			puts("");
		}

		printf("%02x ", mem[n]);
	}
}

// dumps the contents of screen to stdout
void dump_screen()
{
	for (size_t y = 0; y < height; y++)
	{
		puts("");

		for (size_t x = 0; x < width; x++)
		{
			printf("%d", screen[x][y]);
		}
	}
}

/* loads a rom located at the given path
 *   0 = success
 *   1 = unable to open rom
 *   2 = rom is too large
 */
int load_rom(char* name)
{
	// reset vars
	memset(mem, 0, sizeof mem);
	memset(v, 0, sizeof v);
	memset(screen, 0, sizeof screen);
	memset(keys, 0, sizeof keys);
	sp = 0, i = 0, pc = 512, dt = 0, st = 0;

	// load font
	for (int n = 0; n < sizeof font; n++)
	{
		mem[n] = font[n];
	}

	// open rom
	FILE* rom = fopen(name, "rb");
	if (!rom) {
		return 1;
	}

	// get length of rom
	fseek(rom, 0, SEEK_END);
	long len = ftell(rom);
	if (len > sizeof mem - pc) {
		return 2;
	}

	// load rom
	uint8_t* load = mem + pc;
	fseek(rom, 0, SEEK_SET);
	fread(load, 1, len, rom);
	fclose(rom);
	return 0;
}

// updates the timers; call 60 times per second
void update_timers()
{
	if (dt > 0) {
		dt--;
	}

	if (st > 0) {
		// no sound output is supported, but if it was, it would go here
		st--;
	}
}

bool prompting = false;

/* executes the next opcode
 *   0 = success
 *   1 = pc out of bounds
 *   2 = invalid opcode
 */
int tick()
{
	// suspension
	if (guru_meditation || user_pause) {
		return 0;
	}

	// pc out of bounds
	if (pc >= sizeof mem) {
		return 1;
	}

	// get next opcode
	uint8_t hi = mem[pc];
	uint8_t lo = mem[pc + 1];
	uint16_t opcode = hi << 8 | lo;
	uint8_t x = hi & 0x0F;
	uint8_t y = (lo & 0xF0) >> 4;
	uint16_t addr = opcode & 0x0FFF;

	//printf("pc=%d, opcode=0x%x\n", pc, opcode);

	if (prompting) {
		goto prompt;
	}

	// decode and execute next opcode
	switch ((hi & 0xF0) >> 4) {

		case 0x0:
			// clear screen
			if (opcode == 0x00E0) {
				memset(screen, 0, sizeof screen);
			}
			// return from subroutine
			else if (opcode == 0x00EE) {
				pc = stack[--sp];
			}
			// invalid opcode
			else {
				return 2;
			}

			pc = pc + 2;
			break;

		// jump to addr
		case 0x1:
			pc = opcode & addr;
			break;

		// call subroutine at addr
		case 0x2:
			stack[sp++] = pc;
			pc = opcode & addr;
			break;

		// skip next if Vx = lo
		case 0x3:
			if (v[x] == lo) {
				pc = pc + 2;
			}

			pc = pc + 2;
			break;

		// skip next if Vx != lo
		case 0x4:
			if (v[x] != lo) {
				pc = pc + 2;
			}

			pc = pc + 2;
			break;

		// skip next if Vx = Vy
		case 0x5:
			if (v[x] == v[y]) {
				pc = pc + 2;
			}

			pc = pc + 2;
			break;

		// set Vx <- lo
		case 0x6:
			v[x] = lo;
			pc = pc + 2;
			break;

		// set Vx <- Vx + lo
		case 0x7:
			v[x] = v[x] + lo;
			pc = pc + 2;
			break;

		case 0x8:
			switch (lo & 0x0F) {

				// set Vx <- Vy
				case 0x0:
					v[x] = v[y];
					break;

				// set Vx <- Vx | Vy
				case 0x1:
					v[x] = v[x] | v[y];
					break;

				// set Vx <- Vx & Vy
				case 0x2:
					v[x] = v[x] & v[y];
					break;

				// set Vx <- Vx ^ Vy
				case 0x3:
					v[x] = v[x] ^ v[y];
					break;

				// set Vx <- Vx + Vy
				case 0x4: {
					int ans = (int) v[x] + v[y];
					v[x] = ans;

					// set carry flag
					v[0xF] = 0;
					if (ans > 255) {
						v[0xF] = 1;
					}

					break;
				}

				// set Vx <- Vx - Vy
				case 0x5: {
					int ans = (int) v[x] - v[y];
					v[x] = ans;

					// set borrow flag
					v[0xF] = 1;
					if (ans < 0) {
						v[0xF] = 0;
					}

					break;
				}

				// set Vx <- Vx / 2
				case 0x6:
					v[0xF] = v[x] & 0b1; // set lsb flag
					v[x] = v[x] / 2;
					break;

				// set Vx <- Vy - Vx
				case 0x7: {
					int ans = (int) v[y] - v[x];
					v[x] = ans;

					// set borrow flag
					v[0xF] = 1;
					if (ans < 0) {
						v[0xF] = 0;
					}

					break;
				}

				// set Vx <- Vx * 2
				case 0xE:
					v[0xF] = v[x] >> 7; // set msb flag
					v[x] = v[x] * 2;
					break;

				// invald opcode
				default:
					return 2;
			}

			pc = pc + 2;
			break;

		// skip next if Vx != Vy
		case 0x9:
			if (v[x] != v[y]) {
				pc = pc + 2;
			}

			pc = pc + 2;
			break;

		// set I <- addr
		case 0xA:
			i = addr;
			pc = pc + 2;
			break;

		// jump to addr + V0
		case 0xB:
			pc = addr + v[0x0];
			break;

		// set Vx <- random uint8 & lo
		case 0xC:
			v[x] = rand() % 255 & lo;
			pc = pc + 2;
			break;

		// draw sprite
		case 0xD:
			v[0xF] = 0;

			for (uint8_t a = 0; a < (lo & 0x0F); a++)
			{
				uint8_t line = mem[i + a];

				for (uint8_t b = 0; b < 8; b++)
				{
					if ((line & 0x80) > 0)
					{
						size_t xpos = (v[x] + b) % width;
						size_t ypos = (v[y] + a) % height; 

						if (screen[xpos][ypos]) {
							v[0xF] = 1;
						}

						screen[xpos][ypos] ^= 1;
					}

					line = line << 1;
				}
			}

			pc = pc + 2;
			break;

		case 0xE:
			switch (lo) {

				// skip next if the key with value Vx is pressed
				case 0x9E:
					if (keys[v[x]]) {
						pc = pc + 2;
					}

					break;

				// skip next if the key with value Vx is not pressed
				case 0xA1:
					if (!keys[v[x]]) {
						pc = pc + 2;
					}

					break;

				// invalid operator
				default:
					return 2;
			}

			pc = pc + 2;
			break;

		case 0xF:
			switch (lo) {

				// set Vx <- dt
				case 0x07:
					v[x] = dt;
					break;

				// wait for key press, then store Vx <- key value
				case 0x0A:
				prompt:
					prompting = true;

					for (size_t n = 0; n < sizeof keys; n++)
					{
						if (keys[n])
						{
							v[x] = n;
							prompting = false;
							pc = pc + 2;
							return 0;
						}
					}

					return 0;

				// set dt <- Vx
				case 0x15:
					dt = v[x];
					break;

				// set st <- Vx
				case 0x18:
					st = v[x];
					break;

				// set I <- I + Vx
				case 0x1E:
					i = i + v[x];
					break;

				// set I <- location of sprite for the hex character Vx
				case 0x29:
					i = v[x] % 0xF * 5;
					break;

				// bcd of Vx
				case 0x33:
					mem[i + 0] = v[x] / 100;
					mem[i + 1] = v[x] / 10 % 10;
					mem[i + 2] = v[x] % 10;
					break;

				// dump registers
				case 0x55:
					for (uint8_t j = 0; j <= x; j++) {
						mem[i + j] = v[j];
					}

					i = i + x + 1;
					break;

				// load registers
				case 0x65:
					for (uint8_t j = 0; j <= x; j++) {
						v[j] = mem[i + j];
					}

					i = i + x + 1;
					break;

				// invalid operator
				default:
					return 2;
			}

			pc += 2;
			break;
	}

	return 0;
}

// actual window size
#define swidth (width * scale)
#define sheight (height * scale)
uint8_t pixels[4 * swidth * sheight];

// draw one pixel to the screen
void draw_pixel(size_t x, size_t y, size_t color)
{
	size_t offset = 4 * (swidth * y + x);

	pixels[offset + 0] = color & 0xFF;
	pixels[offset + 1] = (color >> 8) & 0xFF;
	pixels[offset + 2] = (color >> 16) & 0xFF;
}

// draw one big, scaled up pixel to the screen
void draw_scaled_pixel(size_t x, size_t y, size_t color)
{
	for (size_t y2 = 0; y2 < scale; y2++)
	{
		for (size_t x2 = 0; x2 < scale; x2++)
		{
			draw_pixel(x * scale + x2, y * scale + y2, color);
		}
	}
}

uint64_t gettimestamp()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (uint64_t) 1000000 * tv.tv_sec + tv.tv_usec;
}

/* CHIP-8 uses a hex keyboard
 *  1 2 3 C
 *  4 5 6 D
 *  7 8 9 E
 *  A 0 B F
 */
int keymap[] = {
	SDLK_x, SDLK_1, SDLK_2, SDLK_3,
	SDLK_q, SDLK_w, SDLK_e, SDLK_a,
	SDLK_s, SDLK_d, SDLK_z, SDLK_c,
	SDLK_4, SDLK_r, SDLK_f, SDLK_v
};

SDL_Window* window;
SDL_Renderer* renderer;
SDL_Texture* texture;

// close the window
void sdl_terminate()
{
	SDL_DestroyTexture(texture);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
}

int main(int argc, char** argv)
{
	start:
	srand(time(0));

	if (argc > 2) {
		fprintf(stderr, "Invalid arguments\n");
		return 1;
	}

	// open rom
	int res;
	if (argc == 2) {
		res = load_rom(argv[1]);
	} else {
		res = load_rom("BOOT1");
	}

	if (res == 1) {
		fprintf(stderr, "Unable to open ROM\n");
		return 1;
	}

	if (res == 2) {
		fprintf(stderr, "ROM is too large\n");
		return 1;
	}

	// init sdl
	SDL_Init(SDL_INIT_VIDEO);
	window = SDL_CreateWindow("CHIP8", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, swidth, sheight, 0);
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, swidth, sheight);

	SDL_Event event;
	bool running = true;
	user_pause = false;

	while (running)
	{
		uint64_t start = gettimestamp();
		for (size_t k = 0; k < ticks_per_frame; k++)
		{
			// sdl event handling
			while (SDL_PollEvent(&event))
			{
				switch (event.type) {

					case SDL_KEYDOWN:
						if (event.key.keysym.mod & KMOD_CTRL)
						{
							// ctrl + q = quit
							if (event.key.keysym.sym == SDLK_q) {
								running = false;
							}

							// ctrl + p = pause
							if (event.key.keysym.sym == SDLK_p) {
								user_pause = !user_pause;
							}

							// ctrl + r = reset
							if (event.key.keysym.sym == SDLK_r) {
								sdl_terminate();
								goto start;
							}
						}
						else
						{
							// update keys
							for (int j = 0; j <= 0xF; j++)
							{
								if (keymap[j] == event.key.keysym.sym) {
									keys[j] = 1;
								}
							}
						}

						break;

					case SDL_KEYUP:
						// update keys
						for (int j = 0; j <= 0xF; j++)
						{
							if (keymap[j] == event.key.keysym.sym) {
								keys[j] = 0;
							}
						}

						break;

					case SDL_QUIT:
						running = false;
						break;
				}
			}

			// run cycle
			res = tick();

			if (res == 1) {
				fprintf(stderr, "PC out of bounds\n");
				guru_meditation = true;
			}

			if (res == 2) {
				fprintf(stderr, "Invalid opcode\n");
				guru_meditation = true;
			}
		}

		SDL_RenderClear(renderer);

		// draw the window
		for (uint8_t y = 0; y < height; y++)
		{
			for (uint8_t x = 0; x < width; x++)
			{
				draw_scaled_pixel(x, y, screen[x][y] ? fgcolor : bgcolor);
			}
		}

		// render the window
		SDL_UpdateTexture(texture, NULL, &pixels[0], swidth * 4);
		SDL_RenderCopy(renderer, texture, NULL, NULL);
		SDL_RenderPresent(renderer);

		// lock framerate to 60 fps
		while (gettimestamp() < start + 1000000 / 60 / speed - 10000) SDL_Delay(5);
		while (gettimestamp() < start + 1000000 / 60 / speed);
		update_timers();
	}

	sdl_terminate();
	return 0;
}
