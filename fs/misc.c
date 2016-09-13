#include "type.h"
#include "stdio.h"
#include "const.h"
#include "protect.h"
#include "string.h"
#include "fs.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "global.h"
#include "keyboard.h"
#include "proto.h"
#include "hd.h"
#include "fs.h"

PUBLIC int strip_path(char * filename, const char * pathname, struct inode** ppinode)
{
	char * t = filename;
	const char * s = pathname;

	if (s == 0) return -1;
	if (*s == '/') s++;

	while (*s) {
		if (*s == '/') return -1;
		*t++ = *s++;
		if (t - filename >= MAX_FILENAME_LEN) break;
	}
	*t = 0;
	*ppinode = root_inode;

	return 0;
}

PUBLIC int search_file(char * path)
{
	int i, j;
	char filename[MAX_PATH];
	struct inode * dir_inode;
	memset(filename, 0, MAX_FILENAME_LEN);
	if (strip_path(filename, path, &dir_inode) != 0) return 0;
	if (filename[0] == 0) return dir_inode->i_num;

	int m = 0;
	struct dir_entry * pde;
	int dir_blk0_nr = dir_inode->i_start_sect;
	int nr_dir_blks = (dir_inode->i_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
	int nr_dir_entries = dir_inode->i_size / DIR_ENTRY_SIZE;
	for (i = 0; i < nr_dir_blks; i++) {
		RD_SECT(dir_inode->i_dev, dir_blk0_nr + i);
		pde = (struct dir_entry *)fsbuf;
		for (j = 0; j < SECTOR_SIZE / DIR_ENTRY_SIZE; j++,pde++) {
			if (memcmp(filename, pde->name, MAX_FILENAME_LEN) == 0) return pde->inode_nr;
			if (++m > nr_dir_entries) break;
		}
		if (m > nr_dir_entries) break;
	}

	return 0;
}

PRIVATE struct stat fs_misc_init_stat(struct inode * pin)
{
	struct stat s;
	s.st_ino = pin->i_num;
	s.st_dev = pin->i_dev;
	s.st_mode= pin->i_mode;
	s.st_rdev= is_special(pin->i_mode) ? pin->i_start_sect : NO_DEV;
	s.st_size= pin->i_size;
	return s;
}

PUBLIC int do_stat()
{
	char filename[MAX_PATH];
	char pathname[MAX_PATH];
	int src = fs_msg.source;
	int name_len = fs_msg.NAME_LEN;
	assert(name_len < MAX_PATH);

	phys_copy((void*)va2la(TASK_FS, pathname), (void*)va2la(src, fs_msg.PATHNAME), name_len);
	pathname[name_len] = 0;

	int inode_nr = search_file(pathname);
	if (inode_nr == INVALID_INODE) { printl("{FS} FS::do_stat():: search_file() returns invalid inode: %s\n", pathname); return -1;}

	struct inode * pin = 0;
	struct inode * dir_inode;
	if (strip_path(filename, pathname, &dir_inode) != 0) assert(0);
	pin = get_inode(dir_inode->i_dev, inode_nr);

	put_inode(pin);
	struct stat s = fs_misc_init_stat(pin);
	phys_copy((void*)va2la(src, fs_msg.BUF), (void*)va2la(TASK_FS, &s), sizeof(struct stat));

	return 0;
}

