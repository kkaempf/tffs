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

#ifndef __TFDISK_H__
#define __TFDISK_H__

#include <sys/stat.h>
#include <vector>
#include <string>
#include "topfield.h"

//#include <tr1/memory>

typedef tf_entry_t* tf_entry_ptr;

typedef uint32_t inode_t;
#define INODE_ROOT 1

struct tffilesegment
{
  int64_t offset;
  int64_t size;
  int64_t pos;
  tffilesegment() : offset(0), size(0), pos(0) {}
  tffilesegment(int64_t _offset, uint32_t _size, int64_t _pos) : offset(_offset),size(_size),pos(_pos) {}
};

struct tfinode;
typedef struct tfinode * tfinode_ptr;
struct tfinode
{
  tf_entry_t entry;
  struct stat st;
  typedef std::vector<tffilesegment> seg_t;
  seg_t seg;
  inode_t first; // first dir entry, if dir, 0 else
  int32_t count; // # of dir entries, if dir, -1 else
};
  
typedef enum {
  TF_UNKNOWN = 0,
    TF_4000 = 1,
    TF_5000 = 2
} tf_type_t;

class tfdisk
{
  private:
    tf_type_t _type;
    /// device file name
    std::string _devfn;
    /// device file descriptor
    int _fd;
    /// device size
    off_t _size;
    /// number of lba sectors
    int64_t _lba_sectors;
    /// size of cluster
    uint32_t _cluster_size;
    /// number of FAT items
    uint32_t _fatitems;
    /// number of allocated FAT items
    uint32_t _fatalloc;
    /// cluster buffer
    uint8_t *_buffer;
    /// FAT
    typedef std::vector<uint32_t> fat_t;
    fat_t _fat;
   
    // all inodes, indexed by inode #, starting from 1
    std::vector<tfinode_ptr> _inodes;

    // read_* functions to real I/O

    // read fat table
    int read_fat();  

    // read cluster to buffer
    int read_cluster(cluster_t n);
   
    // collect inodes with same parent
    // inodes within a directory are consecutive, 
    int collect_inodes(tfinode_ptr parent, inode_t *first, int *count);
   
    // generate inodes from directory, return error
    int gen_inodes(tfinode_ptr inode);
   
    // generate file segment data, return error
    int gen_filesegments(tfinode_ptr inode);

    // filename conversion
   char *conv_name(const char *inbuf, char *outbuf, size_t size);

  public:
    tfdisk(const char *device) : _devfn(device), _fd(-1), _size(-1), _buffer(NULL) {}
    ~tfdisk();
    
    int open(); // returning errno
    void close();

    off_t getsize() { return _size; }
    uint32_t getclustersize() { return _cluster_size; }
    uint32_t getfatsize() { return _fatitems; }
    uint32_t getfatalloc() { return _fatalloc; }
    // get inode for path
    tfinode_ptr inode4path(const char *path, int *err);
    // get inode by index
    tfinode_ptr inodeptr(uint32_t n);
   
    // read directory (inode pointing to it), return errno
    // pass index to first new inoded and count back 
    int readdir(tfinode_ptr inode, inode_t *first, int *count);
    ssize_t read(inode_t ino, char *buf, size_t size, off_t offset);

    uint32_t fsid1() 
     {
     uint32_t t=0;
     for (fat_t::const_iterator it=_fat.begin(),stop=_fat.end();it!=stop;++it)
        t^=*it;
     return t;
     }
    uint32_t fsid2()
     {
     return _fatitems^_cluster_size^_lba_sectors^_size^_fd;
     }
     
};

#endif // __TFDISK_H__
