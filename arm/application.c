/*
 * application.c
 *
 * This program will install the provided boot1 version
 * and set it as the main boot1 slot.
 *
 * Copyright (C) 2020 rw-r-r-0644
 * This file is under GNU GPL
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "video/gfx.h"
#include "system/smc.h"
#include "system/sha.h"
#include "system/ecc.h"
#include "system/ancast.h"
#include "system/memory.h"
#include "system/irq.h"
#include "system/gpio.h"
#include "system/latte.h"
#include "system/crypto.h"
#include "storage/nand.h"
#include "common/utils.h"
#include "storage/sdcard.h"
#include "storage/fatfs/elm.h"
#include "application.h"

#define TARGET_IMAGE_PATH	"sdmc:/boot1.target"

#define BOOT_BLOCK_SIZE		(BLOCK_SIZE * PAGE_SIZE)

#define BOOT_BLOCK(n)		(n & 0xfff)
#define BOOT_BANK(n)		((n >> 12) & 1)


/*
 * SEEPROM boot1 parameters
 */
static seeprom_boot1_params_t
seeprom_boot1_params[2];

/*
 * boot1 ancast image
 */
static u8
boot1_image[BOOT_BLOCK_SIZE];

static ancast_header *
boot1_header = (ancast_header *)&boot1_image[0];

static ancast_signature_block *
boot1_sigblock = (ancast_signature_block *)&boot1_image[0x20];

static ancast_info_block *
boot1_infoblock = (ancast_info_block *)&boot1_image[0x1A0];

void
unload_boot1_image()
{
	memset(
		boot1_image,
		0,
		sizeof(boot1_image)
	);
}

int
load_sdcard_boot1_image(char *path)
{
	unload_boot1_image();

	/* open boot1 file */
	FILE *f_boot1 = fopen(path, "rb");
	if (!f_boot1) {
		printf("error: failed to open %s\n", path);
		return -1;
	}

	/* check boot block size */
	fseek(f_boot1, 0, SEEK_END);
	if (ftell(f_boot1) != BOOT_BLOCK_SIZE) {
		printf("error: incorrect image size\n");
		return -2;
	}
	fseek(f_boot1, 0, SEEK_SET);

	/* read the boot1 image */
	if (fread(boot1_image, 1, BOOT_BLOCK_SIZE, f_boot1) != BOOT_BLOCK_SIZE) {
		printf("error: failed to read boot1 image\n");
		return -3;
	}

	fclose(f_boot1);
	return 0;
}

int
load_nand_boot1_image(int slot)
{
	static u8 pagedata[PAGE_SIZE];
	static u8 pageecc[ECC_BUFFER_ALLOC] ALIGNED(128);
	int res, nand_block, nand_start_page, page, i;

	unload_boot1_image();

	nand_block = BOOT_BLOCK(seeprom_boot1_params[slot].nand_block);
	nand_start_page = nand_block * BLOCK_SIZE;

	nand_initialize(NAND_BANK_SLC);

	for (i = 0; i < BLOCK_SIZE; i++) {
		page = nand_start_page + i;

		/* read page */
		nand_read_page(page, pagedata, pageecc);
		nand_wait();

		/* correct ecc errors */
		res = nand_correct(page, pagedata, pageecc);
		if (res == NAND_ECC_UNCORRECTABLE) {
			printf("error: encountered uncorrectable ecc error\n");
			return -1;
		}

		/* copy page content */
		memcpy(&boot1_image[i * PAGE_SIZE], pagedata, PAGE_SIZE);
	}

	return 0;
}

int
verify_boot1_image()
{
	int i, chk;

	u8 *imagebody;
	u8 imagebodyhash[SHA_HASH_SIZE];

	/*
	 * verify ancast header
	 */

	/* check ancast magic */
	if (boot1_header->magic != ANCAST_MAGIC) {
		printf("error: invalid ancast magic\n");
		return -1;
	}

	/* check signature offset */
	if (boot1_header->signature_block_offset != 0x20) {
		printf("error: boot1 signature block offset is not 0x20\n");
		return -2;
	}

	/*
	 * verify ancast header signature block
	 */

	/* signature type */
	if (boot1_sigblock->type != 2) {
		printf("error: boot1 signature type is not 2\n");
		return -3;
	}

	/* ensure the signature block null pad is empty */
	chk = 0;
	for (i = 0; i < sizeof(boot1_sigblock->type2.nullpad); i++) {
		chk |= boot1_sigblock->type2.nullpad[i];
	}
	if (chk) {
		printf("error: boot1 signature block null padding is not empty\n");
		return -4;
	}

	/* todo: verify info block rsa signature */

	/*
	 * verify ancast header info block
	 */

	/* verify fields that must be set to zero */
	chk = boot1_infoblock->nullpad0 | boot1_infoblock->nullpad1 | boot1_infoblock->nullpad2;
	for (i = 0; i < sizeof(boot1_infoblock->nullpad3); i++) {
		chk |= boot1_infoblock->nullpad3[i];
	}
	if (chk) {
		printf("error: boot1 info block empty fields are not empty\n");
		return -5;
	}

	/*
	 * verify ancast body
	 */
	imagebody = (void *)(boot1_infoblock + 1);

	/* compute ancast body hash */
	sha_hash(imagebody, imagebodyhash, boot1_infoblock->body_size);

	/* compare result with the info block */
	if (memcmp((u8*)boot1_infoblock->body_hash, imagebodyhash, SHA_HASH_SIZE)) {
		printf("error: incorrect boot1 body sha1 checksum\n");
		return -6;
	}

	return 0;
}

u32
get_boot1_params_seeprom_bank(int slot)
{
	return (slot == 0) ? 0x1D : 0x1E;
}

void
read_seeprom_boot1_params()
{
	seeprom_read_encrypted_banks(
		get_boot1_params_seeprom_bank(0),
		(u8 *)&seeprom_boot1_params[0],
		1
	);
	seeprom_read_encrypted_banks(
		get_boot1_params_seeprom_bank(1),
		(u8 *)&seeprom_boot1_params[1],
		1
	);
}

int
get_current_boot_slot()
{
	int boot_slot = -1;

	/* find the newest boot block with a valid crc32 */
	for (int i = 0; i < 2; i++) {
		u32 s_checksum = seeprom_boot1_params[i].checksum;
		u32 c_checksum = crc32_compute(
			(u8 *)&seeprom_boot1_params[i],
			sizeof(seeprom_boot1_params[i]) - sizeof(seeprom_boot1_params[i].checksum)
		);

		if (s_checksum != c_checksum) {
			printf("warning: bad checksum for boot1 parameters slot %d\n", i);
			continue;
		}

		if ((boot_slot == -1) ||
			(seeprom_boot1_params[i].version > seeprom_boot1_params[boot_slot].version)) {
			boot_slot = i;
		}
	}

	if (boot_slot == -1) {
		printf("error: none of the boot1 parameters slots are valid ???\n");
	}

	return boot_slot;
}

int
clear_seeprom_boot1_parameters_checksum(int slot)
{
	int res;

	/* clear the checksum */
	seeprom_boot1_params[slot].checksum = 0;

	/* attempt to write the modified bank to seeprom */
	res = seeprom_write_encrypted_banks(
		get_boot1_params_seeprom_bank(slot),
		(u8 *)&seeprom_boot1_params[slot],
		1
	);

	if (res != 1) {
		printf("error: seeprom writing failed\n");
		return -1;
	}

	return 0;
}

void
create_page_spare(u8 *pagedata,
				  u8 *pagespare)
{
	/* clear the spare buffer */
	memset(pagespare, 0, PAGE_SPARE_SIZE);

	/* mark the page as a good page */
	pagespare[0] = 0xff;
	
	/* compute page ecc */
	calc_ecc(pagedata, pagespare + 0x30);
	calc_ecc(pagedata + 512, pagespare + 0x34);
	calc_ecc(pagedata + 1024, pagespare + 0x38);
	calc_ecc(pagedata + 1536, pagespare + 0x3C);
}

int
flash_boot1_image(int slot)
{
	static u8 pageecc[ECC_BUFFER_ALLOC] ALIGNED(128);
	static u8 pagedata[PAGE_SIZE];
	int nand_block, nand_start_page, page, i;

	nand_block = BOOT_BLOCK(seeprom_boot1_params[slot].nand_block);
	nand_start_page = nand_block * BLOCK_SIZE;

	nand_initialize(NAND_BANK_SLC);

	for (i = 0; i < BLOCK_SIZE; i++) {
		page = nand_start_page + i;

		memset(pageecc, 0, sizeof(pageecc));
		memcpy(pagedata, &boot1_image[i * PAGE_SIZE], PAGE_SIZE);

		nand_write_page(page, pagedata, pageecc);
		nand_wait();

		pageecc[0] = 0xff;
		memcpy(&pageecc[0x30], &pageecc[0x40], 0x10);

		nand_write_page_ecc(page, pageecc);
		nand_wait();
	}

	return 0;
}

void
update_seeprom_boot1_parameters(int slot)
{
	int res;

	/* update the version */
	seeprom_boot1_params[slot].version = boot1_infoblock->version;

	/* calculate checksum */
	seeprom_boot1_params[slot].checksum = crc32_compute(
		(u8 *)&seeprom_boot1_params[slot],
		sizeof(seeprom_boot1_params[slot]) - sizeof(seeprom_boot1_params[slot].checksum)
	);

	/* attempt to write the modified bank to seeprom */
	res = seeprom_write_encrypted_banks(
		get_boot1_params_seeprom_bank(slot),
		(u8 *)&seeprom_boot1_params[slot],
		1
	);

	if (res != 1) {
		printf("error: seeprom writing failed\n");
		return -1;
	}

	return 0;
}

bool
app_run()
{
	int boot_slot, install_slot;

	gfx_clear(GFX_ALL, BLACK);
	printf("boot1_downgrade_tool\n\n");

	/*
	 * Load the target boot1 image
	 */
	if (load_sdcard_boot1_image(TARGET_IMAGE_PATH) != 0) {
		printf(">> Failed to load %s\n", TARGET_IMAGE_PATH);
		goto end;
	} else {
		printf(">> Loaded boot1 image %s\n", TARGET_IMAGE_PATH);
	}

	/*
	 * Verify the target image header and checksum
	 */
	if (verify_boot1_image() != 0) {
		printf(">> Failed to verify target image\n");
		goto end;
	} else {
		printf(">> Succesfully verified target image\n");
	}

	/*
	 * Read the current boot1 parameters from SEEPROM
	 */
	read_seeprom_boot1_params();
	printf(">> Read SEEPROM boot1 parameters\n");

	/*
	 * Determine the currently used boot slot
	 */
	boot_slot = get_current_boot_slot();
	if (boot_slot == -1) {
		printf(">> Failed to select installation slot\n");
		goto end;
	} else {
		printf(">> Current boot1 slot: %d\n", boot_slot);
		printf(">> Current boot1 nand block: %d\n", BOOT_BLOCK(seeprom_boot1_params[boot_slot].nand_block));
		printf(">> Current boot1 version: %d\n", seeprom_boot1_params[boot_slot].version);
	}

	/*
	 * Use the other slot as the installation slot
	 */
	install_slot = !boot_slot;
	printf(">> Slot %d currently contains boot1 version: %d\n", install_slot, seeprom_boot1_params[install_slot].version);

	/*
	 * Ensure boot1 is located on SLC; otherwise it might be
	 * a special console/version, it's safer to stop there.
	 */
	if ((BOOT_BANK(seeprom_boot1_params[boot_slot].nand_block) == 0) ||
		(BOOT_BANK(seeprom_boot1_params[install_slot].nand_block) == 0))
	{
		printf(">> boot1 is unexpectedly located in SLCCMPT. Stopping installation\n");
		goto end;
	}

	/*
	 * Print install information
	 */
	printf(">> Target install slot: %d\n", install_slot);
	printf(">> Target install nand block: %d\n", BOOT_BLOCK(seeprom_boot1_params[install_slot].nand_block));
	printf(">> Target install version: %lu\n", boot1_infoblock->version);

	/*
	 * Wait for user confirmation
	 */
	printf("\n");
	printf(">> PROCEED WITH THE INSTALLATION?\n");
	printf(">> Press EJECT to confirm, POWER to cancel\n");
	while (1) {
		u8 events = smc_get_events();
		if (events & SMC_POWER_BUTTON) {
			printf("Installation cancelled\n");
			goto end;
		}
		if (events & SMC_EJECT_BUTTON) {
			printf("Proceeding with the installation...\n");
			break;
		}
	}
	printf("\n");

	/*
	 * First clear the boot1 parameters checksum for the install slot.
	 * This ensures that if the install is interrupted during the nand flashing
	 * process, boot1 will boot from the other slot which has a valid
	 * checksum and boot image.
	 */
	if (clear_seeprom_boot1_parameters_checksum(install_slot) != 0) {
		printf(">> Failed to clear slot %d seeprom checksum\n", install_slot);
		goto end;
	} else {
		printf(">> Cleared slot %d seeprom checksum\n", install_slot);
	}

	/*
	 * Flash the boot1 image to the target slot
	 */
	printf(">> Flashing the target boot1 image to nand...\n");
	if (flash_boot1_image(install_slot) != 0) {
		printf(">> Failed to flash the target boot1 image to nand\n");
		goto end;
	} else {
		printf(">> Flashed the target boot1 image to nand\n");
	}

	/*
	 * Read back the image we just flashed
	 */
	if (load_nand_boot1_image(install_slot) != 0) {
		printf(">> Failed to read back the flashed image\n");
		goto end;
	} else {
		printf(">> Read back the flashed image\n");
	}

	/*
	 * Verify the flashed image header and checksum
	 */
	if (verify_boot1_image() != 0) {
		printf(">> Failed to verify the flashed image\n");
		goto end;
	} else {
		printf(">> Succesfully verified the flashed image\n");
	}

#if 0
	/*
	 * Update the install slot seeprom parameters
	 */
	if (update_seeprom_boot1_parameters(install_slot) != 0) {
		printf(">> Failed to write updated parameters for slot %d to seeprom\n", install_slot);
		goto end;
	} else {
		printf(">> Successfully written updated parameters for slot %d to seeprom\n", install_slot);
	}

	/*
	 * Now clear the old parameters checksum to ensure that boot0 will
	 * load the boot1 version we just installed
	 */
	// WARNING: THIS IS RISKY, ONLY UNCOMMENT ONCE EVRYTHING IS CONFIRMED TO BE WORKING
	//if (clear_seeprom_boot1_parameters_checksum(boot_slot) != 0) {
	//	printf(">> Failed to clear slot %d checksum\n", boot_slot);
	//	goto end;
	//} else {
	//	printf(">> Cleared slot %d checksum\n", boot_slot);
	//}

	printf("\n");
	printf(">> DOWNGRADE COMPLETED SUCCESSFULLY!\n");
	printf("\n");
#endif

end:
	printf("Press POWER to reboot.\n");
	smc_wait_events(SMC_POWER_BUTTON);
	return true;
}
