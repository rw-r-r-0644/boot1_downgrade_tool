/*
 * ecc.h
 * Functions for nand ecc calculation.
 * 
 * Copyright (C) 2020 rw-r-r-0644
 * This file is under GNU GPL
 */

#pragma once

#include "common/types.h"


/*
 * Compute ecc for the given data
 * [data] size if 512 bytes
 * [ecc] size is 4 bytes
 */
void
calc_ecc(u8 *data,
		 u8 *ecc);

