/**
 *******************************************************************************
 * @file    fs.h
 * @author  Olli Vanhoja
 * @brief   Virtual file system headers.
 * @section LICENSE
 * Copyright (c) 2013, 2014 Olli Vanhoja <olli.vanhoja@cs.helsinki.fi>
 * Copyright (c) 2012, 2013, Ninjaware Oy, Olli Vanhoja <olli.vanhoja@ninjaware.fi>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *******************************************************************************
 */

/** @addtogroup fs
  * @{
  */

#pragma once
#ifndef FS_H
#define FS_H

#include <stdint.h>
#include <sys/types.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <klocks.h>

#define FS_FLAG_INIT        0x01 /*!< File system initialized. */
#define FS_FLAG_FAIL        0x08 /*!< File system has failed. */

#define FS_FILENAME_MAX     255 /*!< Maximum file name length. */
#define PATH_MAX            4096 /* TODO Should be in limits.h? */
#define PATH_DELIMS "/"

/* Some macros for use with flags *********************************************/
/**
 * Test act_flags for FS_FLAG_INIT.
 * @param act_flags actual flag values.
 */
#define FS_TFLAG_INIT(act_flags)       ((act_flags & FS_FLAG_INIT) != 0)

/**
 * Test act_flags for FS_FLAG_FAIL.
 * @param act_flags actual flag values.
 */
#define FS_TFLAG_FAIL(act_flags)       ((act_flags & FS_FLAG_FAIL) != 0)

/**
 * Test act_flags for any of exp_flags.
 * @param act_flags actual flag values.
 * @param exp_flags expected flag values.
 */
#define FS_TFLAGS_ANYOF(act_flags, exp_flags) ((act_flags & exp_flags) != 0)

/**
 * Test act_flags for all of exp_flags.
 * @param act_flags actual flag values.
 * @param exp_flags expected flag values.
 */
#define FS_TFLAGS_ALLOF(act_flags, exp_flags) ((act_flags & exp_flags) == exp_flags)

/* End of macros **************************************************************/

typedef struct vnode {
    ino_t vn_num;               /*!< vnode number. */
    int vn_refcount;
    /*!< Pointer to the vnode in mounted file system. If no fs is mounted on
     *   this vnode then this is a self pointing pointer. */
    struct vnode * vn_mountpoint;
    struct vnode * vn_prev_mountpoint;

    off_t vn_len;               /*!< Length of file. */
    mode_t vn_mode;             /*!< File type part of st_mode sys/stat.h */
    void * vn_specinfo;         /*!< Pointer to an additional information required by the
                                 *   ops. */
    struct fs_superblock * sb;  /*!< Pointer to the super block of this vnode. */
    struct vnode_ops * vnode_ops;
    /* TODO wait queue here */
} vnode_t;

/**
 * File descriptor.
 */
typedef struct file {
    off_t seek_pos;     /*!< Seek pointer. */
    int fdflags;        /*!< File descriptor flags. */
    int oflags;         /*!< File status flags. */
    int refcount;       /*!< Reference count. */
    vnode_t * vnode;
    void * stream;      /*!< Pointer to a special file stream data or info. */
    mtx_t lock;
} file_t;

/**
 * Files that a process has open.
 */
typedef struct files_struct {
        int count;
        struct file * fd[0]; /*!< Open files.
                              *   Thre should be at least following files:
                              *   [0] = stdin
                              *   [1] = stdout
                              *   [2] = stderr
                              */
} files_t;
/**
 * Size of files struct in bytes.
 * @param n is a file count.
 */
#define SIZEOF_FILES(n) (sizeof(files_t) + (n) * sizeof(file_t *))

/**
 * File system.
 */
typedef struct fs {
    char fsname[8];

    int (*mount)(const char * source, uint32_t mode,
                 const char * parm, int parm_len, struct fs_superblock ** sb);
    int (*umount)(struct fs_superblock * fs_sb);
    struct superblock_lnode * sbl_head; /*!< List of all mounts. */
} fs_t;

/**
 * File system superblock.
 */
typedef struct fs_superblock {
    fs_t * fs;
    dev_t vdev_id;          /*!< Virtual dev_id. */
    uint32_t mode_flags;    /*!< Mount mode flags */
    vnode_t * root;         /*!< Root of this fs mount. */
    vnode_t * mountpoint;   /*!< Mount point where this sb is mounted on.
                             *   (only vfs should touch this) */

    /**
     * Get the vnode struct linked to a vnode number.
     * @param[in]  sb is the superblock.
     * @param[in]  vnode_num is the vnode number.
     * @param[out] vnode is a pointer to the vnode.
     * @return Returns 0 if no error; Otherwise value other than zero.
     */
    int (*get_vnode)(struct fs_superblock * sb, ino_t * vnode_num, vnode_t ** vnode);

    /**
     * Delete a vnode reference.
     * Deletes a reference to a vnode and destroys the inode corresponding to the
     * inode if there is no more links and references to it.
     * @param[in] vnode is the vnode.
     * @return Returns 0 if no error; Otherwise value other than zero.
     */
    int (*delete_vnode)(vnode_t * vnode);
} fs_superblock_t;

/**
 * vnode operations struct.
 */
typedef struct vnode_ops {
    /* Operation for open files
     * ------------------------ */
    int (*lock)(file_t * file);
    int (*release)(file_t * file);
    /**
     * Write transfers bytes from buf into file.
     * Writing is begin from offset and ended at offset + count. buf must
     * therefore contain at least count bytes. If offset is past end of the
     * current file the file will be extended; If offset is smaller than file
     * length, the existing data will be overwriten.
     * @param file      is a file stored in ramfs.
     * @param buf       is a buffer where bytes are read from.
     * @param count     is the number of bytes buf contains.
     * @return  Returns the number of bytes written; Otherwise a negative errno
     *          is returned.
     */
    ssize_t (*write)(file_t * file, const void * buf, size_t count);
    /**
     * Read transfers bytes from file into buf.
     * @param file      is a file stored in ramfs.
     * @param buf       is a buffer bytes are written to.
     * @param count     is the number of bytes to be read.
     * @return  Returns the number of bytes read; Otherwise a negative errno
     *          is returned.
     */
    ssize_t (*read)(file_t * file, void * buf, size_t count);
    /**
     * IO Control.
     * Only defined for devices and shall be set NULL if not supported.
     * @param file      is the open file accessed.
     * @param request   is the request number.
     * @param arg       is a pointer to the argument (struct).
     * @param ar_len    is the length of arg.
     * @return          0 ig succeed; Otherwise a negative errno is returned.
     */
    int (*ioctl)(file_t * file, uint32_t request, void * arg, size_t arg_len);
    //int (*mmap)(vnode_t * file, !mem area!);
    /* Directory file operations
     * ------------------------- */
    /**
     * Create a new vnode with S_IFREG and a hard link with specified name for it
     * created in dir.
     * @param dir       is the directory vnode which is used to store the hard link
     *                  created.
     * @param name      is the name of the hard link.
     * @param name_len  is the length of the name.
     * @param[out] result is a pointer to the resulting vnode.
     * @return Zero in case of operation succeed; Otherwise value other than zero.
     */
    int (*create)(vnode_t * dir, const char * name, size_t name_len, mode_t mode,
            vnode_t ** result);
    /**
     * Create a special vnode.
     * @note ops must be set manually after creation of a vnode.
     * @param specinfo  is a pointer to the special info struct.
     * @param mode      is the mode of the new file.
     */
    int (*mknod)(vnode_t * dir, const char * name, size_t name_len, int mode,
            void * specinfo, vnode_t ** result);
    /**
     * Lookup for a hard linked vnode in a directory vnode.
     * @param dir       is a directory in ramfs.
     * @param name      is a filename.
     * @param name_len  is the length of name.
     * @param[out] result is the result of lookup.
     * @return Returns 0 if a vnode was found; Otherwise value other than zero.
     */
    int (*lookup)(vnode_t * dir, const char * name, size_t name_len,
            vnode_t ** result);
    /**
     * Create a hard link.
     * Link vnode into dir with the specified name.
     * @param dir       is the directory where entry will be created.
     * @param vnode     is a vnode where the link will point.
     * @param name      is the name of the hard link.
     * @param name_len  is the length of the name.
     * @return Returns 0 if creating a link succeeded; Otherwise value other than
     *         zero.
     */
    int (*link)(vnode_t * dir, vnode_t * vnode, const char * name,
            size_t name_len);
    int (*unlink)(vnode_t * dir, const char * name, size_t name_len);
    /**
     * Create a directory called name in dir.
     * @param dir       is a directory in ramfs.
     * @param name      is the name of the new directory.
     * @param name_len  is the length of the name.
     * @return Zero in case of operation succeed; Otherwise value other than zero.
     */
    int (*mkdir)(vnode_t * dir,  const char * name, size_t name_len, mode_t mode);
    int (*rmdir)(vnode_t * dir,  const char * name, size_t name_len);
    /**
     * Reads one directory entry from the dir into the struct dirent.
     * @param dir       is a directory in ramfs.
     * @param dirent    is a directory entry struct.
     * @return  Zero in case of operation succeed;
     *          -1 if dir is not a directory;
     *          -2 if end of dir.
     */
    int (*readdir)(vnode_t * dir, struct dirent * d);
    /* Operations specified for any file type */
    /**
     * Get file status.
     */
    int (*stat)(vnode_t * vnode, struct stat * buf);
} vnode_ops_t;


/**
 * Superblock list type.
 */
typedef struct superblock_lnode {
    fs_superblock_t sbl_sb; /*!< Superblock struct. */
    struct superblock_lnode * next; /*!< Pointer to the next super block. */
} superblock_lnode_t;

/**
 * fs list type.
 */
typedef struct fsl_node {
    fs_t * fs; /*!< Pointer to the file system struct. */
    struct fsl_node * next; /*!< Pointer to the next fs list node. */
} fsl_node_t;

/**
 * Sperblock iterator.
 */
typedef struct sb_iterator {
    fsl_node_t * curr_fs; /*!< Current fs list node. */
    superblock_lnode_t * curr_sb; /*!< Current superblock of curr_fs. */
} sb_iterator_t;


void fs_init(void) __attribute__((constructor));

/* VFS Function Prototypes */

/**
 * Register a new file system driver.
 * @param fs file system control struct.
 */
int fs_register(fs_t * fs);

/**
 * Lookup for vnode by path.
 * @param[out]  result  is the target where vnode struct is stored.
 * @param[in]   root    is the vnode where search is started from.
 * @param       str     is the path.
 * @param       oflags  Tested flags are: O_DIRECTORY, O_NOFOLLOW
 * @return  Returns zero if vnode was found;
 *          error code -errno in case of an error.
 */
int lookup_vnode(vnode_t ** result, vnode_t * root, const char * str, int oflags);

/**
 * Walks the file system for a process and tries to locate and lock vnode
 * corresponding to a given path.
 */
int fs_namei_proc(vnode_t ** result, const char * path);

/**
 * Mount file system.
 * @param target    is the target mount point directory.
 * @param vonde_dev is a vnode of a device or other mountable file system
 *                  device.
 * @param fsname    is the name of the file system type to mount. This
 *                  argument is optional and if left out fs_mount() tries
 *                  to determine the file system type from existing information.
 */
int fs_mount(vnode_t * target, const char * source, const char * fsname,
        uint32_t flags, const char * parm, int parm_len);

/**
 * Find registered file system by name.
 * @param fsname is the name of the file system.
 * @return The file system structure.
 */
fs_t * fs_by_name(const char * fsname);

/**
 * Initialize a file system superblock iterator.
 * Iterator is used to iterate over all superblocks of mounted file systems.
 * @param it is an untilitialized superblock iterator struct.
 */
void fs_init_sb_iterator(sb_iterator_t * it);

/**
 * Iterate over superblocks of mounted file systems.
 * @param it is the iterator struct.
 * @return The next superblock or 0.
 */
fs_superblock_t * fs_next_sb(sb_iterator_t * it);

/**
 * Get next free pseudo fs minor code.
 */
unsigned int fs_get_pfs_minor(void);

/**
 * Check file permissions against oflag(s).
 * @param stat      is a pointer to stat struct of the file.
 * @param oflags    specifies the verified oflags.
 * @return  Returns negative errno in case of error or improper permissions;
 *          Otherwise zero.
 */
int chkperm_cproc(struct stat * stat, int oflags);

/**
 * Set parameters of a file descriptor.
 * @param vnode     is a vnode.
 * @param oflags    specifies the opening flags.
 * @return -errno.
 */
int fs_fildes_set(file_t * fildes, vnode_t * vnode, int oflags);

/**
 * Create a new file descriptor to the first empty spot.
 */
int fs_fildes_create_cproc(vnode_t * vnode, int oflags);

/**
 * Get next free file descriptor for the current process.
 * @param new_file  is the file to be stored in the next free position from
 *                  start.
 * @param start     is the start offset.
 */
int fs_fildes_cproc_next(file_t * new_file, int start);

/**
 * Increment or decrement a file descriptor reference count and free the
 * descriptor if there is no more refereces to it.
 * @param files     is the files struct where fd is searched for.
 * @param fd        is the file descriptor to be updated.
 * @param count     is the value to be added to the refcount.
 */
file_t * fs_fildes_ref(files_t * files, int fd, int count);

/**
 * Close file open for curproc.
 * @param fildes    is the file descriptor number.
 * @return  0 if file was closed;
 *          Otherwise a negative value representing errno.
 */
int fs_fildes_close_cproc(int fildes);

/**
 * Read or write to a open file of the current process.
 * @param fildes    is the file descriptor nuber.
 * @param buf       is the buffer.
 * @param nbytes    is the amount of bytes to be read/writted.
 * @return  Return the number of bytes actually read/written from/to the file
 *          associated with fildes; Otherwise a negative value representing
 *          errno.
 */
ssize_t fs_readwrite_cproc(int fildes, void * buf, size_t nbyte, int oper);

/**
 * Create a new file by using fs specific create() function.
 * File will be created relative to the attributes of a current process.
 * @param[in]   path    is a path to the new file.
 * @param[in]   mode    selects the file mode.
 * @param[out]  result  is the new vnode created.
 * @return      0 if succeed; Negative error code representing errno in case of
 *              failure.
 */
int fs_creat_cproc(const char * path, mode_t mode, vnode_t ** result);

/**
 * Remove link to a file.
 */
int fs_unlink_curproc(const char * path, size_t path_len);

/**
 * Remove link to a file relative to fd.
 */
int fs_unlinkat_curproc(int fd, const char * path, int flag);

/**
 * Create a new directory relative to the current process.
 */
int fs_mkdir_curproc(const char * pathname, mode_t mode);

#endif /* FS_H */

/**
  * @}
  */
