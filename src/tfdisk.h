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

#include <stdint.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include "topfield.h"

#include <boost/shared_ptr.hpp>
typedef boost::shared_ptr<directory_entry_t> directory_entry_ptr;
typedef boost::shared_ptr<tf_dir_t> tf_dir_ptr;

struct tffilesegment
  {
  int64_t offset;
  int64_t size;
  int64_t pos;
  tffilesegment(int64_t _offset, uint32_t _size, int64_t _pos) : offset(_offset),size(_size),pos(_pos) {}
  };

struct tfinode
  {
  tf_dir_t d;
  struct stat s;
  tffilesegment *seg;
  
  tfinode() : seg(0) {}
  ~tfinode();
  };
typedef boost::shared_ptr<tfinode> tfinode_ptr;
  
class tfdisk
  {
  private:
    /// device file name
    std::string devfn;
    /// device file descriptor
    int fd;
    /// device size
    off_t size;
    /// number of lba sectors
    int64_t lba_sectors;
    /// size of cluster
    uint32_t cluster_size;
    /// number of FAT items
    uint32_t fatitems;
    /// cluster buffer
    uint8_t *buffer;
    /// FAT
    uint32_t *fat;
   
    std::vector<tfinode_ptr> entry;

    bool readcluster(int n);
    bool parse_dir(uint32_t cluster, uint8_t mask, uint8_t value);
    bool read_fat();  
    void gen_filesegments(tfinode_ptr &i);
    
  public:
    tfdisk(const char *device) : devfn(device), fd(-1),size(-1),buffer(0),fat(0) {}
    ~tfdisk();
    
    bool open();
    void close();
    const std::vector<tfinode_ptr> &entries() { return entry; }
    off_t getsize() { return size; }
    uint32_t getclustersize() { return cluster_size; }
    ssize_t read(uint32_t ino, char *buf, size_t size, off_t offset);
    uint32_t fsid1() 
     {
     uint32_t t=0;
     if (fat) for(unsigned int i=0;i<fatitems;++i) t^=fat[i];
     return t;
     }
    uint32_t fsid2()
     {
     return fatitems^cluster_size^lba_sectors^size^fd;
     }
     
  };

#endif // __TFDISK_H__
