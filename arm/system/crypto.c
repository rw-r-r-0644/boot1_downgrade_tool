/*
 *  minute - a port of the "mini" IOS replacement for the Wii U.
 *
 *  Copyright (C) 2016          SALT
 *  Copyright (C) 2016          Daz Jones <daz@dazzozo.com>
 *
 *  Copyright (C) 2008, 2009    Haxx Enterprises <bushing@gmail.com>
 *  Copyright (C) 2008, 2009    Sven Peter <svenpeter@gmail.com>
 *
 *  This code is licensed to you under the terms of the GNU GPL, version 2;
 *  see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
 */

#include "crypto.h"
#include "system/latte.h"
#include "common/utils.h"
#include "system/memory.h"
#include "system/irq.h"
#include "video/gfx.h"
#include "string.h"
#include "seeprom.h"

#define     AES_CMD_RESET   0
#define     AES_CMD_ENCRYPT 0x9000
#define     AES_CMD_DECRYPT 0x9800

static u32 crc32_table[0x100];

void crypto_make_crc32_table(void)
{
    u32 n, c, k;

    for (n = 0; n < 0x100; n++) {
        c = n;
        for (k = 0; k < 8; k++) {
            c = ((c & 1) ? 0xedb88320 : 0) ^ (c >> 1);
        }
        crc32_table[n] = c;
    }
}

otp_t otp;
seeprom_t seeprom;

void crypto_read_otp(void)
{
    u32 *otpd = (u32*)&otp;
    int word, bank;
    for (bank = 0; bank < 8; bank++)
    {
        for (word = 0; word < 0x20; word++)
        {
            write32(LT_OTPCMD, 0x80000000 | bank << 8 | word);
            *otpd++ = read32(LT_OTPDATA);
        }
    }
}

void crypto_read_seeprom(void)
{
    seeprom_read(&seeprom, 0, sizeof(seeprom) / 2);
}

void crypto_initialize(void)
{
    crypto_make_crc32_table();
    crypto_read_otp();
    crypto_read_seeprom();
    write32(AES_CTRL, 0);
    while (read32(AES_CTRL) != 0);
    irq_enable(IRQ_AES);
}

u32 crc32_compute(u8 *buf, u32 len)
{
    u32 c = 0xffffffff;
    int n;

    for (n = 0; n < len; n++) {
        c = crc32_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
    }

    return c ^ 0xffffffff;
}

void seeprom_read_encrypted_banks(u32 index, u8 *buf, u32 count)
{
	u8 en_buf[0x10];
	int i;

	aes_reset();
	aes_set_key(otp.seeprom_key);
	aes_empty_iv();

	for (i = 0; i < count; i++) {
		seeprom_read(en_buf, (index + i) * 8, 8);
		aes_decrypt(en_buf, en_buf, 1, 0);
		memcpy(&buf[0x10 * i], en_buf, 0x10);
	}
}

int seeprom_write_encrypted_banks(u32 index, u8 *buf, u32 count)
{
	u8 en_buf[0x10];
	int i;

	aes_reset();
	aes_set_key(otp.seeprom_key);
	aes_empty_iv();
	
	for (i = 0; i < count; i++) {
		memcpy(en_buf, &buf[0x10 * i], 0x10);
		aes_encrypt(en_buf, en_buf, 1, 0);
		if (seeprom_write(en_buf, (index + i) * 8, 8) != 8) {
			return -1;
		}
	}

	return count;
}

static int _aes_irq = 0;

void aes_irq(void)
{
    _aes_irq = 1;
}

static inline void aes_command(u16 cmd, u8 iv_keep, u32 blocks)
{
    if (blocks != 0)
        blocks--;
    _aes_irq = 0;
    write32(AES_CTRL, (cmd << 16) | (iv_keep ? 0x1000 : 0) | (blocks&0x7f));
    while (read32(AES_CTRL) & 0x80000000);
}

void aes_reset(void)
{
    write32(AES_CTRL, 0);
    while (read32(AES_CTRL) != 0);
}

void aes_set_iv(u8 *iv)
{
    int i;
    for(i = 0; i < 4; i++) {
        write32(AES_IV, *(u32 *)iv);
        iv += 4;
    }
}

void aes_empty_iv(void)
{
    int i;
    for(i = 0; i < 4; i++)
        write32(AES_IV, 0);
}

void aes_set_key(u8 *key)
{
    int i;
    for(i = 0; i < 4; i++) {
        write32(AES_KEY, *(u32 *)key);
        key += 4;
    }
}

void aes_decrypt(u8 *src, u8 *dst, u32 blocks, u8 keep_iv)
{
    dc_flushrange(src, blocks * 16);
    dc_invalidaterange(dst, blocks * 16);
    ahb_flush_to(RB_AES);

    int this_blocks = 0;
    while(blocks > 0) {
        this_blocks = blocks;
        if (this_blocks > 0x80)
            this_blocks = 0x80;

        write32(AES_SRC, dma_addr(src));
        write32(AES_DEST, dma_addr(dst));

        aes_command(AES_CMD_DECRYPT, keep_iv, this_blocks);

        blocks -= this_blocks;
        src += this_blocks<<4;
        dst += this_blocks<<4;
        keep_iv = 1;
    }

    ahb_flush_from(WB_AES);
    ahb_flush_to(RB_IOD);
}

void aes_encrypt(u8 *src, u8 *dst, u32 blocks, u8 keep_iv)
{
    dc_flushrange(src, blocks * 16);
    dc_invalidaterange(dst, blocks * 16);
    ahb_flush_to(RB_AES);
    
    int this_blocks = 0;
    while(blocks > 0) {
        this_blocks = blocks;
        if (this_blocks > 0x80)
            this_blocks = 0x80;
        
        write32(AES_SRC, dma_addr(src));
        write32(AES_DEST, dma_addr(dst));
        
        aes_command(AES_CMD_ENCRYPT, keep_iv, this_blocks);
        
        blocks -= this_blocks;
        src += this_blocks<<4;
        dst += this_blocks<<4;
        keep_iv = 1;
    }
    
    ahb_flush_from(WB_AES);
    ahb_flush_to(RB_IOD);
}
