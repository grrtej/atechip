#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <stack>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

using u8 = std::uint8_t;
using u16 = std::uint16_t;

void SDLCALL open_file_dialog_callback(void* userdata, const char* const* filelist, int filter);
void start(const std::vector<u8>& rom);
void cycle();

constexpr auto DT_ST_INTERVAL{ std::chrono::milliseconds{1000 / 60} }; // 60 Hz
constexpr auto CPU_INTERVAL{ std::chrono::milliseconds{1000 / 500} }; // 500 Hz

const std::array<u8, 5 * 16> FONT{
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

const std::unordered_map<SDL_Scancode, int> KEYBINDS{
	{SDL_SCANCODE_1, 0x1}, {SDL_SCANCODE_2, 0x2}, {SDL_SCANCODE_3, 0x3}, {SDL_SCANCODE_4, 0xC},
	{SDL_SCANCODE_Q, 0x4}, {SDL_SCANCODE_W, 0x5}, {SDL_SCANCODE_E, 0x6}, {SDL_SCANCODE_R, 0xD},
	{SDL_SCANCODE_A, 0x7}, {SDL_SCANCODE_S, 0x8}, {SDL_SCANCODE_D, 0x9}, {SDL_SCANCODE_F, 0xE},
	{SDL_SCANCODE_Z, 0xA}, {SDL_SCANCODE_X, 0x0}, {SDL_SCANCODE_C, 0xB}, {SDL_SCANCODE_V, 0xF},
};

// evil global variables, change asap
// why evil? no way to control what can read/write, unknown dependencies

/*
* APPLICATION STATE
*/
SDL_Window* window{};
SDL_Renderer* renderer{};
bool running{ false }; // true when app is running, false when exiting
bool started{ false }; // true when rom loaded
std::random_device rd{};
std::mt19937 mt(rd());
std::uniform_int_distribution<> dist(0, 255);


/*
* CHIP8 STATE
*/
std::array<u8, 16> v{};
std::array<u8, 4096> memory{};
u16 pc{ 512 };
u16 ip{};
std::stack<u16> stack;
u8 dt{};
u8 st{};

// monochrome display
std::array<std::array<bool, 64>, 32> display{};

// key state
std::array<bool, 16> keys{};
int last_key{ -1 };

int main(int argc, char* argv[])
{
	SDL_Init(SDL_INIT_VIDEO);
	window = SDL_CreateWindow("atechip", 960, 480, 0); // 15x scale
	renderer = SDL_CreateRenderer(window, nullptr);

	SDL_ShowOpenFileDialog(open_file_dialog_callback, nullptr, window, nullptr, 0, nullptr, false);

	auto cpu_prev = std::chrono::high_resolution_clock::now();
	auto dt_st_prev = std::chrono::high_resolution_clock::now();

	running = true;
	while (running) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
			case SDL_EVENT_QUIT:
				running = false;
				break;
			case SDL_EVENT_KEY_DOWN:
				if (KEYBINDS.contains(event.key.scancode)) {
					keys[KEYBINDS.at(event.key.scancode)] = true;
					last_key = KEYBINDS.at(event.key.scancode);
				}
				break;
			case SDL_EVENT_KEY_UP:
				if (KEYBINDS.contains(event.key.scancode)) {
					keys[KEYBINDS.at(event.key.scancode)] = false;
					last_key = -1;
				}
				break;
			}
		}

		auto now = std::chrono::high_resolution_clock::now();

		auto cpu_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - cpu_prev);
		auto dt_st_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - dt_st_prev);

		if (cpu_elapsed >= CPU_INTERVAL) {
			cycle();
			cpu_prev = now;
		}

		if (dt_st_elapsed >= DT_ST_INTERVAL) {
			if (dt != 0) dt--;
			if (st != 0) st--;
			dt_st_prev = now;
		}

		for (auto y = 0; y < 32; y++) {
			for (auto x = 0; x < 64; x++) {
				if (display[y][x]) {
					SDL_SetRenderDrawColor(renderer, 0x21, 0x29, 0x46, 0xff);
				}
				else {
					SDL_SetRenderDrawColor(renderer, 0x61, 0x86, 0xa9, 0xff);
				}

				SDL_FRect rect{ x * 15.0f,y * 15.0f,15.0f,15.0f };
				SDL_RenderFillRect(renderer, &rect);
			}
		}
		SDL_RenderPresent(renderer);
	}

	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}

void SDLCALL open_file_dialog_callback(void* userdata, const char* const* filelist, int filter)
{
	if (!*filelist) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "no file selected", "no file selected, exiting...", window);
		running = false; // or send sdl event OR std::exit?
	}

	std::ifstream file(filelist[0], std::ios::binary);
	auto filesize = fs::file_size(filelist[0]);
	std::vector<u8> rom{};
	rom.resize(filesize);
	file.read(reinterpret_cast<char*>(rom.data()), filesize);

	// question: when is rom deallocated?
	start(rom);
}

// kick off the emulation
void start(const std::vector<u8>& rom)
{
	// copy rom into memory @ 512
	std::copy(rom.begin(), rom.end(), memory.begin() + 512);

	// copy the font as well
	std::copy(FONT.begin(), FONT.end(), memory.begin());

	started = true;
}

// advance chip8 state
void cycle()
{
	if (!started) return;

	// this can go out of bounds easily if bad rom
	const u16 instruction = memory[pc] << 8 | memory[pc + 1];
	pc += 2;

	// various slices of instruction (wxyz)
	const auto xyz = instruction & 0xfff;
	const auto yz = instruction & 0xff;
	const auto z = instruction & 0xf;
	const auto w = instruction >> 12 & 0xf;
	const auto x = instruction >> 8 & 0xf;
	const auto y = instruction >> 4 & 0xf;

	switch (w) {
	case 0x0:
		switch (yz) {
		case 0xe0:
			for (auto y = 0; y < 32; y++) {
				for (auto x = 0; x < 64; x++) {
					display[y][x] = false;
				}
			}
			break;
		case 0xee:
			pc = stack.top();
			stack.pop();
			break;
		}
		break;
	case 0x1:
		pc = xyz;
		break;
	case 0x2:
		stack.push(pc);
		pc = xyz;
		break;
	case 0x3:
		if (v[x] == yz) pc += 2;
		break;
	case 0x4:
		if (v[x] != yz) pc += 2;
		break;
	case 0x5:
		if (v[x] == v[y]) pc += 2;
		break;
	case 0x6:
		v[x] = yz;
		break;
	case 0x7:
		v[x] += yz;
		break;
	case 0x8:
		switch (z) {
		case 0x0:
			v[x] = v[y];
			break;
		case 0x1:
			v[x] |= v[y];
			break;
		case 0x2:
			v[x] &= v[y];
			break;
		case 0x3:
			v[x] ^= v[y];
			break;
		case 0x4:
			v[0xf] = v[x] + v[y] > 255;
			v[x] += v[y];
			break;
		case 0x5:
			v[0xf] = v[x] > v[y];
			v[x] -= v[y];
			break;
		case 0x6:
			v[0xf] = v[x] & 1;
			v[x] >>= 1;
			break;
		case 0x7:
			v[0xf] = v[y] > v[x];
			v[x] = v[y] - v[x];
			break;
		case 0xe:
			v[0xf] = v[x] >> 7 & 1;
			v[x] <<= 1;
			break;
		}
		break;
	case 0x9:
		if (v[x] != v[y]) pc += 2;
		break;
	case 0xa:
		ip = xyz;
		break;
	case 0xb:
		pc = xyz + v[0];
		break;
	case 0xc:
		v[x] = dist(mt) & yz;
		break;
	case 0xd:
		for (auto i = 0; i < z; i++) {
			u8 line = memory[ip + i];
			for (auto j = 0; j < 8; j++) {
				auto dx = (v[x] + j) % 64;
				auto dy = (v[y] + i) % 32;

				bool pixel = display[dy][dx] ^ (line >> (7 - j) & 1);

				v[0xf] = display[dy][dx] && !pixel; // set collision if pixel cleared
				display[dy][dx] = pixel;
			}
		}
		break;
	case 0xe:
		switch (yz) {
		case 0x9e:
			if (keys[v[x]]) pc += 2;
			break;
		case 0xa1:
			if (!keys[v[x]]) pc += 2;
			break;
		}
		break;
	case 0xf:
		switch (yz) {
		case 0x07:
			v[x] = dt;
			break;
		case 0x15:
			dt = v[x];
			break;
		case 0x18:
			st = v[x];
			break;
		case 0x1e:
			ip += v[x];
			break;
		case 0x29:
			ip = v[x] * 5;
			break;
		case 0x33:
			memory[ip] = v[x] / 100;
			memory[ip + 1] = (v[x] / 10) % 10;
			memory[ip + 2] = v[x] % 10;
			break;
		case 0x55:
			std::copy(v.begin(), v.begin() + x + 1, memory.begin() + ip);
			break;
		case 0x65:
			std::copy(memory.begin() + ip, memory.begin() + ip + x + 1, v.begin());
			break;
		}
		break;
	}
}
