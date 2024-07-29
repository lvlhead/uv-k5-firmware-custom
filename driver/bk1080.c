/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include "bsp/dp32g030/gpio.h"
#include "bk1080.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/system.h"
#include "misc.h"

#ifndef ARRAY_SIZE
	#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#endif

/*
 * BK1080_RegisterTable decoded
 * REG 00 (0x0008, 0000 0000 0000 1000) // No Use, Default value: 0x0006 (0000 0000 0000 0110)
 * REG 01 (0x1080, 0001 0000 1000 0000) // No Use, Default value: 0x1080 (0001 0000 1000 0000)
 * REG 02 (0x0201, 0000 0010 0000 0001) // Enable (1), Reserved (00000), Disable (0), Reserved (0), Seek (0), SeekUp (1), SkMode (0) Reserved (0), CkSel (0), Mono (0), Mute (0), DsMute (0)
 * REG 03 (0x0000, 0000 0000 0000 0000) // Chan (0000 0000 00), Reserved (00 000), Tune (0)
 * REG 04 (0x40c0, 0100 0000 1100 0000) // GPIO1 (00), GPIO2 (00), GPIO3 (00), BlndAdj (11), Reserved (00), AGCD (0), DE (0), Reserved (0) DEBPS (0), STCIEN (1), Reserved (0)
 * REG 05 (0x0A1F, 0000 1010 0001 1111) // Volume (1111), Space (10), Band (00), SeekTh (0101 1000)
 * REG 06 (0x002E, 0000 0000 0010 1110) // SkCnt (0111), SkSNR (0100), Reserved (0000), SMUTEA (00), SMUTER (00)
 * REG 07 (0x02FF, 0000 0010 1111 1111) // SNR (1111), FreqD (1111 0100 0000)
 * REG 08 (0x5B11, 0101 1011 0001 0001) // Reserved, always write to 0
 * REG 09 (0x0000, 0000 0000 0000 0000) // Reserved. If written, these bits should be read first and then written with their pre-existing values. Do not write during power up
 * REG 10 (0x41E1, 0100 0001 1110 0001) // RSSI (0x41, 1000 0111), ST (1), STEN(0), Reserved (00), AFCRL (0), SFBL (0), STC (1), Reserved (0)
 * REG 11 (0x0000, 0000 0000 0000 0000) // ReadChan(00 0000 0000), IMPC(0000), Reserved (00)
 * REG 12 (0xCE00, 1100 1110 0000 0000) // Reserved, always write to 0
 * REG 13 (0x0000, 0000 0000 0000 0000) // Reserved, always write to 0
 * REG 14 (0x0000, 0000 0000 0000 0000) // Reserved, always write to 0
 * REG 15 (0x1000, 0001 0000 0000 0000) // Reserved, always write to 0
 *
 * Below are internal test register, not visible for user. Initial value and procedure will be provided
separately by BEKEN.
 * REG 16 (0x3197, 0011 0001 1001 0111)
 * REG 17 (0x0000, 0000 0000 0000 0000)
 * REG 18 (0x13FF, 0001 0011 1111 1111)
 * REG 19 (0x9052, 1001 0000 0101 0010)
 * REG 20 (0x0000, 0000 0000 0000 0000)
 * REG 21 (0x0000, 0000 0000 0000 0000)
 * REG 22 (0x0800, 0000 1000 0000 0000)
 * REG 23 (0x0000, 0000 0000 0000 0000)
 * REG 24 (0x51E1, 0101 0001 1110 0001)
 * REG 25 (0xA8BC, 1010 1000 1011 1100)
 * REG 26 (0x2645, 0010 0110 0100 0101)
 * REG 27 (0x00E4, 0000 0000 1110 0100)
 * REG 28 (0x1CD8, 0001 1100 1101 1000)
 * REG 29 (0x3A50, 0011 1010 0101 0000)
 * REG 30 (0xEAE0, 1110 1010 1110 0000)
 * REG 31 (0x3000, 0011 0000 0000 0000)
 * REG 32 (0x0200, 0000 0010 0000 0000)
 * REG 33 (0x0000, 0000 0000 0000 0000)
 */

static const uint16_t BK1080_RegisterTable[] =
{
	0x0008, 0x1080, 0x0201, 0x0000, 0x40C0, 0x0A1F, 0x002E, 0x02FF,
	0x5B11, 0x0000, 0x411E, 0x0000, 0xCE00, 0x0000, 0x0000, 0x1000,
	0x3197, 0x0000, 0x13FF, 0x9852, 0x0000, 0x0000, 0x0008, 0x0000,
	0x51E1, 0xA8BC, 0x2645, 0x00E4, 0x1CD8, 0x3A50, 0xEAE0, 0x3000,
	0x0200, 0x0000,
};

static bool gIsInitBK1080;

uint16_t BK1080_BaseFrequency;
uint16_t BK1080_FrequencyDeviation;

void BK1080_Init0(void)
{
	BK1080_Init(0,0,0);
}

void BK1080_Init(uint16_t freq, uint8_t band, uint8_t space)
{
	unsigned int i;

	if (freq) {
		GPIO_ClearBit(&GPIOB->DATA, GPIOB_PIN_BK1080);

		if (!gIsInitBK1080) {
			for (i = 0; i < ARRAY_SIZE(BK1080_RegisterTable); i++)
				BK1080_WriteRegister(i, BK1080_RegisterTable[i]);

			SYSTEM_DelayMs(250);

			// Internal test register, not visible for user. Initial value and procedure will be provided separately by BEKEN.
			// (0x3CA8, 0011 1100 1010 1000)
			BK1080_WriteRegister(BK1080_REG_25_INTERNAL, 0xA83C);
			// (0xBCA8, 1011 1100 1010 1000)
			BK1080_WriteRegister(BK1080_REG_25_INTERNAL, 0xA8BC);

			SYSTEM_DelayMs(60);

			gIsInitBK1080 = true;
		}
		else {
			// (0x0102, 0000 0001 0000 0010) Power-up Enable (0), Reserved (10000), Disable (0), Reserved (0), Seek (1), SeekUp (0), SKMode (0), Reserved (0), CkSel (0), Mono (0), Mute (0), DsMute (0)
			BK1080_WriteRegister(BK1080_REG_02_POWER_CONFIGURATION, 0x0201);
		}

		// (0x1F0A, 0001 1111 0000 1010) Volume (0101), Space (00), Band (00), SeekTh (1111 1000)
		BK1080_WriteRegister(BK1080_REG_05_SYSTEM_CONFIGURATION2, 0x0A1F);
		BK1080_SetFrequency(freq, band, space);
	}
	else {
		// (0x4102, 0100 0001 0000 0010) Power-up Enable (0), Reserved (10000), Disable (0), Reserved (0), Seek (1), SeekUp (0), SKMode (0), Reserved (0), CkSel (0), Mono (0), Mute (1), DsMute (0)
		BK1080_WriteRegister(BK1080_REG_02_POWER_CONFIGURATION, 0x0241);
		GPIO_SetBit(&GPIOB->DATA, GPIOB_PIN_BK1080);
	}
}

uint16_t BK1080_ReadRegister(BK1080_Register_t Register)
{
	uint8_t Value[2];

	I2C_Start();
	I2C_Write(0x80);
	I2C_Write((Register << 1) | I2C_READ);
	I2C_ReadBuffer(Value, sizeof(Value));
	I2C_Stop();

	return (Value[0] << 8) | Value[1];
}

void BK1080_WriteRegister(BK1080_Register_t Register, uint16_t Value)
{
	I2C_Start();
	I2C_Write(0x80);
	I2C_Write((Register << 1) | I2C_WRITE);
	Value = ((Value >> 8) & 0xFF) | ((Value & 0xFF) << 8);
	I2C_WriteBuffer(&Value, sizeof(Value));
	I2C_Stop();
}

void BK1080_Mute(bool Mute)
{
	// 0x0201 (0000 0010 0000 0001) - Enable (1), Reserved (00000), Disable (0), Reserved (0), Seek (1), SeekUp (0), SkMode (0) Reserved (0), CkSel (0), Mono (0), Mute (0), DsMute (0)
	// 0x4201 (0100 0010 0000 0001) - Enable (1), Reserved (00000), Disable (0), Reserved (0), Seek (1), SeekUp (0), SkMode (0) Reserved (0), CkSel (0), Mono (0), Mute (1), DsMute (0)
	BK1080_WriteRegister(BK1080_REG_02_POWER_CONFIGURATION, Mute ? 0x4201 : 0x0201);
}

void BK1080_SetFrequency(uint16_t frequency, uint8_t band, uint8_t space)
{
	uint8_t spacings[] = {20,10,5};
	space %= 3;

	uint16_t channel = (frequency - BK1080_GetFreqLoLimit(band)) * 10 / spacings[space];

	uint16_t regval = BK1080_ReadRegister(BK1080_REG_05_SYSTEM_CONFIGURATION2);
	regval = (regval & ~(0b11 << 6)) | ((band & 0b11) << 6);
	regval = (regval & ~(0b11 << 4)) | ((space & 0b11) << 4);

	BK1080_WriteRegister(BK1080_REG_05_SYSTEM_CONFIGURATION2, regval);

	BK1080_WriteRegister(BK1080_REG_03_CHANNEL, channel);
	SYSTEM_DelayMs(10);
	BK1080_WriteRegister(BK1080_REG_03_CHANNEL, channel | 0x8000);
}

void BK1080_GetFrequencyDeviation(uint16_t Frequency)
{
	BK1080_BaseFrequency      = Frequency;
	BK1080_FrequencyDeviation = BK1080_ReadRegister(BK1080_REG_07) / 16;
}

uint16_t BK1080_GetFreqLoLimit(uint8_t band)
{
	uint16_t lim[] = {875, 760, 760, 640};
	return lim[band % 4];
}

uint16_t BK1080_GetFreqHiLimit(uint8_t band)
{
	band %= 4;
	uint16_t lim[] = {1080, 1080, 900, 760};
	return lim[band % 4];
}

