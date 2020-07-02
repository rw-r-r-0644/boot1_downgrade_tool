/*
 *  minute - a port of the "mini" IOS replacement for the Wii U.
 *
 *  Copyright (C) 2016          SALT
 *  Copyright (C) 2016          Daz Jones <daz@dazzozo.com>
 *
 *  Copyright (C) 2008, 2009    Sven Peter <svenpeter@gmail.com>
 *
 *  This code is licensed to you under the terms of the GNU GPL, version 2;
 *  see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
 */

#include "common/types.h"
#include "common/utils.h"
#include "system/latte.h"
#include "system/gpio.h"

#define eeprom_delay() udelay(5)

void send_bits(int b, int bits)
{
    while(bits--)
    {
        if(b & (1 << bits))
            set32(LT_GPIO_OUT, GP_EEP_MOSI);
        else
            clear32(LT_GPIO_OUT, GP_EEP_MOSI);
        eeprom_delay();
        set32(LT_GPIO_OUT, GP_EEP_CLK);
        eeprom_delay();
        clear32(LT_GPIO_OUT, GP_EEP_CLK);
        eeprom_delay();
    }
}

int recv_bits(int bits)
{
    int res = 0;
    while(bits--)
    {
        res <<= 1;
        set32(LT_GPIO_OUT, GP_EEP_CLK);
        eeprom_delay();
        clear32(LT_GPIO_OUT, GP_EEP_CLK);
        eeprom_delay();
        res |= !!(read32(LT_GPIO_IN) & GP_EEP_MISO);
    }
    return res;
}

int seeprom_read(void *dst, int offset, int count)
{
    int i;
    u16 *ptr = (u16 *)dst;
    u16 recv;

    clear32(LT_GPIO_OUT, GP_EEP_CLK);
    clear32(LT_GPIO_OUT, GP_EEP_CS);
    eeprom_delay();

    for(i = 0; i < count; ++i)
    {
        set32(LT_GPIO_OUT, GP_EEP_CS);
        send_bits((0x600 | (offset + i)), 11);
        recv = recv_bits(16);
        *ptr++ = recv;
        clear32(LT_GPIO_OUT, GP_EEP_CS);
        eeprom_delay();
    }

    return count;
}

int seeprom_write(void *buf, int offset, int count)
{
	int i, j;
	u16 *ptr = (u16 *)buf;

	clear32(LT_GPIO_OUT, GP_EEP_CLK);
	clear32(LT_GPIO_OUT, GP_EEP_CS);
	eeprom_delay();

	/* Send EWEN instruction to enable writing
	 * SB = 1, Op = 00, addr(x16) = 11XXXXXX */
	set32(LT_GPIO_OUT, GP_EEP_CS);
	send_bits((0b1 << 10) | (0b00 << 8) | 0b11000000, 11);
	clear32(LT_GPIO_OUT, GP_EEP_CS);
	eeprom_delay();

	/* Write data to the SEEPROM */
	for(i = 0; i < count; ++i)
	{
		u8 addr = (offset + i) & 0xff;
		u16 data = (ptr[i] & 0xffff);

		/* Send WRITE instruction to write to memory location
		 * SB = 1, Op = 01, addr(x16) = A7-A0, data(x16) = D15-D0 */
		set32(LT_GPIO_OUT, GP_EEP_CS);
		send_bits((0b1 << 26) | (0b01 << 24) | (addr << 16) | data, 27);
		clear32(LT_GPIO_OUT, GP_EEP_CS);
		eeprom_delay();

		/* Wait for the command to complete */
		set32(LT_GPIO_OUT, GP_EEP_CS);
		for (j = 0; j < 100; j++) {
			if (recv_bits(10) & 1) {
				break;
			}
		}
		clear32(LT_GPIO_OUT, GP_EEP_CS);
		eeprom_delay();
	}

	/* Send EWDS instructiont to disable writing
	 * SB = 1, Op = 00, addr(x16) = 00XXXXXX */
	set32(LT_GPIO_OUT, GP_EEP_CS);
	send_bits((0b1 << 10) | (0b00 << 8) | 0b00000000, 11);
	clear32(LT_GPIO_OUT, GP_EEP_CS);
	eeprom_delay();

	/* Attempt to read the data to check if the write
	 * completed successfully */
	for(i = 0; i < count; ++i)
	{
		u16 data;
		seeprom_read(&data, offset + i, 1);
		if (ptr[i] != data) {
			return -1;
		}
	}

	/* Write completed successfully */
	return count;
}
