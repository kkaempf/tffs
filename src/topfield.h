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

static const int TF_MAXFATSIZE=256*512;
typedef uint32_t cluster_t;
#define FAT_CLUSTER -1
#define ROOT_CLUSTER 0

/*** T*pfield data types ***/

struct tf_superblock_t {
	uint32_t	magic __attribute__((packed));
	char	signature[28]; /* "TOPFIELD PVR HDD" ==> TF4000,  
	                          "TOPFIELD TF5000PVR HDD" ==> TF5000 */
	uint16_t	version __attribute__((packed)); /* 0x0100 = TF4000, 0x0101 = TF5000 */
	uint16_t	cluster_size __attribute__((packed)); /* 2048: TF4000, 2068: TF5000 */
	uint32_t	x __attribute__((packed));
	uint32_t	first_empty __attribute__((packed));
	uint32_t	empty_in_root __attribute__((packed));
	uint32_t	fat_crc32 __attribute__((packed));
	uint8_t	filler[512-52];
};

typedef	char	tsdbname[512];

struct tf_entry_t {
	uint8_t	type;	/* 0 */
	uint8_t	data[7]; /* 1 */
	cluster_t	start_cluster; /* 8 */
	uint32_t	count_of_clusters; /* 12, 0x0C */
	uint32_t	empty_in_last_cluster; /* 16, 0x10 */
	char		name[64]; /* 20, 0x14 */
        char            channel[40]; /* 84, 0x54 */
        uint8_t         data1[4]; /* 124, 0x7c */
} __attribute__((packed)); /* sizeof = 0x80 = 128 */

#if 0
struct tf_dir_t {
	uint8_t	type;
	uint8_t	data[7];
	cluster_t	start_cluster;
	uint32_t	count_of_clusters;
	uint32_t	empty_in_last_cluster;
	char		name[512];
}  __attribute__((packed));
#endif

#endif
