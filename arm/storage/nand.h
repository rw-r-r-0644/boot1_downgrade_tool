/*
 *  minute - a port of the "mini" IOS replacement for the Wii U.
 *
 *  Copyright (C) 2016          SALT
 *  Copyright (C) 2016          Daz Jones <daz@dazzozo.com>
 *
 *  Copyright (C) 2008, 2009    Hector Martin "marcan" <marcan@marcansoft.com>
 *  Copyright (C) 2008, 2009    Sven Peter <svenpeter@gmail.com>
 *
 *  This code is licensed to you under the terms of the GNU GPL, version 2;
 *  see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
 */

#ifndef __NAND_H__
#define __NAND_H__

#include "common/types.h"

#define NAND_ADDR_NONE  ((void*)~0)

#define PAGE_SIZE       2048
#define PAGE_SPARE_SIZE     64
#define ECC_BUFFER_SIZE     (PAGE_SPARE_SIZE+16)
#define ECC_BUFFER_ALLOC    (PAGE_SPARE_SIZE+32)
#define BLOCK_SIZE      64
#define NAND_MAX_PAGE       0x40000

#define NAND_BANK_SLCCMPT 0x00000001
#define NAND_BANK_SLC 0x00000002

#define NAND_CMD_EXEC (1<<31)

void nand_irq(void);

void nand_send_command(u32 command, u32 bitmask, u32 flags, u32 num_bytes);
int nand_reset(u32 bank);
void nand_get_id(u8 *);
void nand_get_status(u8 *);
void nand_read_page(u32 pageno, void *data, void *ecc);
void nand_write_page(u32 pageno, void *data, void *ecc);
void nand_write_page_ecc(u32 pageno, void *ecc);
void nand_erase_block(u32 pageno);
void nand_wait(void);

#define NAND_ECC_OK 0
#define NAND_ECC_CORRECTED 1
#define NAND_ECC_UNCORRECTABLE -1

int nand_correct(u32 pageno, void *data, void *ecc);
void nand_initialize(u32 bank);

#endif
