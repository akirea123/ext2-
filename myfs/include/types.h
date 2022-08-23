#ifndef _TYPES_H_
#define _TYPES_H_

typedef int          boolean;
#define MYFS_MAGIC_NUM           0x52415453     // 幻数
#define MYFS_MAX_FILE_NAME       128        // 文件名最长为128 
#define MYFS_IS_DIR(pinode)      (pinode->dentry->ftype == MYFS_DIR)        // 判断文件类型是否是目录
#define MYFS_IS_REG(pinode)      (pinode->dentry->ftype == MYFS_REG_FILE)   // 判断文件类型是否是普通文件 
#define MYFS_ERROR_NONE          0
#define TRUE                     1
#define FALSE                    0
#define MYFS_SUPER_OFS           0          // 超级块偏移量为0
#define MYFS_ROOT_INO            0          // 根目录索引号
#define MYFS_ERROR_IO            EIO
#define MYFS_ERROR_NOSPACE       ENOSPC
#define MYFS_ERROR_NOTFOUND      ENOENT
#define MYFS_ERROR_EXISTS        ENXIO
#define MYFS_ERROR_UNSUPPORTED        EEXIST
#define MYFS_ROUND_DOWN(value, round)    (value % round == 0 ? value : (value / round) * round)
#define MYFS_ROUND_UP(value, round)      (value % round == 0 ? value : (value / round + 1) * round)
#define MYFS_IO_SZ()                    (myfs_super.sz_io)         
#define MYFS_DISK_SZ()                    (myfs_super.sz_disk)        
#define MYFS_DRIVER()                    (myfs_super.fd)     
#define MYFS_BLKS_SZ(blks)               (blks * MYFS_IO_SZ())   //blks对应的具体大小
#define MYFS_INODE_PER_FILE      1          // 一个文件对应1个索引节点
#define MYFS_DATA_PER_FILE       6          // 一个文件最多占用6个数据块
#define UINT32_BITS             32
#define UINT8_BITS              8
//sfs的设计
//#define MYFS_INO_OFS(ino)                (myfs_super.data_offset + ino * MYFS_BLKS_SZ((
//                                         MYFS_INODE_PER_FILE + MYFS_DATA_PER_FILE)))                 // ino索引节点的偏移量
//#define MYFS_DATA_OFS(ino)               (MYFS_INO_OFS(ino) + MYFS_BLKS_SZ(MYFS_INODE_PER_FILE))    // ino索引节点的数据位置的偏移量
//我的设计
//#define MYFS_INO_OFS(ino)                (myfs_super.data_offset + ino * MYFS_BLKS_SZ(MYFS_INODE_PER_FILE))     // ino索引节点的偏移量
//#define MYFS_DATA_OFS(ino)               (myfs_super.data_offset + myfs_super.max_ino * MYFS_BLKS_SZ(MYFS_INODE_PER_FILE)+ ino * 2 * MYFS_BLKS_SZ(MYFS_DATA_PER_FILE))    // ino索引节点的数据位置的偏移量
#define MYFS_INO_OFS(ino)                (myfs_super.inode_offset + ino * MYFS_BLKS_SZ(MYFS_INODE_PER_FILE))
#define MYFS_DATA_OFS(ino)               (myfs_super.data_offset + ino * MYFS_BLKS_SZ(MYFS_DATA_PER_FILE) * 2)


/////////文件系统存储定义的实现形式//////////

typedef enum myfs_file_type {
    MYFS_REG_FILE,
    MYFS_DIR
} MYFS_FILE_TYPE;//定义文件类型，是普通文件还是目录文件

struct custom_options {
	const char*        device;
};

struct myfs_super {
    int                fd;               //驱动路径
    int                sz_io;
    int                sz_disk;
    int                sz_usage;
    int                max_ino;
    uint8_t*           map_inode;        //索引位图
    int                map_inode_blks;  //索引位图占用的块数
    int                map_inode_offset; //索引位图在磁盘上的偏移
    uint8_t*           map_data;        // 数据位图
    int                map_data_blks;   // 数据位图占用的块数
    int                map_data_offset; // 数据位图在磁盘上的偏移
    int                inode_offset;    //索引节点块开始的偏移
    int                data_offset;     //数据块的偏移
    int                is_mounted;      //挂载信号
    struct myfs_dentry* root_dentry;    //根目录
};

struct myfs_super_d
{
    uint32_t           magic_num;        //幻数
    int                sz_usage;
    int                max_ino;
    int                map_inode_blks;
    int                map_inode_offset;
    int                map_data_blks;   // 数据位图占用的块数
    int                map_data_offset; // 数据位图在磁盘上的偏移
    int                inode_offset;
    int                data_offset;     //数据块的偏移
};

struct myfs_inode {  //索引节点
    int                ino;                           /* 在inode位图中的下标 */
    int                size;                          /* 文件已占用空间 */
    int                dir_cnt;                       /* 所有目录项的个数*/
    struct myfs_dentry* dentry;                        /* 指向该inode的dentry */
    struct myfs_dentry* dentrys;                       /* 所有目录项 */
    uint8_t*           data;           
};

struct myfs_inode_d
{
    int                ino;                           /* 在inode位图中的下标 */
    int                size;                          /* 文件已占用空间 */
    int                dir_cnt;
    MYFS_FILE_TYPE      ftype;                       
};  

struct myfs_dentry {
    char               fname[MYFS_MAX_FILE_NAME];
    int                ino;                            /* 对应索引节点在inode位图中的下标*/
    struct myfs_dentry* parent;                        /* 父亲Inode的dentry */
    struct myfs_dentry* brother;                       /* 兄弟 */
    struct myfs_inode*  inode;                         /* 指向inode(子) */
    MYFS_FILE_TYPE      ftype;                         /* 指向的文件类型*/
};

struct myfs_dentry_d
{
    char               fname[MYFS_MAX_FILE_NAME];
    MYFS_FILE_TYPE      ftype;
    int                ino;                           /* 指向的ino号 */
};  


static inline struct myfs_dentry* new_dentry(char * fname, MYFS_FILE_TYPE ftype) {
    struct myfs_dentry * dentry = (struct myfs_dentry *)malloc(sizeof(struct myfs_dentry));
    memset(dentry, 0, sizeof(struct myfs_dentry));
    memcpy(dentry->fname, fname, strlen(fname));
    dentry->ftype   = ftype;
    dentry->ino     = -1; //根目录不指向inode
    dentry->inode   = NULL;
    dentry->parent  = NULL;
    dentry->brother = NULL;                                            
}

#endif /* _TYPES_H_ */