/*  tffs - Top Field File System driver for FUSE
    Copyright (c) 2005 Sven Over <svenover@svenover.de>
    
    All information on the T*PFIELD file system, and also some
    parts of the code (especially the data structures in topfield.h),
    are taken from the original
    "Console driver program for Topfield TF4000PVR disk processing"
    Copyright (c) 2002 Petr Novak <topfield@centrum.cz>
    
    Some parts of the code are taken from example programs included in
    "FUSE: Filesystem in Userspace"
    Copyright (c) 2001-2005  Miklos Szeredi <miklos@szeredi.hu>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef	__TOPFIELD_H__
#define __TOPFIELD_H__

#include <stdint.h>

#define	TF_MAXFATSIZE	(256*512)

/*** T*pfield data types ***/

typedef struct {
	uint32_t	magic;
	char	signature[28]; /* "TOPFIELD PVR HDD" */
	uint16_t	version;
	uint16_t	cluster_size;
	uint32_t	x;
	uint32_t	first_empty;
	uint32_t	empty_in_root;
	uint32_t	fat_crc32;
	uint8_t	filler[512-52];
} tf_superblock_t __attribute__((packed));

typedef	char	tsdbname[512];

struct directory_entry_t {
	uint8_t	type;	/* 0 */
	uint8_t	data[7]; /* 1 */
	uint32_t	start_block; /* 8 */
	uint32_t	count_of_blocks; /* C */
	uint32_t	empty_in_last_block; /* 10 */
	char		name[108]; /* 14 */
} __attribute__((packed)); /* sizeof = 0x80 = 128 */


struct tf_dir_t {
	uint8_t	type;
	uint8_t	data[7];
	uint32_t	start_block;
	uint32_t	count_of_blocks;
	uint32_t	empty_in_last_block;
	char		name[512];
}  __attribute__((packed));

#endif
