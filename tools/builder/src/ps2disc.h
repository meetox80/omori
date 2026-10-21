#ifndef TB_PS2DISC_H
#define TB_PS2DISC_H

#include <stdint.h>
#include <stdio.h>

#include "image.h"
#include "util.h"

#define PS2_BOOT_NAME "OMRI_010.08"
#define PS2_PAK_NAME "OMORI.PAK"
#define PS2_PART_BYTES 0x7FFFF800u

int ps2_elf_check(const uint8_t* data, size_t len);
int ps2_iso_write_head(FILE* f, const uint8_t* elf, size_t elf_len, uint64_t* pak_at);
int ps2_iso_write_tail(FILE* f, uint32_t elf_len, uint64_t pak_len);
int ps2_iso_replace_elf(const char* path, const uint8_t* elf, size_t elf_len);
int ps2_iso_is_iso(FILE* f);
void ps2_icon_sys(const char* title, Buf* out);
void ps2_icon_icn(const Image* art, Buf* out);

#endif
