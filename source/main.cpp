#include "engine.h"
#include "core.h"

struct LFO_Param{
	void set_frequency(float frequency){
		period_length = (float)((double)Audio::samples_per_second / (double)frequency);
	}

	int pause;
	float period_length;
};
struct LFO_Data{
	float period_cursor; // normalized in [0 ; 1]
};
static u8 counter = 0;
void LFO_Processor(u32 frame_count, s16* output, void* in_param, void* in_internal_data){
	LFO_Param* param = (LFO_Param*)in_param;
	LFO_Data* internal_data = (LFO_Data*)in_internal_data;

	if (!param->pause){
		float period_length = param->period_length;
		float cursor = internal_data->period_cursor * period_length;

		for (int isample = 0; isample != frame_count; ++isample){
			float cursor_norm = cursor / period_length;

			float wave = sinf(cursor_norm * 2.f * PI<float>);
			float fvalue = wave * (float)INT16_MAX;
			s16 ivalue = (s16)fvalue;

			output[isample] += ivalue;
			cursor = fmodf(cursor + 1.f, period_length);
		}

		internal_data->period_cursor = cursor / period_length;
	}
}

// REF: http://devernay.free.fr/hacks/chip8/C8TECH10.HTM#1.0 [Cowgod's Chip-8 Technical Reference v1.0]

struct Chip8{
	int screen_width;
	int screen_height;

	float emulation_speed;

	float instructions_per_second;
	float timer_per_second;

	float instruction_accumulator;
	float timer_accumulator;

	struct Memory{
		// interpreter memory covers adresses 0x000 - 0x1FF
		struct Interpreter{
			u8 sprites[80];
			u8 unused[512 - sizeof(sprites)];
		} interpreter_range;

		// user memory covers adresses 0x1FF - 0xFFF
		u8 user_range[Kilobytes(4) - sizeof(Interpreter)];
	} memory;

	union Registers
	{
		struct
		{
			u8 V0;
			u8 V1;
			u8 V2;
			u8 V3;
			u8 V4;
			u8 V5;
			u8 V6;
			u8 V7;
			u8 V8;
			u8 V9;
			u8 VA;
			u8 VB;
			u8 VC;
			u8 VD;
			u8 VE;
			u8 VF; // flag register
		};
		u8 by_index[16];
	} registers;

	u16 I;

	u8 DT; // delay timer register
	u8 ST; // sound timer register

	u16 PC; // program counter
	u16 SP; // stack pointer

	u16 STACK[16];

	// monochrome ; 64x32 ; column major ; (0, 0) top-left ; (63, 31) bottom-right
	u8 SCREEN[256]; 

	// 0 1 2 3 4 5 6 7 8 9 A B C D E F
	// with original layout
	// 1 2 3 C
	// 4 5 6 D
	// 7 8 9 E
	// A 0 B F
	u8 KEYBOARD[16];
	u8 LAST_KEYBOARD[16];

	enum ERROR_TYPE{
		NONE = 0,
		MEMORY_OUT_OF_BOUNDS,
		REGISTER_OUT_OF_BOUNDS,
		PC_INCORRECT,
		SP_INCORRECT,
		INSTRUCTION_UNKNOWN,
		KEY_UNKNOWN,
		SCREEN_COORD_INCORRECT,
	};
	ERROR_TYPE ERROR;
};

void Chip8_validate_memory(Chip8* chip8, u16 adress, u16 size){
	if ((adress > sizeof(Chip8::Memory::Interpreter::sprites) && adress < 0x200) || (adress + size) > 0xFFF){
		chip8->ERROR = Chip8::MEMORY_OUT_OF_BOUNDS;
		ram_assert_msg(false, "MEMORY_OUT_OF_BOUNDS");
	}
}

u8* Chip8_get_memory(Chip8* chip8, u16 adress){
	return (u8*)&chip8->memory + adress;
}

void Chip8_validate_registers(Chip8* chip8, u16 register_index, u16 register_count){
	if ((register_index + register_count) > 16){
		chip8->ERROR = Chip8::REGISTER_OUT_OF_BOUNDS;
		ram_assert_msg(false, "REGISTER_OUT_OF_BOUNDS");
	}
}

void Chip8_create( Chip8* chip8, void* ROM, size_t ROM_size )
{
	chip8->screen_width = 64;
	chip8->screen_height = 32;

	chip8->emulation_speed = 1.f;

	chip8->instructions_per_second = 500.f;
	chip8->timer_per_second = 60.f;

	chip8->instruction_accumulator = 0.f;
	chip8->timer_accumulator = 0.f;

	memset(&chip8->memory, 0x00, sizeof(Chip8::memory));
	memset(&chip8->registers, 0x00, sizeof(Chip8::registers));

	chip8->I = 0;

	chip8->DT = 0;
	chip8->ST = 0;

	chip8->PC = 0;
	chip8->SP = 0;

	memset(&chip8->STACK, 0x00, sizeof(Chip8::STACK));
	memset(&chip8->SCREEN, 0x00, sizeof(Chip8::SCREEN));
	memset(&chip8->KEYBOARD, 0x00, sizeof(Chip8::KEYBOARD));
	memset(&chip8->LAST_KEYBOARD, 0x00, sizeof(Chip8::KEYBOARD));

	chip8->ERROR = Chip8::NONE;

	static_assert(offsetof(Chip8::Memory, user_range) == 0x200);
	static_assert(sizeof(Chip8::Memory) == 4096);

	u8 sprite_data[] = {
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

	// sizeof(sprite_data) == sizeof(Chip8::memory::interpreter_range::sprites)
	static_assert(sizeof(sprite_data) == sizeof(Chip8::Memory::Interpreter::sprites));

	memcpy(chip8->memory.interpreter_range.sprites, sprite_data, sizeof(Chip8::Memory::Interpreter::sprites));

	chip8->PC = 0x200;

	ram_assert(ROM_size <= sizeof(Chip8::Memory::user_range));
	memcpy((void*)(chip8->memory.user_range), ROM, ROM_size);
}

void Chip8_destroy(Chip8* chip8){

}

void Chip8_step(Chip8* chip8, float dtime_sec ){
	dtime_sec *= chip8->emulation_speed;
	
	chip8->instruction_accumulator += chip8->instructions_per_second * dtime_sec;
	int instruction_count = (int)floorf(chip8->instruction_accumulator);
	chip8->instruction_accumulator -= (float)instruction_count;

	float step_timer_decrement = chip8->timer_per_second * dtime_sec;
	float timer_decrement_per_instruction = step_timer_decrement / instruction_count;

	short instruction = 0x0000;
	char* instruction_byte = (char*)&instruction;

	//instruction_count = 1;
	while (instruction_count)
	{
		chip8->timer_accumulator += timer_decrement_per_instruction;
		int timer_decrement = (int)chip8->timer_accumulator;
		chip8->timer_accumulator -= (float)timer_decrement;

		chip8->DT -= min(chip8->DT, (u8)timer_decrement);
		chip8->ST -= min(chip8->ST, (u8)timer_decrement);

		Chip8_validate_memory(chip8, chip8->PC, 2);
		if (chip8->ERROR) break;

		u8* memptr = Chip8_get_memory(chip8, chip8->PC);
		instruction_byte[1] = *memptr;
		++memptr;
		instruction_byte[0] = *memptr;
		chip8->PC += 2;

		if( instruction == 0x00E0 ) // CLS
		{
			memset( chip8->SCREEN, 0x00, sizeof( Chip8::SCREEN ) );
		}
		else if( instruction == 0x00EE ) // RET
		{
			if (chip8->SP == 0){
				chip8->ERROR = Chip8::SP_INCORRECT;
				break;
			}

			--chip8->SP;
			chip8->PC = chip8->STACK[chip8->SP];
		}
		else if( ( instruction & 0xF000 ) == 0x1000 ) // JP addr
		{
			short addr = instruction & 0x0FFF;

			Chip8_validate_memory(chip8, addr, 1);
			if (chip8->ERROR) break;

			chip8->PC = addr;
		}
		else if( ( instruction & 0xF000 ) == 0x2000 ) // CALL addr
		{
			short addr = instruction & 0x0FFF;

			if (chip8->SP == sizeof(Chip8::STACK) - 1) chip8->ERROR = Chip8::SP_INCORRECT;
			Chip8_validate_memory(chip8, addr, 2);
			if (chip8->ERROR) break;

			chip8->STACK[chip8->SP++] = chip8->PC;
			chip8->PC = addr;
		}
		else if( ( instruction & 0xF000 ) == 0x3000 ) // SE Vx, byte
		{
			short regindex = (instruction & 0x0F00) >> 8;

			Chip8_validate_registers(chip8, regindex, 1);
			if (chip8->ERROR) break;

			short regcmp = instruction & 0x00FF;
			if (chip8->registers.by_index[regindex] == regcmp)
				chip8->PC = chip8->PC + 2;
		}
		else if( ( instruction & 0xF000 ) == 0x4000 ) // SNE Vx, byte
		{
			short regindex = ( instruction & 0x0F00 ) >> 8;

			Chip8_validate_registers(chip8, regindex, 1);
			if (chip8->ERROR) break;

			short regcmp = ( instruction & 0x00FF );
			if( chip8->registers.by_index[regindex] != regcmp )
				chip8->PC = chip8->PC + 2;
		}
		else if( ( instruction & 0xF00F ) == 0x5000 ) // SE Vx, Vy
		{
			short regA = ( instruction & 0x0F00 ) >> 8;
			short regB = ( instruction & 0x00F0 ) >> 4;

			Chip8_validate_registers(chip8, regA, 1);
			Chip8_validate_registers(chip8, regB, 1);
			if (chip8->ERROR) break;

			if (chip8->registers.by_index[regA] == chip8->registers.by_index[regB])
				chip8->PC = chip8->PC + 2;
		}
		else if( (instruction & 0xF000) == 0x6000 ) // LD Vx, byte
		{
			short regindex = ( instruction & 0x0F00 ) >> 8;

			Chip8_validate_registers(chip8, regindex, 1);
			if (chip8->ERROR) break;

			short regvalue = ( instruction & 0x00FF );
			chip8->registers.by_index[regindex] = (u8)regvalue;
		}
		else if( (instruction & 0xF000) == 0x7000 ) // ADD Vx, byte
		{
			short regindex = ( instruction & 0x0F00 ) >> 8;

			Chip8_validate_registers(chip8, regindex, 1);
			if (chip8->ERROR) break;

			short regadd = ( instruction & 0x00FF );
			chip8->registers.by_index[regindex] += regadd;
		}
		else if( (instruction & 0xF00F) == 0x8000 ) // LD Vx, Vy
		{
			short regA = ( instruction & 0x0F00 ) >> 8;
			short regB = ( instruction & 0x00F0 ) >> 4;

			Chip8_validate_registers(chip8, regA, 1);
			Chip8_validate_registers(chip8, regB, 1);
			if (chip8->ERROR) break;

			chip8->registers.by_index[regA] = chip8->registers.by_index[regB];
		}
		else if( (instruction & 0xF00F) == 0x8001 ) // OR Vx, Vy
		{
			short regA = ( instruction & 0x0F00 ) >> 8;
			short regB = ( instruction & 0x00F0 ) >> 4;

			Chip8_validate_registers(chip8, regA, 1);
			Chip8_validate_registers(chip8, regB, 1);
			if (chip8->ERROR) break;

			chip8->registers.by_index[regA] |= chip8->registers.by_index[regB];
		}
		else if( (instruction & 0xF00F) == 0x8002 ) // AND Vx, Vy
		{
			short regA = ( instruction & 0x0F00 ) >> 8;
			short regB = ( instruction & 0x00F0 ) >> 4;

			Chip8_validate_registers(chip8, regA, 1);
			Chip8_validate_registers(chip8, regB, 1);
			if (chip8->ERROR) break;

			chip8->registers.by_index[regA] &= chip8->registers.by_index[regB];
		}
		else if( (instruction & 0xF00F) == 0x8003 ) // XOR Vx, Vy
		{
			short regA = ( instruction & 0x0F00 ) >> 8;
			short regB = ( instruction & 0x00F0 ) >> 4;

			Chip8_validate_registers(chip8, regA, 1);
			Chip8_validate_registers(chip8, regB, 1);
			if (chip8->ERROR) break;

			chip8->registers.by_index[regA] ^= chip8->registers.by_index[regB];
		}
		else if( (instruction & 0xF00F) == 0x8004 ) // ADD Vx, Vy
		{
			short regA = ( instruction & 0x0F00 ) >> 8;
			short regB = ( instruction & 0x00F0 ) >> 4;

			Chip8_validate_registers(chip8, regA, 1);
			Chip8_validate_registers(chip8, regB, 1);
			if (chip8->ERROR) break;

			short add = chip8->registers.by_index[regA] + chip8->registers.by_index[regB];
			chip8->registers.VF = add > 255 ? 1 : 0;
			chip8->registers.by_index[regA] = (u8)add;
		}
		else if( (instruction & 0xF00F) == 0x8005 ) // SUB Vx, Vy
		{
			short regA = ( instruction & 0x0F00 ) >> 8;
			short regB = ( instruction & 0x00F0 ) >> 4;

			Chip8_validate_registers(chip8, regA, 1);
			Chip8_validate_registers(chip8, regB, 1);
			if (chip8->ERROR) break;

			chip8->registers.VF = chip8->registers.by_index[regA] > chip8->registers.by_index[regB] ? 1 : 0;
			chip8->registers.by_index[regA] -= chip8->registers.by_index[regB];
		}
		else if( (instruction & 0xF00F) == 0x8006 ) // SHR Vx {, Vy}
		{
			short regindex = ( instruction & 0x0F00 ) >> 8;

			Chip8_validate_registers(chip8, regindex, 1);
			if (chip8->ERROR) break;

			chip8->registers.VF = chip8->registers.by_index[regindex] & 0x01;
			chip8->registers.by_index[regindex] >>= 1;
		}
		else if( (instruction & 0xF00F) == 0x8007 ) // SUBN Vx, Vy
		{
			short regA = ( instruction & 0x0F00 ) >> 8;
			short regB = ( instruction & 0x00F0 ) >> 4;

			Chip8_validate_registers(chip8, regA, 1);
			Chip8_validate_registers(chip8, regB, 1);
			if (chip8->ERROR) break;

			chip8->registers.VF = chip8->registers.by_index[regB] > chip8->registers.by_index[regA] ? 1 : 0;
			chip8->registers.by_index[regA] = chip8->registers.by_index[regB] - chip8->registers.by_index[regA];
		}
		else if( (instruction & 0xF00F) == 0x800E ) // SHL Vx {, Vy}
		{
			short regindex = ( instruction & 0x0F00 ) >> 8;

			Chip8_validate_registers(chip8, regindex, 1);
			if (chip8->ERROR) break;

			chip8->registers.VF = (chip8->registers.by_index[regindex] & 0x80) >> 7;
			chip8->registers.by_index[regindex] <<= 1;
		}
		else if( (instruction & 0xF00F) == 0x9000 ) // SNE Vx, Vy
		{
			short regA = ( instruction & 0x0F00 ) >> 8;
			short regB = ( instruction & 0x00F0 ) >> 4;

			Chip8_validate_registers(chip8, regA, 1);
			Chip8_validate_registers(chip8, regB, 1);
			if (chip8->ERROR) break;

			if (chip8->registers.by_index[regA] != chip8->registers.by_index[regB])
				chip8->PC = chip8->PC + 2;
		}
		else if( ( instruction & 0xF000 ) == 0xA000 ) // LD I, addr
		{
			short regvalue = ( instruction & 0x0FFF );
			chip8->I = regvalue;
		}
		else if( ( instruction & 0xF000 ) == 0xB000 ) // JP V0, addr
		{
			short regvalue = (instruction & 0x0FFF);

			short new_PC = regvalue + chip8->registers.V0;

			Chip8_validate_memory(chip8, new_PC, 2);
			if (chip8->ERROR) break;

			chip8->PC = new_PC;
		}
		else if( ( instruction & 0xF000 ) == 0xC000 ) // RND Vx, byte
		{
			short regindex = (instruction & 0x0F00) >> 8;
			short regvalue = (instruction & 0x00FF);

			Chip8_validate_registers(chip8, regindex, 1);
			if (chip8->ERROR) break;

			u8 random_byte = random_char();
			chip8->registers.by_index[regindex] = random_byte & (u8)regvalue;
		}
		else if( ( instruction & 0xF000 ) == 0xD000 ) // DRW Vx, Vy, nibble
		{
			short regx = (instruction & 0x0F00) >> 8;
			short regy = (instruction & 0x00F0) >> 4;
			short n = (instruction & 0x000F);

			Chip8_validate_registers(chip8, regx, 1);
			Chip8_validate_registers(chip8, regy, 1);
			if (chip8->ERROR) break;

			u8 x = chip8->registers.by_index[regx];
			u8 y = chip8->registers.by_index[regy];

			if (x >= chip8->screen_width || y >= chip8->screen_height || n >= chip8->screen_height)
				chip8->ERROR = Chip8::SCREEN_COORD_INCORRECT;
			Chip8_validate_memory(chip8, chip8->I, n);
			if (chip8->ERROR) break;

			// 3-byte sprite that usually looks like:
			// [A][A']
			// [B][B']
			//
			// on screen at (21, 2):
			// [B'] 11111000 00000000 00000111 [B}
			//      00000000 00000000 00000000
			//      11111000 00000000 00000111
			// [A'] 11111000 00000000 00000111 [A]
			//
			// and on screen at (13, 2):
			//               [B] [B']
			// 00000000 00000111 11111000
			// 00000000 00000000 00000000 
			// 00000000 00000111 11111000 
			// 00000000 00000111 11111000
			//               [A] [A']

			short AB_byte_x = x / 8;
			short AB_bitstart = x % 8;

			short ABdash_byte_x = (AB_byte_x + 1) % (chip8->screen_width / 8);
			short ABdash_bitcount = 8 - AB_bitstart;

			short A_start_y = y;
			short A_height = min((int)n, chip8->screen_height - y);
			
			short B_start_y = 0;
			short B_height = n - A_height;

			u8* src = Chip8_get_memory(chip8, chip8->I);
			u8 erasure = 0;

			for (int iy = 0; iy != A_height; ++iy){
				int SCREENindex = AB_byte_x * chip8->screen_height + A_start_y + iy;
				ram_assert(SCREENindex < sizeof(Chip8::SCREEN));

				u8 SCREENbyte = chip8->SCREEN[SCREENindex];
				u8 SRCbyte = src[iy] >> AB_bitstart;
				u8 new_SCREENbyte = SCREENbyte ^ SRCbyte;

				erasure |= (SCREENbyte & ~new_SCREENbyte) ? 1 : 0;

				chip8->SCREEN[SCREENindex] = new_SCREENbyte;
			}

			for (int iy = 0; iy != B_height; ++iy){
				int SCREENindex = AB_byte_x * chip8->screen_height + B_start_y + iy;
				ram_assert(SCREENindex < sizeof(Chip8::SCREEN));

				u8 SCREENbyte = chip8->SCREEN[SCREENindex];
				u8 SRCbyte = src[A_height + iy] >> AB_bitstart;
				u8 new_SCREENbyte = SCREENbyte ^ SRCbyte;

				erasure |= (SCREENbyte & ~new_SCREENbyte) ? 1 : 0;

				chip8->SCREEN[SCREENindex] = new_SCREENbyte;
			}

			if (ABdash_bitcount != 8){
				for (int iy = 0; iy != A_height; ++iy){
					int SCREENindex = ABdash_byte_x * chip8->screen_height + A_start_y + iy;
					ram_assert(SCREENindex < sizeof(Chip8::SCREEN));

					u8 SCREENbyte = chip8->SCREEN[SCREENindex];
					u8 SRCbyte = src[iy] << ABdash_bitcount;
					u8 new_SCREENbyte = SCREENbyte ^ SRCbyte;

					erasure |= (SCREENbyte & ~new_SCREENbyte) ? 1 : 0;

					chip8->SCREEN[SCREENindex] = new_SCREENbyte;
				}

				for (int iy = 0; iy != B_height; ++iy){
					int SCREENindex = ABdash_byte_x * chip8->screen_height + B_start_y + iy;
					ram_assert(SCREENindex < sizeof(Chip8::SCREEN));

					u8 SCREENbyte = chip8->SCREEN[SCREENindex];
					u8 SRCbyte = src[A_height + iy] << ABdash_bitcount;
					u8 new_SCREENbyte = SCREENbyte ^ SRCbyte;

					erasure |= (SCREENbyte & ~new_SCREENbyte) ? 1 : 0;

					chip8->SCREEN[SCREENindex] = new_SCREENbyte;
				}
			}

			chip8->registers.VF = erasure;
		}
		else if( ( instruction & 0xF0FF ) == 0xE09E ) // SKP Vx
		{
			short regindex = (instruction & 0x0F00) >> 8;

			Chip8_validate_registers(chip8, regindex, 1);
			if (chip8->ERROR) break;

			u8 keyindex = chip8->registers.by_index[regindex];

			if( keyindex > sizeof( Chip8::KEYBOARD ) )
			{
				chip8->ERROR = Chip8::KEY_UNKNOWN;
				break;
			}

			if( chip8->KEYBOARD[keyindex] )
				chip8->PC += 2;
		}
		else if( ( instruction & 0xF0FF ) == 0xE0A1 ) // SKNP Vx
		{
			short regindex = (instruction & 0x0F00) >> 8;

			Chip8_validate_registers(chip8, regindex, 1);
			if (chip8->ERROR) break;

			u8 keyindex = chip8->registers.by_index[regindex];

			if( keyindex >= sizeof( Chip8::KEYBOARD ) )
			{
				chip8->ERROR = Chip8::KEY_UNKNOWN;
				break;
			}

			if (!chip8->KEYBOARD[keyindex])
				chip8->PC += 2;
		}
		else if( ( instruction & 0xF0FF ) == 0xF007 ) // LD Vx, DT
		{
			short regindex = ( instruction & 0x0F00 ) >> 8;

			Chip8_validate_registers(chip8, regindex, 1);
			if( chip8->ERROR ) break;

			chip8->registers.by_index[regindex] = chip8->DT;
		}
		else if( ( instruction & 0xF0FF ) == 0xF00A ) // LD Vx, K
		{
			short regindex = ( instruction & 0x0F00 ) >> 8;

			Chip8_validate_registers(chip8, regindex, 1);
			if( chip8->ERROR ) break;

			int keypress = -1;
			for (int ikey = 0; ikey != carray_size(Chip8::KEYBOARD); ++ikey){
				if (!chip8->KEYBOARD[ikey] && chip8->LAST_KEYBOARD[ikey]){
					keypress = ikey;
					break;
				}
			}

			if (keypress != -1)
				chip8->registers.by_index[regindex] = keypress;
			else
				chip8->PC -= 2; // rewing the instruction to wait
		}
		else if( ( instruction & 0xF0FF ) == 0xF015 ) // LD DT, Vx
		{
			short regindex = ( instruction & 0x0F00 ) >> 8;

			Chip8_validate_registers(chip8, regindex, 1);
			if (chip8->ERROR) break;

			chip8->DT = chip8->registers.by_index[regindex];
		}
		else if( ( instruction & 0xF0FF ) == 0xF018 ) // LD ST, Vx
		{
			short regindex = ( instruction & 0x0F00 ) >> 8;

			Chip8_validate_registers(chip8, regindex, 1);
			if (chip8->ERROR) break;

			chip8->ST = chip8->registers.by_index[regindex];
		}
		else if( ( instruction & 0xF0FF ) == 0xF01E ) // ADD I, Vx
		{
			short regindex = ( instruction & 0x0F00 ) >> 8;

			Chip8_validate_registers(chip8, regindex, 1);
			if (chip8->ERROR) break;

			chip8->I += chip8->registers.by_index[regindex];
		}
		else if( ( instruction & 0xF0FF ) == 0xF029 ) // LD F, Vx
		{
			short regindex = ( instruction & 0x0F00 ) >> 8;

			Chip8_validate_registers(chip8, regindex, 1);
			if (chip8->ERROR) break;

			u8 charindex = chip8->registers.by_index[regindex] & 0x0F;
			short addr = (short)charindex * 5;

			if( addr >= sizeof( Chip8::Memory::Interpreter::sprites) )
			{
				chip8->ERROR = Chip8::KEY_UNKNOWN;
				break;
			}

			chip8->I = addr;
		}
		else if( ( instruction & 0xF0FF ) == 0xF033 ) // LD B, VX
		{
			short regindex = ( instruction & 0x0F00 ) >> 8;

			Chip8_validate_registers(chip8, regindex, 1);
			Chip8_validate_memory(chip8, chip8->I, 3);
			if (chip8->ERROR) break;

			u8 byte = chip8->registers.by_index[regindex];

			u8* memptr = Chip8_get_memory( chip8, chip8->I );

			*memptr = byte / 100;
			++memptr;
			*memptr = (byte % 100) / 10;
			++memptr;
			*memptr = (byte % 10);
		}
		else if( ( instruction & 0xF0FF ) == 0xF055 ) // LD [I], Vx
		{
			short regcount = ( instruction & 0x0F00 ) >> 8;

			Chip8_validate_registers( chip8, 0, regcount + 1 );
			Chip8_validate_memory( chip8, chip8->I, regcount + 1 );
			if( chip8->ERROR ) break;

			++regcount;	

			u8* memptr = Chip8_get_memory( chip8, chip8->I );

			for (int ireg = 0; ireg != regcount; ++ireg)
				memptr[ireg] = chip8->registers.by_index[ireg];
		}
		else if( ( instruction & 0xF0FF ) == 0xF065 ) // LD Vx, [I]
		{
			short regcount = ( instruction & 0xF00 ) >> 8;

			Chip8_validate_registers( chip8, 0, regcount + 1 );
			Chip8_validate_memory( chip8, chip8->I, regcount + 1 );
			if( chip8->ERROR ) break;

			++regcount;

			u8* memptr = Chip8_get_memory( chip8, chip8->I );

			for( int ireg = 0; ireg != regcount; ++ireg )
				chip8->registers.by_index[ireg] = memptr[ireg];
		}
		else
		{
			chip8->ERROR = Chip8::INSTRUCTION_UNKNOWN;
			break;
		}

		memcpy(chip8->LAST_KEYBOARD, chip8->KEYBOARD, sizeof(Chip8::KEYBOARD));

		--instruction_count;
	}
}

void Chip8_to_screen(Chip8* chip8, Pixel_Canvas& screen){
	RGBA color_on;
	color_on.r = 255;
	color_on.g = 255;
	color_on.b = 255;

	RGBA color_off;
	color_off.r = 0;
	color_off.g = 0;
	color_off.b = 0;

	for (int ix = 0; ix != chip8->screen_width; ++ix){
		int byte_x = ix / 8 * chip8->screen_height;
		int bit_x = ix % 8;

		for (int iy = 0; iy != chip8->screen_height; ++iy){
			int SCREENindex = byte_x + iy;

			u8 pixel = (chip8->SCREEN[SCREENindex] >> (7 - bit_x)) & 0x01;
			screen.set_pixel(ix, screen.height - 1 - iy, pixel ? color_on : color_off);
		}
	}
}

struct Game{
	static constexpr int update_per_second = 60;
	
	Window* window;
	int min_window_ratio;
	int window_ratio;

	Frame_Controller controller; 

	Input::Listener* listener;

	Chip8 chip8;
	Pixel_Canvas screen;

	Audio_DSP* DSP;
};

static Game* g_game;

void game_create(){
	Game* game = (Game*)malloc(sizeof(Game));
	game->window = NULL;
	game->listener = NULL;
	game->DSP = NULL;

	// Chip8

	if( g_argc < 2 ) crash("No argument !");
	void* chip8_ROM;
	size_t chip8_ROM_size;
	g_file_system->ReadFile( g_argv[1], chip8_ROM, chip8_ROM_size );
	Chip8_create(&game->chip8, chip8_ROM, chip8_ROM_size);

	// window

	Window* window = g_window_manager->create_window();
	game->window = window;

	window->set_title(ram_project);
	window->disable_resizing();

	int current_width, current_height;
	window->get_size(current_width, current_height);

	window->set_size(0, 0);

	int minx, miny;
	window->get_size(minx, miny);

	int min_ratiox = minx / game->chip8.screen_width + ((minx % game->chip8.screen_width) ? 1 : 0);
	int min_ratioy = miny / game->chip8.screen_height + ((miny % game->chip8.screen_height) ? 1 : 0);
	int min_ratio = max(1, max(min_ratiox, min_ratioy));
	game->min_window_ratio = min_ratio;

	int ratiox = current_width / game->chip8.screen_width + ((current_width % game->chip8.screen_width) ? 1 : 0);
	int ratioy = current_height / game->chip8.screen_height + ((current_height % game->chip8.screen_height) ? 1 : 0);
	int ratio = max(1, max(ratiox, ratioy));
	game->window_ratio = ratio;

	window->set_size(game->chip8.screen_width * ratio, game->chip8.screen_height * ratio);

	// frame controller

	game->controller.create();
	game->controller.step_multiplicity = 1;
	game->controller.step_maximum = 4;
	game->controller.tick_per_step = g_timer->ticks_per_second() / Game::update_per_second;
	game->controller.tick_snapping_error = 0.01;
	game->controller.add_snapping_frequency(60);
	game->controller.add_snapping_frequency(120);
	game->controller.add_snapping_frequency(30);

	Input::Listener* listener = g_input->create_listener();
	listener->device_type = Input::Device_Keyboard;
	listener->pairing_mode = Input::Pairing_Most_Recent_Persistent;

	listener->register_action("1", Input::Control_Button, RAMKey_to_scancode(RAMK_1));
	listener->register_action("2", Input::Control_Button, RAMKey_to_scancode(RAMK_2));
	listener->register_action("3", Input::Control_Button, RAMKey_to_scancode(RAMK_3));
	listener->register_action("C", Input::Control_Button, RAMKey_to_scancode(RAMK_4));
	listener->register_action("4", Input::Control_Button, RAMKey_to_scancode(RAMK_Q));
	listener->register_action("5", Input::Control_Button, RAMKey_to_scancode(RAMK_W));
	listener->register_action("6", Input::Control_Button, RAMKey_to_scancode(RAMK_E));
	listener->register_action("D", Input::Control_Button, RAMKey_to_scancode(RAMK_R));
	listener->register_action("7", Input::Control_Button, RAMKey_to_scancode(RAMK_A));
	listener->register_action("8", Input::Control_Button, RAMKey_to_scancode(RAMK_S));
	listener->register_action("9", Input::Control_Button, RAMKey_to_scancode(RAMK_D));
	listener->register_action("E", Input::Control_Button, RAMKey_to_scancode(RAMK_F));
	listener->register_action("A", Input::Control_Button, RAMKey_to_scancode(RAMK_Z));
	listener->register_action("0", Input::Control_Button, RAMKey_to_scancode(RAMK_X));
	listener->register_action("B", Input::Control_Button, RAMKey_to_scancode(RAMK_C));
	listener->register_action("F", Input::Control_Button, RAMKey_to_scancode(RAMK_V));
	
	game->listener = listener;

	// screen

	game->screen.create();
	game->screen.set_resolution(game->chip8.screen_width, game->chip8.screen_height);

	// audio

	LFO_Data data;
	data.period_cursor = 0.f;

	Audio_DSP* DSP = g_audio->create_DSP(sizeof(LFO_Param), &data, sizeof(LFO_Data));
	game->DSP = DSP;

	DSP->process = LFO_Processor;

	LFO_Param* param = (LFO_Param*)DSP->get_param();
	param->pause = true;
	param->set_frequency(440);
	DSP->commit_param();

	g_audio->activate_DSP(DSP);

	g_game = game;
}

int game_update(){
	if (!g_game) return 1;

	char state[16];

	Input::Control_Data action;
	action = g_game->listener->get_action_status("0");
	g_game->chip8.KEYBOARD[0] = action.button.down;
	state[0] = '0' + action.button.down;
	action = g_game->listener->get_action_status("1");
	g_game->chip8.KEYBOARD[1] = action.button.down;
	state[1] = '0' + action.button.down;
	action = g_game->listener->get_action_status("2");
	g_game->chip8.KEYBOARD[2] = action.button.down;
	state[2] = '0' + action.button.down;
	action = g_game->listener->get_action_status("3");
	g_game->chip8.KEYBOARD[3] = action.button.down;
	state[3] = '0' + action.button.down;
	action = g_game->listener->get_action_status("4");
	g_game->chip8.KEYBOARD[4] = action.button.down;
	state[4] = '0' + action.button.down;
	action = g_game->listener->get_action_status("5");
	g_game->chip8.KEYBOARD[5] = action.button.down;
	state[5] = '0' + action.button.down;
	action = g_game->listener->get_action_status("6");
	g_game->chip8.KEYBOARD[6] = action.button.down;
	state[6] = '0' + action.button.down;
	action = g_game->listener->get_action_status("7");
	g_game->chip8.KEYBOARD[7] = action.button.down;
	state[7] = '0' + action.button.down;
	action = g_game->listener->get_action_status("8");
	g_game->chip8.KEYBOARD[8] = action.button.down;
	state[8] = '0' + action.button.down;
	action = g_game->listener->get_action_status("9");
	g_game->chip8.KEYBOARD[9] = action.button.down;
	state[9] = '0' + action.button.down;
	action = g_game->listener->get_action_status("A");
	g_game->chip8.KEYBOARD[10] = action.button.down;
	state[10] = '0' + action.button.down;
	action = g_game->listener->get_action_status("B");
	g_game->chip8.KEYBOARD[11] = action.button.down;
	state[11] = '0' + action.button.down;
	action = g_game->listener->get_action_status("C");
	g_game->chip8.KEYBOARD[12] = action.button.down;
	state[12] = '0' + action.button.down;
	action = g_game->listener->get_action_status("D");
	g_game->chip8.KEYBOARD[13] = action.button.down;
	state[13] = '0' + action.button.down;
	action = g_game->listener->get_action_status("E");
	g_game->chip8.KEYBOARD[14] = action.button.down;
	state[14] = '0' + action.button.down;
	action = g_game->listener->get_action_status("F");
	g_game->chip8.KEYBOARD[15] = action.button.down;
	state[15] = '0' + action.button.down;

	if( memcmp( g_game->chip8.KEYBOARD, g_game->chip8.LAST_KEYBOARD, sizeof( Chip8::KEYBOARD ) ) )
		ram_info( "KEYBOARD: %.16s", state );

	float dtime_sec = 1.f / (float)Game::update_per_second;
	u64 time = g_timer->ticks();
	int step_count = g_game->controller.update_time(time);
	for (int istep = 0; istep != step_count; ++istep){
		Chip8_step(&g_game->chip8, dtime_sec);
		if (g_game->chip8.ERROR){
			ram_error("Chip8 ERROR: %d", g_game->chip8.ERROR);
			break;
		}
	}

	LFO_Param* param = (LFO_Param*)g_game->DSP->get_param();
	param->pause = g_game->chip8.ST > 0 ? false : true;
	g_game->DSP->commit_param();
	
	if (g_game->window->user_requested_close) return true;
	else return false;
}

void game_render(){
	if( !g_game ) return;

	RGBA color_none;
	color_none.r = 0xF5;
	color_none.g = 0x42;
	color_none.b = 0x72;
	color_none.a = 0xFF;
	g_game->screen.clear(color_none);

	Chip8_to_screen(&g_game->chip8, g_game->screen);

	copy_image_to_window(
		g_game->screen.width,
		g_game->screen.height,
		g_game->screen.canvas,
		g_game->window
	);
}

void game_destroy(){
	if( !g_game ) return;
	g_audio->deactivate_DSP(g_game->DSP);

	Chip8_destroy(&g_game->chip8);

	g_game->screen.destroy();

	free(g_game);
	g_game = NULL;
}

void (*g_game_create)() = game_create;
int (*g_game_update)() = game_update;
void (*g_game_render)() = game_render;
void (*g_game_destroy)() = game_destroy;
