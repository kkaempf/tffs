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

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include "tfdisk.h"

static struct fuse_operations tffs_oper = { };
static tfdisk *tf;

// *************************************************************************
// tffs_getattr

static int tffs_getattr(const char *path, struct stat *stbuf)
{
  if (path[0] != '/')
    return -ENOENT;

  if (path[1] == 0) {
    memset(stbuf, 0, sizeof(struct stat));
    stbuf->st_ino = 1;
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2;
    return 0;
  }

  for (std::vector < tfinode_ptr >::const_iterator it = tf->entries().begin();
       it != tf->entries().end(); ++it)
    if (!strcmp(path + 1, (*it)->d.name)) {
      memcpy(stbuf, &(*it)->s, sizeof(struct stat));
      return 0;
    }

  return -ENOENT;
}

// *************************************************************************
// tffs_readdir

static int tffs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi)
{
  if (strcmp(path, "/") != 0)
    return -ENOENT;

  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);

  for (std::vector < tfinode_ptr >::const_iterator it = tf->entries().begin();
       it != tf->entries().end(); ++it)
    filler(buf, (*it)->d.name, &(*it)->s, 0);

  return 0;
}

// *************************************************************************
// tffs_open

static int tffs_open(const char *path, struct fuse_file_info *fi)
{
  if (path[0] != '/')
    return -ENOENT;
  if (path[1] == 0)
    return -EISDIR;

  for (std::vector < tfinode_ptr >::const_iterator it = tf->entries().begin();
       it != tf->entries().end(); ++it)
    if (!strcmp(path + 1, (*it)->d.name)) {
      if ((fi->flags & 3) != O_RDONLY)
        return -EACCES;
      fi->fh = (*it)->s.st_ino; // inode number
      return 0;
    }

  return -ENOENT;
}

// *************************************************************************
// tffs_read

static int tffs_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
  return tf->read(fi->fh, buf, size, offset);
}

// *************************************************************************
// tffs_statfs

static int tffs_statfs(const char *path, struct statfs *sfs)
{
// sfs->f_type = ('t'<<24)|('f'<<16)|('f'<<8)|('s');
  sfs->f_bsize = tf->getclustersize();
  sfs->f_blocks = tf->getsize() / sfs->f_bsize;
  sfs->f_bfree = sfs->f_bavail = 0;
  sfs->f_files = tf->entries().size() + 1;
  sfs->f_ffree = 0;
// sfs->f_fsid.__val[0] = tf->fsid1();
// sfs->f_fsid.__val[1] = tf->fsid2() ^ sfs->f_fsid.__val[0];
  sfs->f_namelen = 512;

  return 0;
}

// *************************************************************************
// usage (helper function)

static void usage(const char *progname)
{
  const char *fusehelp[] = { progname, "-ho", NULL };

  fprintf(stderr, "usage: %s device mountpoint [options]\n" "\n", progname);
  fuse_main(2, (char **) fusehelp, &tffs_oper);
  exit(1);
}

// *************************************************************************
// main

int main(int argc, char *argv[])
{
  // Fill fuse_operations structure
  tffs_oper.getattr = tffs_getattr;
  tffs_oper.readdir = tffs_readdir;
  tffs_oper.open = tffs_open;
  tffs_oper.read = tffs_read;
  tffs_oper.statfs = tffs_statfs;

  // Extract device name from command line
  char **newargv = (char **) malloc((argc + 2) * sizeof(char *));
  char *device = NULL;

  int newargc = 1;

  newargv[newargc++] = "-r";

  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] != '-' && !device)
      device = argv[i];
    else
      newargv[newargc++] = argv[i];
  }

  if (!device)
    usage(argv[0]);

  newargv[0] = device;

  // Create tfdisk object
  tfdisk tfd(device);

  tf = &tfd;

  if (!tfd.open()) {
    //fprintf(stderr,"%s: %s\n",argv[0],strerror(errno));
    exit(1);
  }
  
  // Run FUSE main loop
  newargv[newargc] = NULL;
  return fuse_main(newargc, newargv, &tffs_oper);
}
