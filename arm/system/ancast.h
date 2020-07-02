/*
 *  minute - a port of the "mini" IOS replacement for the Wii U.
 *
 *  Copyright (C) 2016          SALT
 *  Copyright (C) 2016          Daz Jones <daz@dazzozo.com>
 *
 *  This code is licensed to you under the terms of the GNU GPL, version 2;
 *  see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
 */

#ifndef _ANCAST_H
#define _ANCAST_H

#include "common/types.h"
#include "sha.h"

#define ANCAST_MAGIC (0xEFA282D9l)
#define ANCAST_TARGET_IOP (0x02)
#define ANCAST_TARGET_PPC (0x01)

typedef struct
{
    u8 signature[0x38];
    u8 nullpad[0x44];
} PACKED ancast_type1_signature;

typedef struct
{
    u8 signature[0x100];
    u8 nullpad[0x7C];
} PACKED ancast_type2_signature;

typedef struct
{
    u32 type;
    union
    {
        ancast_type1_signature type1;
        ancast_type2_signature type2;
    };
} PACKED ancast_signature_block;

typedef struct
{
    u16 nullpad0;
    u8 nullpad1;
    u8 nullpad2;
    u32 device;
    u32 type;
    u32 body_size;
    u32 body_hash[SHA_HASH_WORDS];
    u32 version;
    u8 nullpad3[0x38];
} PACKED ancast_info_block;

typedef struct
{
    u32 magic;
    u8 pad0[0x04];
    u32 signature_block_offset;
    u8 pad1[0x14];
} PACKED ancast_header;

u32 ancast_iop_load(const char* path);
u32 ancast_ppc_load(const char* path);

#endif
