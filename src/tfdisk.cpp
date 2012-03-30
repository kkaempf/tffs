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
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <byteswap.h>

#include <list>
#include <iostream>

extern "C" void hexdump (unsigned char *ptr, int size, FILE *out, const char *title);

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
#   define DEBUGMSG(format, ...) DEBUGMSG( "%s(%d): " format "\n" , __FILE__, __LINE__, ##__VA_ARGS__)
#  endif
# else
#  define DEBUGMSG(format, ...)
# endif
#endif

// *************************************************************************
// convert_date_time (static helper function)

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
// char *conv_name(const char *inbuf, char *outbuf, size_t size)
char *
tfdisk::conv_name(const char *inbuf, char *outbuf, size_t size)
{
  char *out = outbuf;
  unsigned char utf8 = 0;
  unsigned char utf8_start = 0;
  if (inbuf == NULL
      || outbuf == NULL)
    return NULL;
  if (*inbuf == 0x05) ++inbuf;

  while (*inbuf && size > 0) {
    switch ((unsigned char)*inbuf) {
     case 0xad: // soft hyphen
      utf8_start = 0xc2;
      utf8 = 0xad;
      break;
     case 0xc4: //Ä
      utf8 = 0x84;
      break;
     case 0xd6: //Ö
      utf8 = 0x96;
      break;
     case 0xdc: //Ü
      utf8 = 0x9c;
      break;
     case 0xdf: //ß
      utf8 = 0x9f;
      break;
     case 0xe4: //ä
      utf8 = 0xa4;
      break;
     case 0xf6: //ö
      utf8 = 0xb6;
      break;
     case 0xfc: //ü
      utf8 = 0xbc;
      break;
     case 0xff: //-
      *out++ = '-';
      break;
     default:
      if ((*inbuf < 32) || (*inbuf > 126)) {
	::fprintf(::stderr, "Bad char %02x!\n", (unsigned char)*inbuf);
      }
      *out++ = *inbuf;
      break;
    }
    if (utf8) {
      if (size < 2) {
	::fprintf(::stderr, "Outbuf overflow!\n");
	break;
      }
      *out++ = utf8_start ? utf8_start : 0xc3; --size;
      utf8_start = 0;
      *out++ = utf8; --size;
      utf8 = 0;
    }
    ++inbuf;
  }
  *out = 0;
  return outbuf;
}

// *************************************************************************
// tfdisk::~tfdisk (destructor)

tfdisk::~tfdisk()
{
  close();
  delete _buffer;
}

// *************************************************************************
// tfdisk::open

int
tfdisk::open()
{
  int err = 0;
  const std::vector<tfinode_ptr> *root;

  if (_fd >= 0)
    close();

  _fd =::open(_devfn.c_str(), O_RDONLY);

  if (_fd < 0) {
    cerr << "open(" << _devfn << "): " << strerror(errno) << endl;
    return errno;
  }

  _size = lseek(_fd, 0, SEEK_END);

  if (_size < 0 || lseek(_fd, 0, SEEK_SET) != 0) {
    ::close(_fd);
    _fd = -1;
    cerr << "lseek(" << _devfn << "): " << strerror(errno) << endl;
    return errno;
  }

  tf_superblock_t superblock;

  if (::read(_fd, &superblock, sizeof(superblock)) != sizeof(superblock)) {
    ::close(_fd);
    _fd = -1;
    cerr << "read(" << _devfn << "): " << strerror(errno) << endl;
    return errno;
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
  _type = TF_UNKNOWN;
  
  if (strcmp(superblock.signature, "TOPFIELD PVR HDD") == 0) {
    _type = TF_4000;
    DEBUGMSG("Found TF4000, version %04x", superblock.version);
  } else if (strcmp(superblock.signature, "TOPFIELD TF5000PVR HDD") == 0) {
    _type = TF_5000;
    DEBUGMSG("Found TF5000, version %04x", superblock.version);
  } else {
    ::close(_fd);
    _fd = -1;
    errno = EIO;
    cerr << _devfn << " is not a TOPFIELD TF4000PVR/TF5000PVR HDD device! [" << superblock.signature << "]" << endl;
    return errno;
  }

  DEBUGMSG("Size %lld/%lldK/%lldM", _size, _size/1024LL, _size/1024LL/1024LL);
  _lba_sectors = _size / 512LL;
  DEBUGMSG("Sectors %lld/0x%08llx", _lba_sectors, _lba_sectors);
  _cluster_size = swap16(superblock.cluster_size) << 9;
  DEBUGMSG("Clustersize %04ld sectors / 0x%04lx bytes", swap16(superblock.cluster_size), _cluster_size);

  delete [] _buffer;
  _buffer = NULL;
  _buffer = new uint8_t [_cluster_size];

  _fatitems = ((uint32_t) (_lba_sectors / (_cluster_size >> 9))) - 1;
  DEBUGMSG("%ld clusters", _fatitems);
  if (_fatitems > TF_MAXFATSIZE) {
    DEBUGMSG("TF_MAXFATSIZE %ld", TF_MAXFATSIZE);
    _fatitems = TF_MAXFATSIZE;
  }
  // read FAT table
  err = read_fat();
  if (err)
    return err;

  // create root dir inode
  tfinode_ptr rootdir = inode4path("/", &err);
  if (err)
    return err;

  _inodes.push_back(rootdir); // dummy 'zero' inode
  _inodes.push_back(rootdir); // root dir gets inode #1
  DEBUGMSG("Created inodes 0 and 1 @%p", rootdir);
//  ::close(fd);errno=EIO;return false;

#if 0
  DEBUGMSG("clustersize:%d", int (getclustersize()));
  int ino = 2;

  for (std::vector < tfinode_ptr >::const_iterator it = root->begin(); it != root->end(); ++it, ++ino) {
    DEBUGMSG("  ino:%d", ino);
    int i = 0;

    for (tfinode::seg_t::const_iterator seg=(*it)->seg.begin(); seg->size; ++seg, ++i)
      DEBUGMSG("  ino:%d [%d] offset:%lld size:%lld pos:%lld", ino, i, (long long) seg->offset,
               (long long) seg->size, (long long) seg->pos);
  }
#endif

  return 0;
}

// *************************************************************************
// tfdisk::close

void
tfdisk::close()
{
  if (_fd >= 0)
    ::close(_fd);
  _fd = -1;
  _size = -1ll;
}

// *************************************************************************
// tfdisk::inodeptr

tfinode_ptr
tfdisk::inodeptr(uint32_t n)
{
//  DEBUGMSG("inodeptr(%d)", n);
  if (n < INODE_ROOT
      || n >= _inodes.size())
    return NULL;
//  DEBUGMSG("-> %p", _inodes[n]);
  return _inodes[n];
}

// *************************************************************************
// tfdisk::inode4path

tfinode_ptr
tfdisk::inode4path(const char *path, int *err)
{
  tfinode_ptr inode;

  *err = 0;
  DEBUGMSG("inode4path(%s)", path);

  // path must be absolute
  if (!path || *path != '/') {
    *err = -EINVAL;
    return inode;
  }

  // start traversal at root dir
  inode = inodeptr(INODE_ROOT);

  if (path[1] == 0) { // root dir
    if (!inode) { // first call, gen root inode
      tfinode_ptr root(new struct tfinode);
      memset(root, 0, sizeof (struct tfinode));
      inode = root;
      DEBUGMSG("Root inode");
      inode->first = 0;
      inode->count = -1;
      inode->st.st_ino = 1;
      inode->st.st_mode = S_IFDIR | 0555;
      inode->st.st_nlink = 2;
    }
    DEBUGMSG("Root @%p", inode);
    return inode;
  }

  inode_t first;
  int count;

  // split path at '/'
  // read directory
  // compare entries with path elements
  // 
  for(;;) {
    const char *next = strchr(++path, '/');
    if (!next)
      next = path + strlen(path);
    DEBUGMSG("Looking for <%s>[%d]", path, next - path);
    
    DEBUGMSG("In loop, reading dir for '%s'", inode->entry.name);
    *err = readdir(inode, &first, &count);
    if (*err)
      break;
    DEBUGMSG("Dir @%p has %d entries, first at %d", inode, count, first);
    while (count--) {
      inode = inodeptr(first++);
      if (!strncmp(path, inode->entry.name, next-path)) {
	DEBUGMSG("Found <%s> [%d@%p]", inode->entry.name, (int)(first-1), (void *)inode);
	break;
      }
    } // while...
    if (count < 0) {
      DEBUGMSG("Not found");
      *err = -ENOENT;
      break;
    }
    if (*next == 0) // traversal finished !
      break;
    path = next;
  } // for(;;)

  return inode;
}

// *************************************************************************
// tfdisk::readdir
// collect inodes from cache
// 
int
tfdisk::readdir(tfinode_ptr inode, inode_t *first, int *count )
{
  int err;
  uint32_t i;

  DEBUGMSG("tfdisk::readdir(%d)", inode->st.st_ino);
  err = gen_inodes(inode);
  if (err)
    return err;

  *count = inode->count;
  *first = inode->first;
  DEBUGMSG("readdir %d entries @ %d", *count, *first);
  return 0;
}



// *************************************************************************
// tfdisk::read

ssize_t
tfdisk::read(inode_t ino, char *buf, size_t size, off_t offset)
{
  tfinode_ptr inode = inodeptr(ino);
  if (!inode) {
    DEBUGMSG("read: bad inode %d", ino);
    return -EINVAL;
  }
  DEBUGMSG("tfdisk::read: ino:%d size:%lld offset:%lld", ino, (long long)size, (long long)offset);

  tfinode::seg_t::const_iterator seq = inode->seg.begin();

  DEBUGMSG("Start: seq->offset(%lld) seq->size(%lld) seq->pos(%lld)", (long long) seq->offset,
           (long long) seq->size, (long long) seq->pos);

  seq += int (offset / (off_t) getclustersize());

  DEBUGMSG("Offset: seq->offset(%lld) seq->size(%lld) seq->pos(%lld)", (long long) seq->offset,
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

    if (lseek(_fd, (seq->pos + (offset & ~1ll) - seq->offset), SEEK_SET) == -1) {
      cerr << "lseek: " << strerror(errno) << endl;
      return -EIO;
    }
    
    // if offset is an odd number, read one byte into minibuffer and discard one byte
    // (byte swapping!)
    if ((offset & 1) || (s == 1)) {
      uint8_t minibuffer[2];
      ssize_t rd =::read(_fd, minibuffer, 2);

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

    ssize_t rd =::read(_fd, buf, s);

    DEBUGMSG("rd=%lld  (s=%lld)", (long long) rd, (long long) s);

    if (rd == 0)
      break;
    if (rd < 0) {
      cerr << "read: " << strerror(errno) << endl;
      return -EIO;
    }
    rd &= ~1llu;

    if (_type == TF_4000) {
      uint16_t *p, *e;

      for (p = (uint16_t *) buf, e = p + s / 2; p != e; ++p)
	*p = bswap_16(*p);
    } else {
      uint32_t *p, *e;

      for (p = (uint32_t *) buf, e = p + s / 4; p != e; ++p)
	*p = bswap_32(*p);
    }
    
    written += rd;
    buf += s;
    offset += s;
    size -= s;

    DEBUGMSG("written=%lld buf=%p offset=%lld size=%lld", (long long) written, (void *) buf,
             (long long) offset, (long long) size);
  }

  DEBUGMSG("written=%lld", (long long) written);
  return written;
}

// *************************************************************************
// tfdisk::read_cluster (private helper function)

int
tfdisk::read_cluster(cluster_t n)
{
  off_t pos = ((off_t)(n + 1)) * _cluster_size;
  DEBUGMSG("Read cluster %d at %08lx[sector %08lx]", n, pos, pos>>9);
  
  if (pos + _cluster_size > _size) {
    cerr << "Attempt to read " << _cluster_size << " bytes at " << pos << " which is after end of disk " << _size << " !" << endl;
    return -EFBIG;
  }

  if (lseek(_fd, pos, SEEK_SET) == -1) {
    cerr << "lseek(" << pos << "): " << strerror(errno) << endl;
    return -EIO;
  }

  ssize_t bytesread =::read(_fd, _buffer, _cluster_size);

  if (bytesread < 0) {
    cerr << "read: " << strerror(errno) << endl;
    return errno;
  }

  if ((unsigned off_t) bytesread != _cluster_size)
    cerr << "Warning: read returned " << (_cluster_size - bytesread) << " bytes too few" << endl;

  if (_type == TF_4000) {
    for (uint16_t * e = (uint16_t *) (_buffer + _cluster_size), *p = (uint16_t *) _buffer; p != e; ++p)
      *p = bswap_16(*p);
  } else {
    for (uint32_t * e = (uint32_t *) (_buffer + _cluster_size), *p = (uint32_t *) _buffer; p != e; ++p)
      *p = bswap_32(*p);
  }
  return 0;
}

// *************************************************************************
// tfdisk::gen_inodes (private helper function)
// read and parse directory at cluster

int
tfdisk::gen_inodes(tfinode_ptr dir)
{
  int err;
  tf_entry_t *entry = (tf_entry_t *) _buffer;
  uint32_t tsdb = ~uint32_t(0);
  
  assert(sizeof(tf_entry_t) == 0x80);

  DEBUGMSG("gen_inodes for %p", dir);

  if (!dir)
    return -EINVAL;
  if (!S_ISDIR(dir->st.st_mode))
    return -ENOTDIR;
  
  if (dir->count >= 0) { // already cached
    DEBUGMSG("Already cached with %d entries", dir->count);
    return 0;
  }

  cluster_t cluster = dir->entry.start_cluster;
  DEBUGMSG("Not cached, reading cluster %d", cluster);  
  // FIXME: handle directories spanning multiple clusters!
  err = read_cluster(cluster);
  if (err)
    return err;
  if (entry->type != 0xF1) {
    DEBUGMSG("Wrong directory type (%02x != F1) at cluster %ld", entry->type, cluster);
    return -EINVAL;
  }

  // first unused entry in dir, incl. deleted items
  int first_unused = (_cluster_size - swap32(entry->empty_in_last_cluster)) / sizeof(tf_entry_t);
  inode_t ino = _inodes.size();
  DEBUGMSG("read_dir cluster %ld: %d entries, first inode %d", cluster, first_unused, ino);
  dir->first = ino;
  dir->count = 0; // count valid entries
  
  // traverse directory, generate inodes
  
  for (int dir_entry = 0; dir_entry < first_unused; ++entry, ++dir_entry) {
    mode_t mode;
//    hexdump((unsigned char *)entry, sizeof(tf_entry_t), stderr, "Entry");
    // types
    // ff : deleted
    // f1 : __ROOT__
    // f2 : directory
    // f3 : hidden
    // d1 : normal
    switch (entry->type) {
      case 0xd1: mode = S_IFREG | 0444; break; /* _r__r__r__ file */
      case 0xd0: mode = S_IFREG | 0400; break; /* _r________ hidden file */
      case 0xf0: mode = 0; break; /* '..' -> see filler() in main() */
      case 0xf1: mode = 0; break; /* '.' -> see filler() in main() */
      case 0xf2: mode = S_IFDIR | 0555; break; /* dr_xr_xr_x dir */
      case 0xf3: mode = S_IFDIR | 0500; break; /* dr_x______ hidden dir */
      case 0xff: mode = 0; break; /* deleted */ 
      default: mode = 0;
	DEBUGMSG( "Unknown type %02x\n", entry->type);
        break;
    }
    if (mode == 0) {
      continue;                 // Ignore entry
    }

    tfinode_ptr inode(new tfinode);
    memset(inode, 0, sizeof (struct tfinode));
    inode->first = 0;
    inode->count = -1;
    _inodes.push_back(inode);
    dir->count++;
    inode->st.st_ino = ino++;
    DEBUGMSG("Entry %3d/%3d: inode %ld @ %p [%02x]", dir->count, dir_entry, (long int)inode->st.st_ino, (void *)inode, (unsigned char)entry->name[0]);

    inode->entry.type = entry->type;
    (void)conv_name(entry->name, inode->entry.name, sizeof(inode->entry.name)-1);
    DEBUGMSG("\t[%s]'%s' -> '%s' ", entry->channel, entry->name, inode->entry.name);
    inode->entry.start_cluster = swap32(entry->start_cluster); 
    inode->entry.count_of_clusters = swap32(entry->count_of_clusters);
    inode->entry.empty_in_last_cluster = swap32(entry->empty_in_last_cluster);
    memcpy(inode->entry.data, entry->data, sizeof(entry->data));

    struct stat *sp = &(inode->st);
    sp->st_mode = mode;
    sp->st_nlink = 1;
    sp->st_uid = 0;
    sp->st_gid = 0;
    sp->st_rdev = 0;
    sp->st_size = (off_t) inode->entry.count_of_clusters * (off_t) _cluster_size - inode->entry.empty_in_last_cluster;
    sp->st_blksize = _cluster_size;
    sp->st_blocks = ( uint64_t(inode->entry.count_of_clusters) * _cluster_size + 511 ) / 512;
    sp->st_atime = sp->st_mtime = sp->st_ctime = convert_date_time(inode->entry.data);
    DEBUGMSG("\tstart_cluster %d, count %d, empty %d: %ld MB", inode->entry.start_cluster, inode->entry.count_of_clusters, inode->entry.empty_in_last_cluster, sp->st_size/1024L);
    
    if (sp->st_mode) // only for known types
      gen_filesegments(inode);

  }

  return 0;
}

// *************************************************************************
// tfdisk::read_fat (private helper function)

int
tfdisk::read_fat()
{
  int err;
  fat_t fat1, fat2;
  bool fat1bad = false;
  bool fat2bad = false;

  DEBUGMSG("read_fat");
  
  _fatalloc = 0;

  // Read in the cluster with both FATs
  err = read_cluster(FAT_CLUSTER);
  if (err)
    return err;

  // first FAT table
  uint8_t *buf = _buffer + (0x100 << 9);

  fat1.reserve(_fatitems);

  // Walk over the FAT data just read
  // Create fat1 temporrary array

  for (uint32_t i = 0; i < _fatitems; ++i) {
    uint32_t val = (buf[3 * i] << 16) | (buf[3 * i + 1] << 8) | buf[3 * i + 2];
    if (i < 32) {
      DEBUGMSG("FAT[%d] = %x", i, val);
    }
    // Check each item for validity
    if (val < 0xfffffb && val > (_lba_sectors >> 11)) {
      DEBUGMSG( "FAT1 item #%d wrong (%ld/%08x)\n", i, val, val);
      if ((val & 0xffff) == i + 1) {
        val &= 0xffff;
      } else {
        val = ~0L;
        fat1bad = true;
      }
    }
    else if (val != 0xffffff) {
      _fatalloc++;
    }

    fat1.push_back(val);
  }
  DEBUGMSG("FAT %d of %d item allocated", _fatalloc, _fatitems);

  // second FAT table
  buf = _buffer + (0x400 << 9);

  fat2.reserve(_fatitems);

  for (uint32_t i = 0; i < _fatitems; ++i) {
    uint32_t val = (buf[3 * i] << 16) | (buf[3 * i + 1] << 8) | buf[3 * i + 2];

    if (val < 0xfffffb && val > (_lba_sectors >> 11)) {
//      cerr << "FAT2 item #" << i << " wrong (" << val << ")!" << endl;
      DEBUGMSG( "FAT2 item #%d wrong (%ld/%08x)\n", i, val, val);
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
    _fat.swap(fat1);
  else if (!fat2bad)
    _fat.swap(fat2);
  else
  {
    if (_type == TF_4000)
      cerr << "No valid FAT24 found!" << endl;
    else
      cerr << "No valid FAT32 found!" << endl;
    return -EINVAL;
  }

  return 0;
}

// *************************************************************************
// tfdisk::gen_filesegments (private helper function)

int
tfdisk::gen_filesegments(tfinode_ptr inode)
{
//  DEBUGMSG("get_filesegments(%p)", inode);
  inode->seg.clear();
  inode->seg.resize(inode->entry.count_of_clusters + 1);
//   if (i->seg)
//     free(i->seg);
//   i->seg = (tffilesegment *) malloc((i->entry.count_of_clusters + 1) * sizeof(tffilesegment));
  uint32_t cluster = inode->entry.start_cluster;

  uint32_t j;
  uint32_t clusters = uint32_t(_size / (off_t) _cluster_size);

//  DEBUGMSG("%d segments, start %d, count %d", inode->seg.size(), cluster, clusters);
  
  for (j = 0; j < inode->entry.count_of_clusters && cluster < clusters; ++j) {
    inode->seg[j] =
        tffilesegment((off_t) j * (off_t) _cluster_size, _cluster_size,
                      (off_t) (cluster + 1) * (off_t) _cluster_size);
//    DEBUGMSG("Seg %d, cluster %d", j, cluster);
    cluster = _fat[cluster];
//    DEBUGMSG("Next cluster %d", cluster);
  }
  if (j == inode->entry.count_of_clusters) {
    inode->seg[j - 1].size -= inode->entry.empty_in_last_cluster;
//    DEBUGMSG("Size of last: %d", inode->seg[j - 1].size);
  }
  for (; j <= inode->entry.count_of_clusters; ++j) {
    inode->seg[j] = tffilesegment(0, 0, 0);
  }
  // merge adjacent clusters (will reduce number of file operations)

  int maxclusters = (2u << 30) / _cluster_size - 1;
  
//  DEBUGMSG("maxclusters %d", maxclusters);
  for (j = 0; j < inode->entry.count_of_clusters - 1; ++j) {
    uint32_t n = 0;

    for (; j + n + 1 < inode->entry.count_of_clusters; ++n)
      if (inode->seg[j + n + 1].pos != inode->seg[j + n].pos + _cluster_size)
        break;

    for (uint32_t k = 0; k < n; ++k)
      inode->seg[j + k].size += _cluster_size * (((n - k) > maxclusters) ? maxclusters : (n - k));

    j += n;
  }
  return 0;
}
