#include <linux/module.h>
#include <linux/slab.h>
#include <linux/buffer_head.h>
#include "simple.h"
#include <linux/fs.h>

int simplefs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);
int simplefs_mkdir (struct inode *dir,struct dentry *dentry, umode_t mode);
int simplefs_unlink (struct inode *inode, struct dentry *dentry);
struct dentry *simplefs_lookup(struct inode *dir,struct dentry *dentry, unsigned int flags);
int simplefs_write_inode (struct inode *inode,
        struct writeback_control *wbc);
struct inode *simplefs_iget(struct super_block *s, int ino);
struct inode *simplefs_alloc_inode(struct super_block *sb);
void simplefs_destroy_inode(struct inode *inode);
int simplefs_readdir(struct file *filp, void *dirent, filldir_t filldir);
ssize_t simplefs_read(struct file *filp, char __user *buf, size_t len, loff_t *offset);
ssize_t simplefs_write(struct file *filp, const char __user *buf, size_t len, loff_t *offset);

static struct kmem_cache * simplefs_inode_cachep;

static inline struct simplefs_super_block *SIMPLEFS_SB(struct super_block *sb)
{                                                                               
        return sb->s_fs_info;                                                       
}                                                                               
static inline struct simplefs_inode *SIMPLEFS_INODE(struct inode *inode)        
{                                                                               
        return inode->i_private;                                                    
}

struct super_operations simplefs_sops = {
//    .alloc_inode = simplefs_alloc_inode,
    .destroy_inode = simplefs_destroy_inode,
#if 0
    .write_inode = simplefs_write_inode,
#endif
};

struct inode_operations simplefs_inode_operations = {
#if 1
    .create = simplefs_create,
    .lookup = simplefs_lookup,
    .mkdir = simplefs_mkdir,
    .unlink = simplefs_unlink,
#endif
};
struct file_operations simplefs_reg_operations = {
#if 1
    .read  = simplefs_read,
    .write = simplefs_write,
#endif
};
ssize_t simplefs_read(struct file *filp, char __user *buf, size_t len, loff_t *offset)
{
    struct inode *inode = filp->f_inode;
    struct simplefs_inode *si = (struct simplefs_inode *)inode->i_private;
    struct buffer_head *bh = NULL;

    if (*offset > 0)
        return 0;
    bh = sb_bread(inode->i_sb, si->data_block_number);
    if (bh->b_size < *offset)
        return 0;
    if (bh->b_size < *offset + len)
        len = bh->b_size - *offset;
    if (copy_to_user(buf, bh->b_data + *offset, len))
        printk(KERN_ERR "copy to user failed\n");
    *offset += len;

    brelse(bh);
    return len;
}
int simplefs_sync_inode_table(struct inode *inode)
{
    struct super_block *vsb = inode->i_sb; 
    struct simplefs_inode *si = (struct simplefs_inode *)inode->i_private;
    struct buffer_head *bh = NULL;
    loff_t offset;

    bh = sb_bread(vsb, SIMPLEFS_INODESTORE_BLOCK_NUMBER);
    offset = sizeof(struct simplefs_inode) * (si->inode_no - 1);
    memcpy(bh->b_data + offset, si, sizeof(struct simplefs_inode));
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);

    return 0;
}
ssize_t simplefs_write(struct file *filp, const char __user *buf, size_t len, loff_t *offset)
{
    struct inode *inode = filp->f_inode;
    struct simplefs_inode *si = (struct simplefs_inode *)inode->i_private;
    struct buffer_head *bh = NULL;

    bh = sb_bread(inode->i_sb, si->data_block_number);
    printk(KERN_ERR "len:%lu offset:%lld block:%llu bh:%p\n", len, *offset, si->data_block_number, bh);
    if(copy_from_user(bh->b_data + *offset, buf, len))
        printk(KERN_ERR "copy from user failed\n");
    *offset += len;
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);

    si->file_size = *offset;
    inode->i_size = si->file_size;
    simplefs_sync_inode_table(inode);

    return len;
}

const struct file_operations simplefs_dir_operations = {
    .readdir = simplefs_readdir,
};

int simplefs_readdir (struct file *filp, void *dirent, filldir_t filldir)
{
    struct inode *inode = file_inode(filp);
    struct buffer_head *bh;
    struct simplefs_inode *si = (struct simplefs_inode *)inode->i_private;
    struct simplefs_dir_record *dir;
    int index = 0, pos = filp->f_pos;

    if (filp->f_pos)
        return 0;

    //dump_stack();
    bh = sb_bread(inode->i_sb, si->data_block_number);
    dir = (struct simplefs_dir_record *)bh->b_data;

    printk(KERN_ERR "%s child:%llu\n", __func__, si->dir_children_count);
    for (index = 0; index < si->dir_children_count; index++) {
        filldir(dirent, dir[index].filename, SIMPLEFS_FILENAME_MAXLEN, pos, dir[index].inode_no, DT_UNKNOWN);
        filp->f_pos += sizeof(struct simplefs_dir_record);
        pos += sizeof(struct simplefs_dir_record);
        printk(KERN_ERR "%s child:%s\n", __func__, dir[index].filename);
    }
    brelse(bh);
    return 0;
}
int simplefs_mkdir(struct inode *dir,struct dentry *dentry, umode_t mode)
{
    //dump_stack();

    simplefs_create(dir, dentry, mode, false);

    return 0;
}
int simplefs_unlink (struct inode *inode,struct dentry *dentry)
{
    //dump_stack();
    return 0;
}
int simplefs_get_free_block(struct super_block *vsb)
{
    struct simplefs_super_block *ssb = SIMPLEFS_SB(vsb);
    int index = 0;

    for (index = 3; index < SIMPLEFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED; index ++) {
        if (ssb->free_blocks & (1 << index))
            break;
    }

    if (index == SIMPLEFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED) {
        printk(KERN_ERR "no free block\n");
        return -1;
    }
    ssb->free_blocks &= ~(1 << index);
    return index;
}
int simplefs_sync(struct super_block *vsb, void *data, int block_number)
{
    struct buffer_head *bh = sb_bread(vsb, block_number);
    struct simplefs_super_block *sbi = (struct simplefs_super_block *)data;
    printk(KERN_ERR "super block:%d inode counts:%llu block bitmap:%llx\n",
            block_number, sbi->inodes_count, sbi->free_blocks);
    BUG_ON(!bh);

    memcpy(bh->b_data, data, SIMPLEFS_DEFAULT_BLOCK_SIZE);
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);

    return 0;
}

int simplefs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl){
    //dump_stack();
    struct super_block *vsb = dir->i_sb;
    struct simplefs_super_block *sbi = SIMPLEFS_SB(vsb); 
    struct simplefs_inode *si, *si_dir, *head;
    struct inode *inode;
    struct buffer_head *bh;
    struct simplefs_dir_record *record;
    uint64_t free_block;

    if (sbi->inodes_count >= SIMPLEFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED) {
        printk(KERN_ERR "too many inodes\n");
        return -1;
    }

    si = kmem_cache_alloc(simplefs_inode_cachep, GFP_KERNEL);
    inode = new_inode(vsb);
    inode->i_sb = vsb;
    inode->i_ino = sbi->inodes_count + 1; 

    //super block:inode count,data map
    free_block = simplefs_get_free_block(vsb);
    simplefs_sync(vsb, sbi, SIMPLEFS_SUPERBLOCK_BLOCK_NUMBER);
    sbi->inodes_count++;
    printk("super block update: inode count:%llu data:%llx version:%llu magic:%llx\n",
            sbi->inodes_count, sbi->free_blocks, sbi->version, sbi->magic);
    simplefs_sync(vsb, sbi, SIMPLEFS_SUPERBLOCK_BLOCK_NUMBER);

    //inode and simplefs_inode
    si->data_block_number = free_block;
    si->inode_no = inode->i_ino; 
    si->mode = mode;
    inode->i_private = si;
    inode->i_op = &simplefs_inode_operations;
    inode->i_blocks = 1;
    
    inode->i_mode = mode;
    if (S_ISDIR(mode)) {
        printk(KERN_INFO "create new directory\n");
        si->dir_children_count = 0;
        inode->i_fop = &simplefs_dir_operations;
    } else if (S_ISREG(mode)) {
        printk(KERN_INFO "create new directory\n");
        si->file_size = 0;
        inode->i_fop = &simplefs_reg_operations;
    }
    //store inode and parent inode metadata
    bh = sb_bread(vsb, SIMPLEFS_INODESTORE_BLOCK_NUMBER);
    head = (struct simplefs_inode *)bh->b_data;
    memcpy(head + si->inode_no - 1, si, sizeof(struct simplefs_inode));

    si_dir = (struct simplefs_inode *)dir->i_private;
    if (!S_ISDIR(si_dir->mode)) {
        printk(KERN_ERR "create new inode not in directory\n");
    }
    si_dir->dir_children_count++;
    memcpy(head + si_dir->inode_no - 1, si_dir, sizeof(struct simplefs_inode));
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);

    //parent inode data
    bh = sb_bread(vsb, si_dir->data_block_number); 
    record = ((struct simplefs_dir_record *)bh->b_data) + si_dir->dir_children_count - 1;
    strcpy(record->filename, dentry->d_iname);
    record->inode_no = inode->i_ino;
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);

    d_add(dentry, inode);

    return 0;
}
struct simplefs_inode *simplefs_get_inode(struct super_block *sb, int ino)
{
    struct simplefs_inode *si = kmem_cache_alloc(simplefs_inode_cachep, GFP_KERNEL);
    struct buffer_head *bh;
    struct simplefs_super_block *sbi = (struct simplefs_super_block *)sb->s_fs_info;
    struct simplefs_inode *inode_table;
    int index = 0;
    bh = sb_bread(sb, SIMPLEFS_INODESTORE_BLOCK_NUMBER);
    inode_table = (struct simplefs_inode *)bh->b_data;
    for (index = 0; index < sbi->inodes_count; index++) {
        if (ino == inode_table[index].inode_no) {
            break;
        }
    }
    if (index < sbi->inodes_count) {
        memcpy(si, &inode_table[index], sizeof(struct simplefs_inode));
        brelse(bh);
        return si;
    } else {
        brelse(bh);
        kfree(si);
        return NULL;
    }
}
struct dentry *simplefs_lookup(struct inode *dir,struct dentry *dentry, unsigned int flags)
{
    const unsigned char *name = dentry->d_name.name;
    struct simplefs_dir_record *record = NULL;
    struct simplefs_inode *si = (struct simplefs_inode *)dir->i_private;
    struct super_block *sb = dir->i_sb;
    struct inode *inode;
    int index = 0;
    struct buffer_head *bh;

    //dump_stack();
    printk(KERN_ERR "find name %s in directory inode:%lu\n", name, dir->i_ino);
    if (!S_ISDIR(dir->i_mode)) {
        printk(KERN_ERR "lookup not in dir\n");
        return ERR_PTR(-EIO);
    }

    bh = sb_bread(sb, si->data_block_number);
    if (!bh) {
        printk(KERN_ERR "sb read failed\n");
        return NULL;
    }
    record = (struct simplefs_dir_record *)bh->b_data;

    for (index = 0; index < si->dir_children_count; index ++) {
        if (!strcmp(record[index].filename, name))
            break;
    }
    if (index == si->dir_children_count) {
        printk("failed find children\n");
        goto err1;
    }

    inode = simplefs_iget(sb, record[index].inode_no);

    d_instantiate(dentry, inode);
    d_rehash(dentry);

    brelse(bh);
    return dentry;

err1:
    brelse(bh);
    return NULL;
}
int simplefs_write_inode (struct inode *inode,
        struct writeback_control *wbc)
{
    struct buffer_head *bh;
    struct super_block *sb = inode->i_sb;
    struct simplefs_inode *s_inode = (struct simplefs_inode *)inode->i_private;

    bh = sb_bread(sb, s_inode->inode_no);
    mark_buffer_dirty(bh);
    return 0;
}
void simplefs_destroy_inode(struct inode *inode)
{
    kmem_cache_free(simplefs_inode_cachep, (struct simplefs_inode *)inode->i_private);
}
#if 0
struct inode *simplefs_alloc_inode(struct super_block *sb)
{
    //struct inode *inode = iget_locked(sb, ((struct simplefs_super_block *)sb->s_fs_info)->inodes_count);
    struct inode *inode = new_inode(sb);
    struct simplefs_inode *s_inode = kmem_cache_alloc(simplefs_inode_cachep, GFP_KERNEL);
    inode->i_private = s_inode;
    return inode;
}
#endif
struct inode *simplefs_iget(struct super_block *s, int ino)
{
    struct buffer_head *bh;
    struct simplefs_inode *s_inode;
    struct inode *inode = iget_locked(s, ino);

    if (!inode)
        return ERR_PTR(-ENOMEM);
    if (!(inode->i_state & I_NEW))
        return ERR_PTR(-ENOMEM);

    bh = sb_bread(s, SIMPLEFS_INODESTORE_BLOCK_NUMBER);
    s_inode = ((struct simplefs_inode *)bh->b_data) + ino - 1;

    //inode->i_mapping->a_ops = &simplefs_aops;
    inode->i_op = &simplefs_inode_operations;
    if (S_ISDIR(s_inode->mode))
        inode->i_fop = &simplefs_dir_operations;
    else if (S_ISREG(s_inode->mode))
        inode->i_fop = &simplefs_reg_operations;
    else
        inode->i_fop = NULL;
    inode->i_mode = s_inode->mode;
    inode->i_ino = ino;
    inode->i_private = s_inode;
    inode->i_mtime.tv_sec = inode->i_atime.tv_sec = inode->i_ctime.tv_sec = 0;
    inode->i_mtime.tv_nsec = inode->i_atime.tv_nsec = inode->i_ctime.tv_nsec = 0;
    inode->i_blocks = 1;
    inode->i_size = s_inode->file_size;

    brelse(bh);
    unlock_new_inode(inode);
    return inode;
}
static int simplefs_fill_super(struct super_block *s, void *data, int silent)
{
    struct inode *root_inode = NULL;
    struct simplefs_super_block *sf;
    struct buffer_head *bh;

    sf = kzalloc(sizeof(struct simplefs_super_block), GFP_KERNEL);
    if (!sf)
        goto err1;

    if (!sb_set_blocksize(s, SIMPLEFS_DEFAULT_BLOCK_SIZE))
        goto err2;

    bh = sb_bread(s, SIMPLEFS_SUPERBLOCK_BLOCK_NUMBER);
    if (bh == NULL)
        goto err2;
    memcpy(sf, bh->b_data, sizeof(struct simplefs_super_block));
    if (sf->magic != SIMPLEFS_MAGIC) {
        printk(KERN_ERR "magic is wrong:0x%Lx\n", sf->magic);
        goto err3;
    }
    s->s_magic = sf->magic;
    s->s_op = &simplefs_sops;

    s->s_maxbytes      = SIMPLEFS_DEFAULT_BLOCK_SIZE;
    s->s_time_gran     = 1;
    s->s_fs_info = sf;

    root_inode = simplefs_iget(s, SIMPLEFS_ROOTDIR_INODE_NUMBER);
    s->s_root = d_make_root(root_inode);

    brelse(bh);
    printk(KERN_ERR "simplefs magic:0x%lx\n", s->s_magic);

    return 0;
err3:
    brelse(bh);
err2:
    kfree(sf);
err1:
    return -1;
}

static struct dentry *simplefs_mount(struct file_system_type *fs_type,
            int flags, const char *dev_name, void *data)
{
        return mount_bdev(fs_type, flags, dev_name, data, simplefs_fill_super);
}
static struct file_system_type simplefs_type = {
    .owner      = THIS_MODULE,
    .name       = "simplefs",
    .mount      = simplefs_mount,
    .kill_sb    = kill_block_super,
    .fs_flags   = FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("simplefs");

static int __init init_inodecache(void)
{
    simplefs_inode_cachep = kmem_cache_create("simplefs_inode_cache",
            sizeof(struct simplefs_inode), 0, SLAB_RECLAIM_ACCOUNT, NULL);

    if (NULL == simplefs_inode_cachep)
        return -ENOMEM;
    return 0;
}

static void destroy_inodecache(void)
{
    kmem_cache_destroy(simplefs_inode_cachep);
}


static int __init init_simplefs(void)
{
    int err = init_inodecache();
    if (err)
        goto out1;
    err = register_filesystem(&simplefs_type);
    if (err)
        goto out;
    return 0;
out:
    destroy_inodecache();
out1:
    return err;
}

static void __exit exit_simplefs(void)
{
    unregister_filesystem(&simplefs_type);
    destroy_inodecache();
}

module_init(init_simplefs)
module_exit(exit_simplefs)
MODULE_LICENSE("GPL");
