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

// #include <cunistd>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cerrno>

#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <byteswap.h>

#include <list>
#include <iostream>

#include "tfdisk.h"

using namespace std;

#if __BYTE_ORDER == __LITTLE_ENDIAN
# define swap32(x) (bswap_32(x))
# define swap16(x) (bswap_16(x))
#else
# define swap32(x) (x)
# define swap16(x) (x)
#endif


// DEBUGMSG
#ifndef DEBUGMSG
# ifdef DEBUG
#  include <stdio.h>
#  ifdef __cplusplus
#   define DEBUGMSG(format, ...) ::fprintf(::stderr, "%s(%d): " format "\n" , __FILE__, __LINE__, ##__VA_ARGS__)
#  else
#   define DEBUGMSG(format, ...) fprintf(stderr, "%s(%d): " format "\n" , __FILE__, __LINE__, ##__VA_ARGS__)
#  endif
# else
#  define DEBUGMSG(format, ...)
# endif
#endif

// *************************************************************************
// convert_date_time (static helper function)

static void make_name(char *newname, const char *oldname, const char *extension, const char space, const char def);
static time_t convert_date_time(uint8_t * p)
{
/* Please see EN 300 468 Annex C - Conversion between time and date conventions */
/* The EN is freely available from www.ETSI.ch */

#define	MJD_scale	10000L

  long MJDdate = ((p[0] << 8) | p[1]) * MJD_scale;
  int month, day, year;
  int year_diff, month_diff;

  int hr = p[2];
  int min = p[3];

  if (MJDdate) {
    year = (MJDdate - 150782000L) / 3652500L;
    year_diff = ((year * 3652500L) / MJD_scale) * MJD_scale;
    month = (MJDdate - 149561000L - year_diff) / 306001L;
    month_diff = ((month * 306001L) / MJD_scale) * MJD_scale;
    day = (MJDdate - 149560000L - year_diff - month_diff) / MJD_scale;
    if (month > 13) {
      year++;
      month -= 13;
    } else {
      month--;
    }
    struct tm t;

    t.tm_year = year;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hr;
    t.tm_min = min;
    t.tm_sec = 0;
    return mktime(&t);
  } else {
    return 0;
  }

}

// *************************************************************************
// tfdisk::~tfdisk (destructor)

tfdisk::~tfdisk()
{
  close();
  delete buffer;
}

// *************************************************************************
// tfdisk::open

bool tfdisk::open()
{
  if (fd >= 0)
    close();

  fd =::open(devfn.c_str(), O_RDONLY);

  if (fd < 0) {
    cerr << "open(" << devfn << "): " << strerror(errno) << endl;
    return false;
  }

  size = lseek(fd, 0, SEEK_END);

  if (size < 0 || lseek(fd, 0, SEEK_SET) != 0) {
    ::close(fd);
    fd = -1;
    cerr << "lseek(" << devfn << "): " << strerror(errno) << endl;
    return false;
  }

  tf_superblock_t superblock;

  if (::read(fd, &superblock, sizeof(superblock)) != sizeof(superblock)) {
    ::close(fd);
    fd = -1;
    cerr << "read(" << devfn << "): " << strerror(errno) << endl;
    return false;
  }
  if (superblock.cluster_size == 0x0101) { // value for version after bswap !
    // swap read data for TF5000
    for (uint32_t * p = (uint32_t *) & superblock; p != (uint32_t *) (&superblock + 1); ++p)
      *p = bswap_32(*p);
  } else {
    for (uint16_t * p = (uint16_t *) & superblock; p != (uint16_t *) (&superblock + 1); ++p)
      *p = bswap_16(*p);
  }
  // is the device a T*PFIELD PVR HDD?
  tf_type = TF_UNKNOWN;
  
  if (strcmp(superblock.signature, "TOPFIELD PVR HDD") == 0) {
    tf_type = TF_4000;
    DEBUGMSG("Found TF4000, version %04x", superblock.version);
  } else if (strcmp(superblock.signature, "TOPFIELD TF5000PVR HDD") == 0) {
    tf_type = TF_5000;
    DEBUGMSG("Found TF5000, version %04x", superblock.version);
  } else {
    ::close(fd);
    fd = -1;
    errno = EIO;
    cerr << devfn << " is not a TOPFIELD TF4000PVR/TF5000PVR HDD device! [" << superblock.signature << "]" << endl;
    return false;
  }

  DEBUGMSG("Size %lld/%lldK/%lldM", size, size/1024LL, size/1024LL/1024LL);
  lba_sectors = size / 512LL;
  DEBUGMSG("Sectors %lld/%08llx", lba_sectors, lba_sectors);
  cluster_size = swap16(superblock.cluster_size) << 9;
  DEBUGMSG("Clustersize %04x/%04x", superblock.cluster_size, cluster_size);

  delete buffer;
  buffer = NULL;
  buffer = new uint8_t [cluster_size];

  fatitems = ((uint32_t) (lba_sectors / (cluster_size >> 9))) - 1;
  DEBUGMSG("Fatitems %ld", fatitems);
  if (fatitems > TF_MAXFATSIZE) {
    DEBUGMSG("TF_MAXFATSIZE %ld", TF_MAXFATSIZE);
    fatitems = TF_MAXFATSIZE;
  }
  // read FAT table
  read_fat();
  // parse directory
  parse_dir(0, 0xff, 0xd1);
//  ::close(fd);errno=EIO;return false;
  // generate file segment table for all files
  for (std::vector < tfinode_ptr >::iterator it = entry.begin(); it != entry.end(); ++it)
    gen_filesegments(*it);

#ifdef DEBUG
  DEBUGMSG("clustersize:%d", int (getclustersize()));
  int ino = 2;

  for (std::vector < tfinode_ptr >::iterator it = entry.begin(); it != entry.end(); ++it, ++ino) {
    DEBUGMSG("  ino:%d", ino);
    int i = 0;

    for (tfinode::seg_t::const_iterator seg=(*it)->seg.begin(); seg->size; ++seg, ++i)
      DEBUGMSG("  ino:%d [%d] offset:%lld size:%lld pos:%lld", ino, i, (long long) seg->offset,
               (long long) seg->size, (long long) seg->pos);
  }
#endif

  return true;
}

// *************************************************************************
// tfdisk::close

void tfdisk::close()
{
  if (fd >= 0)
    ::close(fd);
  fd = -1;
  size = -1ll;
}

// *************************************************************************
// tfdisk::read

ssize_t tfdisk::read(uint32_t ino, char *buf, size_t size, off_t offset)
{
  if (ino < 2 || ino >= entry.size() + 2)
    return -EIO;

  DEBUGMSG("tfdisk::read: ino:%d size:%d offset:%lld", ino, int (size), offset);

  tfinode::seg_t::const_iterator seq = entry[ino - 2]->seg.begin();

  DEBUGMSG("seq->offset(%lld) seq->size(%lld) seq->pos(%lld)", (long long) seq->offset,
           (long long) seq->size, (long long) seq->pos);

  seq += int (offset / (off_t) getclustersize());

  DEBUGMSG("seq->offset(%lld) seq->size(%lld) seq->pos(%lld)", (long long) seq->offset,
           (long long) seq->size, (long long) seq->pos);
  
  ssize_t written = 0;

  while (size > 0 && seq->size > 0) {
    
    DEBUGMSG("size=%lld, seq->size=%lld", (long long) size, (long long) seq->size);
    
    if (offset >= seq->offset + seq->size) {
      ++seq;
      DEBUGMSG("offset(%lld) >= seq->offset(%lld) + seq->size(%lld)", (long long) offset,
               (long long) seq->offset, (long long) seq->size);
    }
    
    // seek to position
    // if offset is an odd number, decrement seek position
    if (seq->size <= (offset - seq->offset))
      continue;

    off_t s64 = (int64_t(seq->size) - (offset - seq->offset));
    size_t s = (s64 > size) ? size : s64;

    if (lseek(fd, (seq->pos + (offset & ~1ll) - seq->offset), SEEK_SET) == -1) {
      cerr << "lseek: " << strerror(errno) << endl;
      return -EIO;
    }
    
    // if oddset is an odd number, read one byte into buffer and discard one byte
    // (byte swapping!)
    if ((offset & 1) || (s == 1)) {
      uint8_t minibuffer[2];
      ssize_t rd =::read(fd, minibuffer, 2);

      DEBUGMSG("offset=%lld s=%lld", (long long) offset, (long long) s);

      if (rd < 0) {
        cerr << "read: " << strerror(errno) << endl;
        return -EIO;
        if (rd != 0)
          break;
      }
      
      DEBUGMSG("---");

      *buf = minibuffer[(offset & 1) ? 0 : 1];
      ++written;
      ++buf;
      ++offset;
      --size;

      continue;
    }

    DEBUGMSG("---");

    s &= ~1;

    ssize_t rd =::read(fd, buf, s);

    DEBUGMSG("rd=%lld  (s=%lld)", (long long) rd, (long long) s);

    if (rd == 0)
      break;
    if (rd < 0) {
      cerr << "read: " << strerror(errno) << endl;
      return -EIO;
    }
    rd &= ~1llu;

    if (tf_type == TF_4000) {
      uint16_t *p, *e;

      for (p = (uint16_t *) buf, e = p + s / 2; p != e; ++p)
	*p = bswap_16(*p);
    } else {
      uint32_t *p, *e;

      for (p = (uint32_t *) buf, e = p + s / 2; p != e; ++p)
	*p = bswap_32(*p);
    }
    
    written += rd;
    buf += s;
    offset += s;
    size -= s;

    DEBUGMSG("written=%lld buf=%lld offset=%lld size=%lld", (long long) written, (long long) buf,
             (long long) offset, (long long) size);
  }

  DEBUGMSG("written=%lld", (long long) written);
  return written;
}

// *************************************************************************
// tfdisk::readcluster (private helper function)

bool tfdisk::readcluster(int n)
{
  off_t pos = ((off_t) n + 1) * cluster_size;
DEBUGMSG("Read cluster %d at %08lx[%08lx]", n, pos, pos>>9);
  if (pos + cluster_size > size) {
    cerr << "Attempt to read after end of disk!" << endl;
    return false;
  }

  if (lseek(fd, pos, SEEK_SET) == -1) {
    cerr << "lseek(" << pos << "): " << strerror(errno) << endl;
    return false;
  }

  ssize_t bytesread =::read(fd, buffer, cluster_size);

  if (bytesread < 0) {
    cerr << "read: " << strerror(errno) << endl;
    return false;
  }

  if ((unsigned off_t) bytesread != cluster_size)
    cerr << "Warning: read returned " << (cluster_size - bytesread) << " bytes too few" << endl;

  if (tf_type == TF_4000) {
    for (uint16_t * e = (uint16_t *) (buffer + cluster_size), *p = (uint16_t *) buffer; p != e; ++p)
      *p = bswap_16(*p);
  } else {
    for (uint32_t * e = (uint32_t *) (buffer + cluster_size), *p = (uint32_t *) buffer; p != e; ++p)
      *p = bswap_32(*p);
  }
  return true;
}

// *************************************************************************
// tfdisk::parse_dir (private helper function)

bool tfdisk::parse_dir(uint32_t cluster, uint8_t mask, uint8_t value)
{
  directory_entry_t *p = (directory_entry_t *) buffer;
  uint32_t tsdb = ~uint32_t(0);

  typedef std::list<tfinode_ptr> tempdir_t;
  tempdir_t tempdir;

  if ((!readcluster(cluster)) || (p->type != 0xF1)) {
    DEBUGMSG("Wrong directory type (%02x != F1) at cluster %ld", p->type, cluster);
    return false;
  }

  int maxdir = (cluster_size - swap32(p->empty_in_last_block)) / sizeof(directory_entry_t);
DEBUGMSG("parse_dir @%ld, mask %02x, value %02x: %d entries", cluster, mask, value, maxdir);
  for (int dir_item = 0; dir_item < maxdir; ++p, ++dir_item) {
    
    // types
    // ff : deleted
    // f1 : __ROOT__
    // f2 : directory
    // f3 : hidden
    // d1 : normal
    if (p->type == 0xff) {
      continue;                 // Ignore deleted files
    } else {
      tfinode_ptr d(new tfinode);

      tempdir.push_back(d);

      d->d.type = p->type;
      strncpy(d->d.name, p->name, sizeof(p->name) - 1);
      d->d.name[sizeof(p->name) - 1] = 0;
      d->d.start_block =swap32(p->start_block); 
      d->d.count_of_blocks = swap32(p->count_of_blocks);
      d->d.empty_in_last_block = swap32(p->empty_in_last_block);
      memcpy(d->d.data, p->data, sizeof(p->data));

    DEBUGMSG("Item %3d: type %02x: '%s'", dir_item, p->type, d->d.name);
      struct stat &sp = d->s;

      sp.st_ino = 0;

      switch (p->type) {
       case 0xf1: /*FALLTHRU*/
       case 0xf2: sp.st_mode = S_IFDIR | 0555; break; /* dr_xr_xr_x */
       case 0xf3: sp.st_mode = S_IFDIR | 0500; break; /* dr_x______ */
       case 0xd1: sp.st_mode = 0444; break; /* _r__r__r__ */
       default: sp.st_mode = 0;
	fprintf(stderr, "Unknown type %02x for '%s'\n", p->type, d->d.name);
      }
      sp.st_nlink = 1;
      sp.st_uid = 0;
      sp.st_gid = 0;
      sp.st_rdev = 0;
      sp.st_size = (off_t) d->d.count_of_blocks * (off_t) cluster_size - d->d.empty_in_last_block;
      sp.st_blksize = cluster_size;
      sp.st_blocks = ( uint64_t(d->d.count_of_blocks) * cluster_size + 511 ) / 512;
      sp.st_atime = sp.st_mtime = sp.st_ctime = convert_date_time(d->d.data);
    }
    if (!strcmp(p->name, "__FILETSDB__.ss")) {
      tsdb = swap32(p->start_block);
    }
  }

  // WHAT THE HELL HAPPENS HERE WITH THE NAMES??? RAG
  if (mask && (tsdb != ~uint32_t(0))) {
    if (!readcluster(tsdb))
      return false;
    int ino = 1;
    uint32_t count = (int) swap32(*((uint32_t *) buffer));

    for (uint32_t i = 1; i <= count; ++i)
      for (std::list < tfinode_ptr >::iterator it = tempdir.begin(); it != tempdir.end(); ++it) {
        //printf("%d. %s == %s: ",i,(*it)->d.name,((tsdbname *) buffer)[i]);
        if (!strncmp((*it)->d.name, ((tsdbname *) buffer)[i], sizeof(p->name) - 1)) {
	  const char *ext = (tf_type == TF_4000) ? ".tts" : ".rec";
          // clean the file names from invalid chars instead just copying and
          // add the well known file extention
          // strncpy((*it)->d.name, ((tsdbname *) buffer)[i], sizeof(tsdbname)); 
          make_name((*it)->d.name, ((tsdbname *) buffer)[i], ext, ' ', '_');
          //printf("%d. OLD: '%s' (char[0] = %d) ==> NEW: '%s'\n",i,((tsdbname *) buffer)[i],((tsdbname *)  buffer)[i][0],(*it)->d.name);
          entry.push_back(*it);
          tempdir.erase(it);
          (*it)->s.st_ino = ++ino;
          break;
        }
	}

    return true;
  } else {
    int ino = 1;

    for (tempdir_t::iterator it = tempdir.begin(); it != tempdir.end(); ++it) {
      (*it)->s.st_ino = ++ino;
      entry.push_back(*it);
    }

    return true;
  }

  return true;
}

// *************************************************************************
// tfdisk::read_fat (private helper function)

bool tfdisk::read_fat()
{
  fat_t fat1, fat2;
  bool fat1bad = false;
  bool fat2bad = false;

  // Read in the cluster with both FATs
  if (!readcluster(-1))
    return false;

  // first FAT table
  uint8_t *buf = buffer + (0x100 << 9);

  fat1.reserve(fatitems);

  // Walk over the FAT data just read
  // Create fat1 temporrary array

  for (uint32_t i = 0; i < fatitems; ++i) {
    uint32_t val = (buf[3 * i] << 16) | (buf[3 * i + 1] << 8) | buf[3 * i + 2];

    // Check each item for validity
    if (val < 0xfffffb && val > (lba_sectors >> 11)) {
      fprintf(stderr, "FAT1 item #%d wrong (%ld/%08x)\n", i, val, val);
      if ((val & 0xffff) == i + 1) {
        val &= 0xffff;
      } else {
        val = ~0L;
        fat1bad = true;
      }
    }

    fat1.push_back(val);
  }

  // second FAT table
  buf = buffer + (0x400 << 9);

  fat2.reserve(fatitems);

  for (uint32_t i = 0; i < fatitems; ++i) {
    uint32_t val = (buf[3 * i] << 16) | (buf[3 * i + 1] << 8) | buf[3 * i + 2];

    if (val < 0xfffffb && val > (lba_sectors >> 11)) {
//      cerr << "FAT2 item #" << i << " wrong (" << val << ")!" << endl;
      fprintf(stderr, "FAT2 item #%d wrong (%ld/%08x)\n", i, val, val);
      val = ~0L;
      fat2bad = true;
    }
    
    fat2.push_back(val);

    // Check if FAT1 and FAT2 are identical
    // ignore error items marked in phase 1
    if ((fat1[i] != ~uint32_t(0)) && (val != ~uint32_t(0)) && (fat1[i] != val)) {
      cerr << "FAT mismatch: #" << i << ": " << fat1[i] << " != " << val << endl;
    }
  }

  // Copy a valid fat into global variable for later use
  if (!fat1bad) 
    fat.swap(fat1);
  else if (!fat2bad)
    fat.swap(fat2);
  else
  {
    if (tf_type == TF_4000)
      cerr << "No valid FAT24 found!" << endl;
    else
      cerr << "No valid FAT32 found!" << endl;
    return false;
  }

  return true;
}

// *************************************************************************
// tfdisk::gen_filesegments (private helper function)

void tfdisk::gen_filesegments(tfinode_ptr & i)
{
  i->seg.clear();
  i->seg.resize(i->d.count_of_blocks + 1);
//   if (i->seg)
//     free(i->seg);
//   i->seg = (tffilesegment *) malloc((i->d.count_of_blocks + 1) * sizeof(tffilesegment));
  uint32_t cluster = i->d.start_block;

  uint32_t j;
  uint32_t clusters = uint32_t(size / (off_t) cluster_size);

  for (j = 0; j < i->d.count_of_blocks && cluster < clusters; ++j) {
    i->seg[j] =
        tffilesegment((off_t) j * (off_t) cluster_size, cluster_size,
                      (off_t) (cluster + 1) * (off_t) cluster_size);
    cluster = fat[cluster];
  }
  if (j == i->d.count_of_blocks)
    i->seg[j - 1].size -= i->d.empty_in_last_block;

  for (; j <= i->d.count_of_blocks; ++j)
    i->seg[j] = tffilesegment(0, 0, 0);

  // merge adjacent clusters (will reduce number of file operations)

  int maxclusters = (2u << 30) / cluster_size - 1;

  for (j = 0; j < i->d.count_of_blocks - 1; ++j) {
    uint32_t n = 0;

    for (; j + n + 1 < i->d.count_of_blocks; ++n)
      if (i->seg[j + n + 1].pos != i->seg[j + n].pos + cluster_size)
        break;

    for (uint32_t k = 0; k < n; ++k)
      i->seg[j + k].size += cluster_size * (((n - k) > maxclusters) ? maxclusters : (n - k));

    j += n;
  }

}

// *************************************************************************
// void tfdisk::make_name (make valid file names)

static void make_name(char *newname, const char *oldname, const char *extension, const char space, const char def)
{
	// TODO: - one could make the extension & default replacement character ('_' or NULL?) for special chars externally configurable 
        //       - the replacement of spaces externally switchable (' ' or '_' or NULL?)

	while (*oldname) {
                if (strchr("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-+=;$!@#$%^&()[]{}_", *oldname)) {
                // accept only "normal" chars listed above
		        *newname = *oldname;
                        //printf("1. normal %c / %d / %x ==> %c\n",*oldname,(unsigned char)*oldname,(unsigned char)*oldname,*newname);
	                newname++;
                } else if ((unsigned char)*oldname > 31 ) {
                // replace every (printable) character NOT listed above, for instance language specific characters (and discard the rest)
                        if((unsigned char)*oldname == 0xe4) {
                                // conversion for special language specific (latin1) chars? 
			        *newname = 'a';
                                //printf("2. special '%c' / %u / %x ==> '%c",*oldname,(unsigned char)*oldname,(unsigned char)*oldname,*newname);
		                newname++;
			        *newname = 'e';
                                //printf("%c'\n",*newname);
		                newname++;
                        } else if((unsigned char)*oldname == 0xf6) {
                                // conversion for special language specific (latin1) chars? 
			        *newname = 'o';
                                //printf("2. special '%c' / %u / %x ==> '%c",*oldname,(unsigned char)*oldname,(unsigned char)*oldname,*newname);
		                newname++;
			        *newname = 'e';
                                //printf("%c'\n",*newname);
		                newname++;
                        } else if((unsigned char)*oldname == 0xfc) {
                                // conversion for special language specific (latin1) chars? 
			        *newname = 'u';
                                //printf("2. special '%c' / %u / %x ==> '%c",*oldname,(unsigned char)*oldname,(unsigned char)*oldname,*newname);
		                newname++;
			        *newname = 'e';
                                //printf("%c'\n",*newname);
		                newname++;
                        } else if((unsigned char)*oldname == 0xc4) {
                                // conversion for special language specific (latin1) chars? 
			        *newname = 'A';
                                //printf("2. special '%c' / %u / %x ==> '%c",*oldname,(unsigned char)*oldname,(unsigned char)*oldname,*newname);
		                newname++;
			        *newname = 'e';
                                //printf("%c'\n",*newname);
		                newname++;
                        } else if((unsigned char)*oldname == 0xd6) {
                                // conversion for special language specific (latin1) chars? 
			        *newname = 'O';
                                //printf("2. special '%c' / %u / %x ==> '%c",*oldname,(unsigned char)*oldname,(unsigned char)*oldname,*newname);
		                newname++;
			        *newname = 'e';
                                //printf("%c'\n",*newname);
		                newname++;
                        } else if((unsigned char)*oldname == 0xdc) {
                                // conversion for special language specific (latin1) chars? 
			        *newname = 'U';
                                //printf("2. special '%c' / %u / %x ==> '%c",*oldname,(unsigned char)*oldname,(unsigned char)*oldname,*newname);
		                newname++;
			        *newname = 'e';
                                //printf("%c'\n",*newname);
		                newname++;
                        } else if((unsigned char)*oldname == 0xdf) {
                                // conversion for special language specific (latin1) chars? 
			        *newname = 's';
                                //printf("2. special '%c' / %u / %x ==> '%c",*oldname,(unsigned char)*oldname,(unsigned char)*oldname,*newname);
		                newname++;
			        *newname = 's';
                                //printf("%c'\n",*newname);
		                newname++;
                        } else if((unsigned char)*oldname == 32 && space!=0x0) {
                                // better accept spaces (among other "normal" chars from above) since otherwise one can get ambiguous/double file names! 
                                *newname = space;
                                //printf("2. special '%c' / %u / %x ==> '%c'\n",*oldname,(unsigned char)*oldname,(unsigned char)*oldname,*newname);
		                newname++;
			} else if(def!=0x0) {
                                // all others get the default character
                                *newname = def;
                                //printf("2. special '%c' / %u / %x ==> '%c'\n",*oldname,(unsigned char)*oldname,(unsigned char)*oldname,*newname);
		                newname++;
                        }
		}
                //else
                  //printf("3. non-printable '%c' / %u / %x ==> DISCARD!\n",*oldname,(unsigned char)*oldname,(unsigned char)*oldname);
		oldname++;
	}
	*newname = '\0';
	strcat(newname, extension);
}
