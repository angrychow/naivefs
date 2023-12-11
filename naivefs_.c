
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/kern_levels.h>

#include "naivefs.h"

// make intellisense happy

// #define size_t int64_t
// #define min(x,y) (x) < (y) ? (x) : (y)
// #define bool char


struct file_blk block[MAX_FILES + 1];

int curr_count = 0;

// vfs interface to be implemented
static struct inode_operations naive_fs_inode_ops;

static int get_block(void) {
    int i;
    for(i = 2; i < MAX_FILES; i++) {
        if(!block[i].busy) {
            block[i].busy = 1;
            return i;
        }
    }
    return BLOCK_NOT_FOUND;
}

// 读目录

static int naive_fs_readdir(struct file *filp, struct dir_context *ctx) {
    if(!dir_emit_dots(filp, ctx)) return 0;

    struct file_blk *blk;
    struct dir_entry *entry;
    struct dentry *dentry = filp->f_path.dentry;
    loff_t pos = filp->f_pos;
    if(pos) return 0;
    blk = (struct file_blk *)dentry->d_inode->i_private;
    if(!S_ISDIR(blk->mode)) {
        return -ENOTDIR;
    }
    entry = (struct dir_entry *)&blk->data;
    int i;
    for(i = 0; i < blk->dir_children; i++) {
        if(
            !dir_emit(
                ctx,
                entry[i].filename,
                strlen(entry[i].filename),
                entry[i].idx,
                DT_UNKNOWN
            )
        ) {
            break;
        }
        filp->f_pos += sizeof(struct dir_entry);
        pos += sizeof(struct dir_entry);
    }
    return 0;
}

ssize_t naive_fs_read(
    struct file *filp,
    char __user *buf,
    size_t len,
    loff_t *ppos
) {
    struct file_blk *blk = filp->f_path.dentry->d_inode->i_private;
    char *buffer = (char*) blk->data;
    printk(KERN_EMERG "Read: %lld", *ppos);
    if(*ppos >= blk->file_size) { // 越界
        return 0;
    }
    len = min((size_t)blk->file_size, len); // less than blk size
    if(copy_to_user(buf, buffer, len)) {
        return -EFAULT;
    }
    *ppos += len;
    return len;
}

ssize_t naive_fs_write(
    struct file *filp,
    const char __user *buf,
    size_t len,
    loff_t *ppos
) {
    struct file_blk *blk = filp->f_path.dentry->d_inode->i_private;
    char *buffer = (char*) blk->data;
    buffer += *ppos;

    printk(KERN_EMERG "Read: %d", len);

    if(*ppos + len >= 512) { // 越界
        return -EFAULT;
    }
    if(copy_from_user(buffer, buf, len)) {
        return -EFAULT;
    }
    *ppos += len;
    blk->file_size += *ppos;
    return len;
}

const struct file_operations naive_fs_file_operations = {
    .read = naive_fs_read,
    .write = naive_fs_write,
};

static const struct file_operations naive_fs_dir_operations = {
    .owner = THIS_MODULE,
    .read		= generic_read_dir,
    .iterate_shared	= naive_fs_readdir, // generic_read_dir 调用 iterate_shared
};

static int naive_fs_do_create(
    struct inode *dir, // 父目录项
    struct dentry *dentry, // 新文件的目录项
    umode_t mode // 模式位图
) {
    struct inode *inode;
    struct super_block *sb;
    struct dir_entry *entry;
    struct file_blk *blk, *pblk;
    int idx;

    sb = dir->i_sb;

    if(curr_count >= MAX_FILES) {
        return -ENOSPC;
    }

    if(!S_ISDIR(mode) && !S_ISREG(mode)) {
        return -EINVAL;
    }

    inode = new_inode(sb);
    if(!inode) {
        return -ENOMEM;
    }

    inode->i_sb = sb;
    inode->i_op = &naive_fs_inode_ops;
    struct timespec64 ts;
    ktime_get_ts64(&ts);
    inode->i_atime = ts;
    inode->i_mtime = ts;

    idx = get_block();
    if(idx == BLOCK_NOT_FOUND) { // 理论上不会发生
        return -ENOMEM;
    }

    printk(KERN_EMERG "create: %d", idx);

    blk = &block[idx];
    inode->i_ino = idx;
    blk->mode = mode;
    curr_count ++;

    if(S_ISDIR(mode)) {
        blk->dir_children = 0;
        inode->i_fop = &naive_fs_dir_operations;
    } else if(S_ISREG(mode)) {
        blk->file_size = 0;
        inode->i_fop = &naive_fs_file_operations;
    }

    inode->i_private = blk;
    pblk = (struct file_blk *) dir->i_private;
    entry = (struct dir_entry *)&pblk->data;
    entry +=pblk->dir_children;
    entry->idx = idx;
    pblk->dir_children ++;
    strcpy(entry->filename, dentry->d_name.name);
    inode_init_owner(
        inode,
        dir,
        mode
    );
    d_add(dentry, inode);
    return 0;
}

static int naive_fs_mkdir(
    struct inode *dir,
    struct dentry *dentry,
    umode_t mode
) {
    return naive_fs_do_create(dir, dentry, S_IFDIR | mode);
}

static int naive_fs_create(
    struct inode *dir,
    struct dentry *dentry,
    umode_t mode,
    bool excl
) {
    return naive_fs_do_create(dir, dentry, mode);
}

static struct inode* naive_fs_iget(
    struct super_block *sb,
    int idx,
    struct inode* parent_inode
) {
    struct inode *inode = NULL;
    struct file_blk *blk;
    inode = new_inode(sb);
    inode->i_ino = idx;
    inode->i_sb = sb;
    inode->i_op = &naive_fs_inode_ops;

    blk = &block[idx];
    if(S_ISDIR(blk->mode)) {
        inode->i_fop = &naive_fs_dir_operations;
    } else {
        inode->i_fop = &naive_fs_file_operations;
    }
    struct timespec64 ts;
    ktime_get_ts64(&ts);
    inode->i_atime = ts;
    inode->i_mtime = ts;
    inode->i_private = blk;
    return inode;
}


// lookup 只负责把对应的块从外设（此处用 memory 模拟）给拉回 inode cache 中
struct dentry *naive_fs_lookup(
    struct inode *parent_inode,
    struct dentry *child_dentry,
    unsigned int flags
) {
    struct super_block *sb = parent_inode->i_sb;
    struct file_blk *blk;
    struct dir_entry *entry;
    int i;

    blk = (struct file_blk *) parent_inode->i_private;
    entry = (struct dir_entry *)&blk->data;
    for(i = 0; i < blk->dir_children; i++) {
        if(strcmp(entry[i].filename, child_dentry->d_name.name) == 0) {
            struct inode *inode = naive_fs_iget(sb, entry[i].idx, parent_inode);
            struct file_blk *inner = (struct file_blk *)inode->i_private;
            inode_init_owner(inode, parent_inode, inner->mode);
            d_add(child_dentry, inode);
            return NULL;
        }
    }
    return NULL;
}

int naive_fs_unlink(
    struct inode *dir,
    struct dentry *dentry
) {
    int i;
    struct inode *inode = dentry->d_inode;
    struct file_blk *blk = (struct file_blk *)inode->i_private;
    struct file_blk *pblk = (struct file_blk *)dir->i_private;
    struct dir_entry *entry;

    entry = (struct dir_entry*)&pblk->data;
    for (i = 0; i < pblk->dir_children; i++) {
        if (!strcmp(entry[i].filename, dentry->d_name.name)) {
            int j;
            for (j = i; j < pblk->dir_children - 1; j++) {
                memcpy(&entry[j], &entry[j+1], sizeof(struct dir_entry));
            }
            pblk->dir_children --;
            
            break;
        }
    }
    // curr_count --;
    blk->busy = 0;
    return simple_unlink(dir, dentry);
}

int naive_fs_rmdir(struct inode *dir, struct dentry *dentry) {
    struct inode *inode = dentry->d_inode;
    struct file_blk *blk = (struct file_blk*) inode->i_private;
    struct file_blk *pblk = (struct file_blk*) dir->i_private;
    int i;
    struct dir_entry *entry = (struct dir_entry*)&pblk->data;
    for(i = 0; i < pblk->dir_children; i++) {
        if (!strcmp(entry[i].filename, dentry->d_name.name)) {
            int j;
            for (j = i; j < pblk->dir_children - 1; j++) {
                memcpy(&entry[j], &entry[j+1], sizeof(struct dir_entry));
            }
            pblk->dir_children --;
            break;
        }
    }

    entry = (struct dir_entry*)&blk->data;
    struct dentry* child_dentry;
    if(atomic_read(&inode->i_count) == 1) { // todo: 递归删除所有内容
        list_for_each_entry(child_dentry, &dentry->d_subdirs, d_child) {
            struct inode* son_inode = child_dentry->d_inode;
            if(S_ISDIR(son_inode->i_mode)) {
                naive_fs_rmdir(inode, child_dentry);
            } else {
                naive_fs_rmdir(inode, child_dentry);
            }
        }
    }

    // curr_count --;
    blk->busy = 0;
    return simple_rmdir(dir, dentry); // libfs
}



static struct inode_operations naive_fs_inode_ops = {
    .create  = naive_fs_create  ,
    .lookup  = naive_fs_lookup  ,
    .mkdir   = naive_fs_mkdir   ,
    .rmdir   = naive_fs_rmdir   ,
    .unlink  = naive_fs_unlink  ,
};

int naive_fs_fill_super(struct super_block *sb, void *data, int silent) { // 新建 root_inode
    struct inode *root_inode;
    umode_t mode = S_IFDIR;
    root_inode = new_inode(sb);
    root_inode->i_ino = 1;
    inode_init_owner(root_inode, NULL, mode);
    root_inode->i_sb = sb;
    root_inode->i_op = &naive_fs_inode_ops;
    root_inode->i_fop = &naive_fs_dir_operations;
    struct timespec64 ts;
    ktime_get_ts64(&ts);
    root_inode->i_atime = ts;
    root_inode->i_mtime = ts;

    block[1].mode = mode;
    block[1].dir_children = 0;
    block[1].idx = 1;
    block[1].busy = 1;
    root_inode->i_private = &block[1];

    sb->s_root = d_make_root(root_inode);
    curr_count ++;

    return 0;
}

static struct dentry *naive_fs_mount(
    struct file_system_type *fs_type, 
    int flags, 
    const char* dev_name, 
    void *data
) {
    return mount_nodev(fs_type, flags, data, naive_fs_fill_super);
}

static void naive_fs_kill_superblock(struct super_block *sb) {
    kill_anon_super(sb);
}

struct file_system_type naive_fs_type = {
    .owner = THIS_MODULE,
    .name = "naivefs",
    .mount = naive_fs_mount,
    .kill_sb = naive_fs_kill_superblock,
};

static int naive_fs_init(void) {
    int ret;

    memset(block, 0, sizeof(block));
    ret = register_filesystem(&naive_fs_type);

    if (ret) {
        printk(KERN_EMERG "Mount FS Error");
    }
    return ret;
}

static void naive_fs_exit(void) {
    unregister_filesystem(&naive_fs_type);
}

module_init(naive_fs_init);
module_exit(naive_fs_exit);

MODULE_LICENSE("GPL v2");