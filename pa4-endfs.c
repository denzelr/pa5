/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  Minor modifications and note by Andy Sayler (2012) <www.andysayler.com>

  Source: fuse-2.8.7.tar.gz examples directory
  http://sourceforge.net/projects/fuse/files/fuse-2.X/

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  gcc -Wall `pkg-config fuse --cflags` fusexmp.c -o fusexmp `pkg-config fuse --libs`

  Note: This implementation is largely stateless and does not maintain
        open file handels between open and release calls (fi->fh).
        Instead, files are opened and closed as necessary inside read(), write(),
        etc calls. As such, the functions that rely on maintaining file handles are
        not implmented (fgetattr(), etc). Those seeking a more efficient and
        more complete implementation may wish to add fi->fh support to minimize
        open() and close() calls and support fh dependent functions.

*/

#define FUSE_USE_VERSION 28
#define HAVE_SETXATTR

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

static const char FLAG[] = "user.pa4-endfs.encrypted";

#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#define PATH 255
#define ENOATTR ENODATA
#endif

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>

#endif

#include "aes-crypt.h"

struct xmp {
	char *input; //mirror path
	char *output; //mount path
	char *key; //password
};

#define XMP_INFO ((struct xmp *) fuse_get_context()->private_data)

static int mirror(char mirDir[PATH], const char *path)
{
	char *dir = XMP_INFO->input;
	strcpy(mirDir, dir);
	strcat(mirDir, path);
	return 0;
}

static int xmp_getattr(const char *path, struct stat *stbuf)
{
	int res;
	char newPath[PATH];
	mirror(newPath,path);

	res = lstat(newPath, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_access(const char *path, int mask)
{
	int res;

	char newPath[PATH];
	mirror(newPath,path);

	res = access(newPath, mask);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_readlink(const char *path, char *buf, size_t size)
{
	int res;

	char newPath[PATH];
	mirror(newPath,path);

	res = readlink(newPath, buf, size - 1);
	if (res == -1)
		return -errno;

	buf[res] = '\0';
	return 0;
}


static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	DIR *dp;
	struct dirent *de;

	(void) offset;
	(void) fi;

	char newPath[PATH];
	mirror(newPath,path);

	dp = opendir(newPath);
	if (dp == NULL)
		return -errno;

	while ((de = readdir(dp)) != NULL) {
		struct stat st;
		memset(&st, 0, sizeof(st));
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;
		if (filler(buf, de->d_name, &st, 0))
			break;
	}

	closedir(dp);
	return 0;
}

static int xmp_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int res;

	char newPath[PATH];
	mirror(newPath,path);

	/* On Linux this could just be 'mknod(path, mode, rdev)' but this
	   is more portable */
	if (S_ISREG(mode)) {
		res = open(newPath, O_CREAT | O_EXCL | O_WRONLY, mode);
		if (res >= 0)
			res = close(res);
	} else if (S_ISFIFO(mode))
		res = mkfifo(newPath, mode);
	else
		res = mknod(newPath, mode, rdev);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_mkdir(const char *path, mode_t mode)
{
	int res;

	char newPath[PATH];
	mirror(newPath,path);

	res = mkdir(newPath, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_unlink(const char *path)
{
	int res;

	char newPath[PATH];
	mirror(newPath,path);

	res = unlink(newPath);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_rmdir(const char *path)
{
	int res;

	char newPath[PATH];
	mirror(newPath,path);

	res = rmdir(newPath);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_symlink(const char *from, const char *to)
{
	int res;

	res = symlink(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_rename(const char *from, const char *to)
{
	int res;

	res = rename(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_link(const char *from, const char *to)
{
	int res;

	res = link(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_chmod(const char *path, mode_t mode)
{
	int res;

	char newPath[PATH];
	mirror(newPath,path);

	res = chmod(newPath, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_chown(const char *path, uid_t uid, gid_t gid)
{
	int res;

	char newPath[PATH];
	mirror(newPath,path);

	res = lchown(newPath, uid, gid);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_truncate(const char *path, off_t size)
{
	int res;

	char newPath[PATH];
	mirror(newPath,path);

	res = truncate(newPath, size);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_utimens(const char *path, const struct timespec ts[2])
{
	int res;
	struct timeval tv[2];

	char newPath[PATH];
	mirror(newPath,path);

	tv[0].tv_sec = ts[0].tv_sec;
	tv[0].tv_usec = ts[0].tv_nsec / 1000;
	tv[1].tv_sec = ts[1].tv_sec;
	tv[1].tv_usec = ts[1].tv_nsec / 1000;

	res = utimes(newPath, tv);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_open(const char *path, struct fuse_file_info *fi)
{
	int res;

	char newPath[PATH];
	mirror(newPath,path);

	res = open(newPath, fi->flags);
	if (res == -1)
		return -errno;

	close(res);
	return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
	int fd;
	int res;
	FILE *fp, *tmp;
	int action = 0;

	char newPath[PATH];
	mirror(newPath, path);

	(void) fi;

	//Uncomment out this section to mount encrypted code////////
	fp = fopen(newPath, "r");
	if (fp == NULL)
			return -errno;
	tmp = tmpfile();
	if (tmp == NULL)
			return -errno;

	if(!do_crypt(fp, tmp, action, XMP_INFO->key)){
		fprintf(stderr, "do_crypt failed\n");
    }
    fclose(fp);
    fseek(tmp, offset, SEEK_SET);
	res = fread(buf, 1, size, tmp);
	if (res == -1)
		res = -errno;
	fclose(tmp);
	/////////////////////////////////////////////////////////////

	//Uncomment this section to mount unencrypted code///////////
	/*fd = open(newPath, O_RDONLY);
	if (fd == -1)
		return -errno;
	
	res = pread(fd, buf, size, offset);
	if (res == -1)
		res = -errno;

	close(fd);*/
	/////////////////////////////////////////////////////////////

	return res;
}


static int xmp_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
	int fd;
	int res;
	FILE *fp, *tmp;

	char newPath[PATH];
	mirror(newPath,path);

	(void) fi;

	//Uncomment this section to write unencrypted code to mnt////////
	fp = fopen(newPath, "r");
	if (fp == NULL)
		return -errno;
	
	tmp = tmpfile();
	if (tmp == NULL)
		return -errno;
	
	do_crypt(fp, tmp, 0, XMP_INFO->key);
	fclose(fp);

	fseek(tmp, offset, SEEK_SET);
	res = fwrite(buf, 1, size, tmp);
	if (res == -1)
		res = -errno;
		
	fp = fopen(newPath, "w");
	fseek(tmp, 0, SEEK_SET);
	do_crypt(tmp, fp, 1, XMP_INFO->key);

	fclose(tmp);
	fclose(fp);
	/////////////////////////////////////////////////////////////////////

	//Uncomment this section to write encrypted code to mnt///////////
	/*fd = open(newPath, O_WRONLY);
	if (fd == -1)
		return -errno;

	res = pwrite(fd, buf, size, offset);
	if (res == -1)
		res = -errno;*/

	close(fd);

	return res;
}

static int xmp_statfs(const char *path, struct statvfs *stbuf)
{
	int res;

	char newPath[PATH];
	mirror(newPath,path);

	res = statvfs(newPath, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_create(const char* path, mode_t mode, struct fuse_file_info* fi) {

    (void) fi;

    int res;
    char newPath[PATH];
	mirror(newPath,path);
    res = creat(newPath, mode);
    if(res == -1)
	return -errno;

    close(res);

    return 0;
}


static int xmp_release(const char *path, struct fuse_file_info *fi)
{
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */

	(void) path;
	(void) fi;
	return 0;
}

static int xmp_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */

	(void) path;
	(void) isdatasync;
	(void) fi;
	return 0;
}

#ifdef HAVE_SETXATTR
static int xmp_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
{
	char newPath[PATH];
	mirror(newPath,path);
	int res = lsetxattr(newPath, name, value, size, flags);
	if (res == -1)
		return -errno;
	return 0;
}

static int xmp_getxattr(const char *path, const char *name, char *value,
			size_t size)
{
	char newPath[PATH];
	mirror(newPath,path);
	int res = lgetxattr(newPath, name, value, size);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_listxattr(const char *path, char *list, size_t size)
{
	char newPath[PATH];
	mirror(newPath,path);
	int res = llistxattr(newPath, list, size);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_removexattr(const char *path, const char *name)
{
	char newPath[PATH];
	mirror(newPath,path);
	int res = lremovexattr(newPath, name);
	if (res == -1)
		return -errno;
	return 0;
}
#endif /* HAVE_SETXATTR */

static struct fuse_operations xmp_oper = {
	.getattr	= xmp_getattr,
	.access		= xmp_access,
	.readlink	= xmp_readlink,
	.readdir	= xmp_readdir,
	.mknod		= xmp_mknod,
	.mkdir		= xmp_mkdir,
	.symlink	= xmp_symlink,
	.unlink		= xmp_unlink,
	.rmdir		= xmp_rmdir,
	.rename		= xmp_rename,
	.link		= xmp_link,
	.chmod		= xmp_chmod,
	.chown		= xmp_chown,
	.truncate	= xmp_truncate,
	.utimens	= xmp_utimens,
	.open		= xmp_open,
	.read		= xmp_read,
	.write		= xmp_write,
	.statfs		= xmp_statfs,
	.create         = xmp_create,
	.release	= xmp_release,
	.fsync		= xmp_fsync,
#ifdef HAVE_SETXATTR
	.setxattr	= xmp_setxattr,
	.getxattr	= xmp_getxattr,
	.listxattr	= xmp_listxattr,
	.removexattr	= xmp_removexattr,
#endif
};


//Pfeiffer, Joseph. Writing a FUSE Filesystem: a Tutorial. January 10th, 2011. http:
//www.cs.nmsu.edu/~pfeiffer/fuse-tutorial/
//bbfs.c
int main(int argc, char *argv[])
{
	umask(0);

	if (argc < 4){
		printf("Not Enough Arguments... Exiting\n");
		return 1;
	}

	struct xmp *mirror;
	mirror = malloc(sizeof(struct xmp));
	mirror->input = realpath(argv[2],NULL);
	mirror->output = realpath(argv[3],NULL);
	mirror->key = argv[1];
	argv[1] = argv[3];
    argv[2] = argv[4];
    argc = argc - 2;

	return fuse_main(argc, argv, &xmp_oper, mirror);
}
