#include "myfs.h"

/******************************************************************************
* SECTION: 宏定义
*******************************************************************************/
#define OPTION(t, p)        { t, offsetof(struct custom_options, p), 1 }
#define MYFS_DBG(fmt, ...) do { printf("MYFS_DBG: " fmt, ##__VA_ARGS__); } while(0)

/******************************************************************************
* SECTION: 全局变量
*******************************************************************************/
static const struct fuse_opt option_spec[] = {		/* 用于FUSE文件系统解析参数 */
	OPTION("--device=%s", device),
	FUSE_OPT_END
};

struct custom_options myfs_options;			 /* 全局选项 */
struct myfs_super myfs_super; 
/******************************************************************************
* SECTION: FUSE操作定义
*******************************************************************************/
static struct fuse_operations operations = {
	.init = myfs_init,						 /* mount文件系统 */		
	.destroy = myfs_destroy,				 /* umount文件系统 */
	.mkdir = myfs_mkdir,					 /* 建目录，mkdir */
	.getattr = myfs_getattr,				 /* 获取文件属性，类似stat，必须完成 */
	.readdir = myfs_readdir,				 /* 填充dentrys */
	.mknod = myfs_mknod,					 /* 创建文件，touch相关 */
	.write = NULL,								  	 /* 写入文件 */
	.read = NULL,								  	 /* 读文件 */
	.utimens = myfs_utimens,				 /* 修改时间，忽略，避免touch报错 */
	.truncate = NULL,						  		 /* 改变文件大小 */
	.unlink = NULL,							  		 /* 删除文件 */
	.rmdir	= NULL,							  		 /* 删除目录， rm -r */
	.rename = NULL,							  		 /* 重命名，mv */

	.open = NULL,							
	.opendir = NULL,
	.access = NULL
};

//挂载的调试信息
void myfs_dump_map() {
    int byte_cursor = 0;//按byte遍历
    int bit_cursor = 0;//按位单独遍历
    //遍历索引位图的信息
    printf("索引位图:\n");
    for (byte_cursor = 0; byte_cursor < MYFS_BLKS_SZ(myfs_super.map_inode_blks); 
         byte_cursor+=4)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            printf("%d ", (myfs_super.map_inode[byte_cursor] & (0x1 << bit_cursor)) >> bit_cursor);   
        }
        printf("\t");

        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            printf("%d ", (myfs_super.map_inode[byte_cursor + 1] & (0x1 << bit_cursor)) >> bit_cursor);   
        }
        printf("\t");
        
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            printf("%d ", (myfs_super.map_inode[byte_cursor + 2] & (0x1 << bit_cursor)) >> bit_cursor);   
        }
        printf("\t");
        
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            printf("%d ", (myfs_super.map_inode[byte_cursor + 3] & (0x1 << bit_cursor)) >> bit_cursor);   
        }
        printf("\n");
    }

    // //遍历数据位图的信息
    // printf("数据位图:\n");
    // for (byte_cursor = 0; byte_cursor < MYFS_BLKS_SZ(myfs_super.map_data_blks); 
    //      byte_cursor+=4)
    // {
    //     for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
    //         printf("%d ", (myfs_super.map_data[byte_cursor] & (0x1 << bit_cursor)) >> bit_cursor);   
    //     }
    //     printf("\t");

    //     for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
    //         printf("%d ", (myfs_super.map_data[byte_cursor + 1] & (0x1 << bit_cursor)) >> bit_cursor);   
    //     }
    //     printf("\t");
        
    //     for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
    //         printf("%d ", (myfs_super.map_data[byte_cursor + 2] & (0x1 << bit_cursor)) >> bit_cursor);   
    //     }
    //     printf("\t");
        
    //     for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
    //         printf("%d ", (myfs_super.map_data[byte_cursor + 3] & (0x1 << bit_cursor)) >> bit_cursor);   
    //     }
    //     printf("\n");
    // }
}


//驱动读*
int myfs_driver_read(int offset, uint8_t *out_content, int size) {
    int      offset_aligned = MYFS_ROUND_DOWN(offset, MYFS_IO_SZ());   //找到读的地址，与数据块对齐
    int      bias           = offset - offset_aligned;
    int      size_aligned   = MYFS_ROUND_UP((size + bias), MYFS_IO_SZ());  // 要读出的数据块的长度
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);   // 暂存读出的数据块
    uint8_t* cur            = temp_content; 
    ddriver_seek(MYFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)   
    {
        ddriver_read(MYFS_DRIVER(), cur, MYFS_IO_SZ());
        cur          += MYFS_IO_SZ();
        size_aligned -= MYFS_IO_SZ();
    }
    memcpy(out_content, temp_content + bias, size);
    free(temp_content);
    return MYFS_ERROR_NONE;    
}

// 驱动写*
int myfs_driver_write(int offset, uint8_t *in_content, int size) {
    int      offset_aligned = MYFS_ROUND_DOWN(offset,MYFS_IO_SZ());   // 找到写的地址，与数据块对齐
    int      bias           = offset - offset_aligned;
    int      size_aligned   = MYFS_ROUND_UP((size + bias), MYFS_IO_SZ());  // 要写入的数据块的长度
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);   // 暂存写入的数据块
    uint8_t* cur            = temp_content; 
    myfs_driver_read(offset_aligned, temp_content, size_aligned);   // 先将读出要写入的区域的数据
    memcpy(temp_content + bias, in_content, size);  // 替换为待写入的数据
    ddriver_seek(MYFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0){
        ddriver_write(MYFS_DRIVER(), cur, MYFS_IO_SZ());
        cur          += MYFS_IO_SZ();
        size_aligned -= MYFS_IO_SZ();
    }
    free(temp_content);
    return MYFS_ERROR_NONE;    
}

//获取文件名*
char* myfs_get_fname(const char* path) {
    char ch = '/';
    char *q = strrchr(path, ch) + 1;    // 找到路径中最后一个'/'字符的后一个位置，即为文件名
    return q;
}

//根据输入的dir获取对应的目录项*
struct myfs_dentry* myfs_get_dentry(struct myfs_inode * inode, int dir) {
    struct myfs_dentry* dentry_cursor = inode->dentrys;
    int    cnt = 0;
    while (dentry_cursor)//如果存在目录项
    {
        if (dir == cnt) {//dir为所找目录项的序号
            return dentry_cursor;
        }
        cnt++;
        dentry_cursor = dentry_cursor->brother;//遍历目录下所有目录项，直到找到
    }
    return NULL;
}

//*
struct myfs_inode* myfs_alloc_inode(struct myfs_dentry * dentry) { //新建索引节点
    struct myfs_inode* inode;
    int byte_cursor = 0; 		  //按字节遍历索引位图
    int bit_cursor  = 0; 		  //按位遍历字节内每一位
    int ino_cursor  = 0;		  //空闲索引节点编号
    int is_find_free_entry = 0;	  //是否找到空闲索引节点

    for (byte_cursor = 0; byte_cursor < MYFS_BLKS_SZ(myfs_super.map_inode_blks); 
         byte_cursor++)//按字节遍历索引位图
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((myfs_super.map_inode[byte_cursor] & (0x1 << bit_cursor)) == 0) { //这一位为0  
                                                      /* 当前ino_cursor位置空闲 */
                myfs_super.map_inode[byte_cursor] |= (0x1 << bit_cursor); //将这一位位置为1
              
                //更新数据位图//
                if(byte_cursor==0)
                {
                  myfs_super.map_data[6*byte_cursor] |= (0x1);
                  myfs_super.map_data[6*byte_cursor] |= (0x1 << 1);
                  myfs_super.map_data[6*byte_cursor] |= (0x1 << 2);
                  myfs_super.map_data[6*byte_cursor] |= (0x1 << 3);
                  myfs_super.map_data[6*byte_cursor] |= (0x1 << 4);
                  myfs_super.map_data[6*byte_cursor] |= (0x1 << 5);
                }
                else if(byte_cursor==1)
                {
                  myfs_super.map_data[6*byte_cursor] |= (0x1 << 6);
                  myfs_super.map_data[6*byte_cursor] |= (0x1 << 7);
                  myfs_super.map_data[6*byte_cursor+1] |= (0x1);
                  myfs_super.map_data[6*byte_cursor+1] |= (0x1 << 1);
                  myfs_super.map_data[6*byte_cursor+1] |= (0x1 << 2);
                  myfs_super.map_data[6*byte_cursor+1] |= (0x1 << 3);
                }
                else if(byte_cursor==2)
                {
                  myfs_super.map_data[6*byte_cursor+1] |= (0x1 << 4);
                  myfs_super.map_data[6*byte_cursor+1] |= (0x1 << 5);
                  myfs_super.map_data[6*byte_cursor+1] |= (0x1 << 6);
                  myfs_super.map_data[6*byte_cursor+1] |= (0x1 << 7);
                  myfs_super.map_data[6*byte_cursor+2] |= (0x1);
                  myfs_super.map_data[6*byte_cursor+2] |= (0x1 << 1);
                }
                else if(byte_cursor==3)
                {
                  myfs_super.map_data[6*byte_cursor+2] |= (0x1 << 2);
                  myfs_super.map_data[6*byte_cursor+2] |= (0x1 << 3);
                  myfs_super.map_data[6*byte_cursor+2] |= (0x1 << 4);
                  myfs_super.map_data[6*byte_cursor+2] |= (0x1 << 5);
                  myfs_super.map_data[6*byte_cursor+2] |= (0x1 << 6);
                  myfs_super.map_data[6*byte_cursor+2] |= (0x1 << 7);
                }
                else if(byte_cursor==4)
                {
                  myfs_super.map_data[6*byte_cursor+3] |= (0x1);
                  myfs_super.map_data[6*byte_cursor+3] |= (0x1 << 1);
                  myfs_super.map_data[6*byte_cursor+3] |= (0x1 << 2);
                  myfs_super.map_data[6*byte_cursor+3] |= (0x1 << 3);
                  myfs_super.map_data[6*byte_cursor+3] |= (0x1 << 4);
                  myfs_super.map_data[6*byte_cursor+3] |= (0x1 << 5);
                }
                else if(byte_cursor==5)
                {
                  myfs_super.map_data[6*byte_cursor+3] |= (0x1 << 6);
                  myfs_super.map_data[6*byte_cursor+3] |= (0x1 << 7);
                  myfs_super.map_data[6*byte_cursor+4] |= (0x1);
                  myfs_super.map_data[6*byte_cursor+4] |= (0x1 << 1);
                  myfs_super.map_data[6*byte_cursor+4] |= (0x1 << 2);
                  myfs_super.map_data[6*byte_cursor+4] |= (0x1 << 3);
                }
                else if(byte_cursor==6)
                {
                  myfs_super.map_data[6*byte_cursor+4] |= (0x1 << 4);
                  myfs_super.map_data[6*byte_cursor+4] |= (0x1 << 5);
                  myfs_super.map_data[6*byte_cursor+4] |= (0x1 << 6);
                  myfs_super.map_data[6*byte_cursor+4] |= (0x1 << 7);
                  myfs_super.map_data[6*byte_cursor+5] |= (0x1);
                  myfs_super.map_data[6*byte_cursor+5] |= (0x1 << 1);
                }
                else if(byte_cursor==7)
                {
                  myfs_super.map_data[6*byte_cursor+5] |= (0x1 << 2);
                  myfs_super.map_data[6*byte_cursor+5] |= (0x1 << 3);
                  myfs_super.map_data[6*byte_cursor+5] |= (0x1 << 4);
                  myfs_super.map_data[6*byte_cursor+5] |= (0x1 << 5);
                  myfs_super.map_data[6*byte_cursor+5] |= (0x1 << 6);
                  myfs_super.map_data[6*byte_cursor+5] |= (0x1 << 7);
                }
                is_find_free_entry = 1;           
                break;
            }
            ino_cursor++;
        }
        if (is_find_free_entry) {
            break;
        }
    }
    if (!is_find_free_entry || ino_cursor == myfs_super.max_ino)//没找到或超过最大文件数
        return -MYFS_ERROR_NOSPACE;
    
	// 创建索引节点
    inode = (struct myfs_inode*)malloc(sizeof(struct myfs_inode));
    inode->ino  = ino_cursor; 
    inode->size = 0;
    //该dentry指向此索引节点
    dentry->inode = inode;
    dentry->ino   = inode->ino;
    //指向该索引节点的目录项为该dentry
    inode->dentry = dentry;
    inode->dir_cnt = 0;
    inode->dentrys = NULL;
    if (MYFS_IS_REG(inode)) {
        inode->data = (uint8_t *)malloc(MYFS_BLKS_SZ(MYFS_DATA_PER_FILE));//如果是普通文件则分配最大文件大小的内存
    }
    return inode;
}

// 将索引节点和对应数据写回磁盘*
int myfs_sync_inode(struct myfs_inode * inode) {
    struct myfs_inode_d  inode_d;
    struct myfs_dentry*  dentry_cursor;
    struct myfs_dentry_d dentry_d;
    int ino             = inode->ino;

    inode_d.ino         = ino;
    inode_d.size        = inode->size;
    inode_d.ftype       = inode->dentry->ftype;
    inode_d.dir_cnt     = inode->dir_cnt;
    int offset;
    
    if (myfs_driver_write(MYFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                     sizeof(struct myfs_inode_d)) != 0) {
        MYFS_DBG("[%s] io error\n", __func__);
        return -MYFS_ERROR_IO;
    }//写索引节点

    if (MYFS_IS_DIR(inode)) {                   // 为目录文件           
        dentry_cursor = inode->dentrys;         // 获得第一个目录项
        offset        = MYFS_DATA_OFS(ino);
        while (dentry_cursor != NULL)
        {
            memcpy(dentry_d.fname, dentry_cursor->fname,MYFS_MAX_FILE_NAME);
            dentry_d.ftype = dentry_cursor->ftype;
            dentry_d.ino = dentry_cursor->ino;
            if (myfs_driver_write(offset, (uint8_t *)&dentry_d, 
                                 sizeof(struct myfs_dentry_d)) != 0) {  // 将目录项dentry_d写进节点的data区域
                MYFS_DBG("[%s] io error\n", __func__);
                return -MYFS_ERROR_IO;                     
            }   
            if (dentry_cursor->inode != NULL) {    //其指向索引节点
                myfs_sync_inode(dentry_cursor->inode);//递归写入
            }
            dentry_cursor = dentry_cursor->brother;
            offset += sizeof(struct myfs_dentry_d);
        }
    }
    else if (MYFS_IS_REG(inode)) {              // 为普通文件
        if (myfs_driver_write(MYFS_DATA_OFS(ino), inode->data, 
                             MYFS_BLKS_SZ(MYFS_DATA_PER_FILE * 2)) != 0) { //将数据写入
            MYFS_DBG("[%s] io error\n", __func__);
            return -MYFS_ERROR_IO;
        }
    }
    return MYFS_ERROR_NONE;
}

// 在目录型文件下添加输入的目录项*
int myfs_alloc_dentry(struct myfs_inode* inode, struct myfs_dentry* dentry) {
    if (inode->dentrys == NULL) {
        inode->dentrys = dentry;
    }//同级目录dentrys首地址
    else {
        dentry->brother = inode->dentrys;
        inode->dentrys = dentry;//头插法插入dentrys
    }
    inode->dir_cnt++;//目录项数++
    return inode->dir_cnt;
}

// 读取目录项dentry指向的索引节点*
struct myfs_inode* myfs_read_inode(struct myfs_dentry * dentry, int ino) {
    struct myfs_inode* inode = (struct myfs_inode*)malloc(sizeof(struct myfs_inode));
    struct myfs_inode_d inode_d;
    struct myfs_dentry* sub_dentry;
    struct myfs_dentry_d dentry_d;
    int    dir_cnt = 0, i;
    
    if (myfs_driver_read(MYFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                        sizeof(struct myfs_inode_d)) != 0) {//读取索引节点
        MYFS_DBG("[%s] io error\n", __func__);
        return NULL;                    
    }
    inode->dir_cnt = 0;
    inode->ino = inode_d.ino;
    inode->size = inode_d.size;
    inode->dentry = dentry;
    inode->dentrys = NULL; //构建in-memory结构
    if (MYFS_IS_DIR(inode)) { //目录文件
        dir_cnt = inode_d.dir_cnt;
        for (i = 0; i < dir_cnt; i++)
        {
            if (myfs_driver_read(MYFS_DATA_OFS(ino) + i * sizeof(struct myfs_dentry_d), 
                                (uint8_t *)&dentry_d, 
                                sizeof(struct myfs_dentry_d)) != 0) {
                MYFS_DBG("[%s] io error\n", __func__);
                return NULL;                    
            }//读出每一个目录项
            sub_dentry = new_dentry(dentry_d.fname, dentry_d.ftype);//设置该目录项数据
            sub_dentry->parent = inode->dentry;
            sub_dentry->ino    = dentry_d.ino; 
            myfs_alloc_dentry(inode, sub_dentry);//在目录型inode下加入目录型
        }
    }
    else if (MYFS_IS_REG(inode)) {//普通文件
        inode->data = (uint8_t *)malloc(MYFS_BLKS_SZ(MYFS_DATA_PER_FILE) * 2);
        if (myfs_driver_read(MYFS_DATA_OFS(ino), (uint8_t *)inode->data, 
                            MYFS_BLKS_SZ(MYFS_DATA_PER_FILE) * 2) != 0) {//读出文件的数据
            MYFS_DBG("[%s] io error\n", __func__);
            return NULL;                    
        }
    }
    return inode;
}

//*
int myfs_calc_lvl(const char * path) {  //统计为第几级目录
    // char* path_cpy = (char *)malloc(strlen(path));
    // strcpy(path_cpy, path);
    char* str = path;
    int   lvl = 0;
    if (strcmp(path, "/") == 0) {   //根目录，返回lvl=0
        return lvl;
    }
    while (*str != NULL) {
        if (*str == '/') {
            lvl++;//遍历每一个字符，统计为第几级目录
        }
        str++;
    }
    return lvl;
}

//*
struct myfs_dentry* myfs_lookup(const char * path, boolean* is_find, boolean* is_root) {
    struct myfs_dentry* dentry_cursor = myfs_super.root_dentry;
    struct myfs_dentry* dentry_ret = NULL;
    struct myfs_inode*  inode; 
    int   total_lvl = myfs_calc_lvl(path);        //获取为第几级目录
    int   lvl = 0;
    int is_hit;                                   
    char* fname = NULL;
    char* path_cpy = (char*)malloc(sizeof(path));
    *is_root = 0;
    strcpy(path_cpy, path);                       //path_cpy用于路径的比较

    if (total_lvl == 0) {                           /* 根目录 */
        *is_find = 1;
        *is_root = 1;
        dentry_ret = myfs_super.root_dentry;        //返回根目录的目录项
    }
    fname = strtok(path_cpy, "/");       
    while (fname)
    {   
        lvl++;
        if (dentry_cursor->inode == NULL) {           /* Cache机制 */
        //已修改
            dentry_cursor->inode = myfs_read_inode(dentry_cursor, dentry_cursor->ino);
        }

        inode = dentry_cursor->inode;

        if (MYFS_IS_REG(inode) && lvl < total_lvl) {  //前一部分目录指向普通文件，即查询错误
            MYFS_DBG("[%s] not a dir\n", __func__);
            dentry_ret = inode->dentry;
            break;
        }
        if (MYFS_IS_DIR(inode)) {
            dentry_cursor = inode->dentrys;
            is_hit        = 0;

            while (dentry_cursor)
            {
                if (memcmp(dentry_cursor->fname, fname, strlen(fname)) == 0) {
                    is_hit = 1;
                    break;
                }
                dentry_cursor = dentry_cursor->brother;  //遍历所有dentrys所指向的文件
            }
            
            if (!is_hit) {
                *is_find = 0;
                MYFS_DBG("[%s] not found %s\n", __func__, fname);
                dentry_ret = inode->dentry;
                break;
            }

            if (is_hit && lvl == total_lvl) {
                *is_find = 1;
                dentry_ret = dentry_cursor;
                break;                 //查找到对应路径且确实为该级，证明确实找到
            }
        }
        fname = strtok(NULL, "/"); 
    }

    if (dentry_ret->inode == NULL) {  //如果该目录项还未设置指向ionde的数据
        dentry_ret->inode = myfs_read_inode(dentry_ret, dentry_ret->ino);
    }
    
    return dentry_ret;
}

//开始实现挂载
int myfs_mount(struct custom_options options){
    printf("mount:\n");
    int                 ret = MYFS_ERROR_NONE;
    int                 driver_fd;//
    struct myfs_super_d  myfs_super_d;  
    struct myfs_dentry*  root_dentry;   //根目录
    struct myfs_inode*   root_inode;    //根目录对应的索引节点
    int                 inode_num;      //索引节点数量
    int                 map_inode_blks; // 索引位图块数
    int                 map_data_blks;	// 数据位图块数
    int                 super_blks;     
    int                 is_init = FALSE;
    
 
    myfs_super.is_mounted = FALSE;

	//挂载开始
    driver_fd = ddriver_open(options.device); //打开ddriver设备
    if (driver_fd < 0) {
        return driver_fd;
    }//打开驱动失败
    myfs_super.fd = driver_fd;//ddriver设备路径
   
    ddriver_ioctl(MYFS_DRIVER(), IOC_REQ_DEVICE_SIZE,  &myfs_super.sz_disk);    // 查看磁盘大小
    ddriver_ioctl(MYFS_DRIVER(), IOC_REQ_DEVICE_IO_SZ, &myfs_super.sz_io);      // 单次IO操作的大小
    
	root_dentry = new_dentry("/", MYFS_DIR);//新建根目录
    if (myfs_driver_read(MYFS_SUPER_OFS, (uint8_t *)(&myfs_super_d), 
                        sizeof(struct myfs_super_d)) != 0) {                    // 读取超级块
        return -MYFS_ERROR_IO;  //return -1;
    }   
    
    printf("幻数:%d\n",myfs_super_d.magic_num);

    // 若幻数不为MYFS文件系统的幻数，则需要填写超级块信息
    if (myfs_super_d.magic_num != MYFS_MAGIC_NUM) {
        super_blks = MYFS_ROUND_UP(sizeof(struct myfs_super_d), MYFS_IO_SZ()) / MYFS_IO_SZ();                 // 超级块占用块数
        inode_num  =  MYFS_DISK_SZ() / ((2*MYFS_DATA_PER_FILE + MYFS_INODE_PER_FILE) * MYFS_IO_SZ());            // 索引节点数量
        map_inode_blks = MYFS_ROUND_UP(MYFS_ROUND_UP(inode_num, 8),(8*MYFS_IO_SZ())) / (8*MYFS_IO_SZ());   // 索引节点位图占用块数
        map_data_blks = MYFS_ROUND_UP(MYFS_ROUND_UP((inode_num*MYFS_DATA_PER_FILE), 8),(8*MYFS_IO_SZ())) / (8*MYFS_IO_SZ()); // 数据块位图占用块数
        
        
        //map_inode_blks = 1;
        //map_data_blks = 1;
        // 布局layout
        myfs_super.max_ino = (inode_num - super_blks - map_inode_blks - map_data_blks);
        //myfs_super.max_ino = 500;

        myfs_super_d.max_ino = myfs_super.max_ino;
        myfs_super_d.map_inode_blks  = map_inode_blks;
        myfs_super_d.map_inode_offset = MYFS_SUPER_OFS + MYFS_BLKS_SZ(super_blks);//索引位图的偏移量
		myfs_super_d.map_data_blks = map_data_blks;
        myfs_super_d.map_data_offset = myfs_super_d.map_inode_offset + MYFS_BLKS_SZ(map_inode_blks);//数据位图的偏移量
        myfs_super_d.inode_offset = myfs_super_d.map_data_offset + MYFS_BLKS_SZ(map_data_blks);
		myfs_super_d.data_offset = myfs_super_d.inode_offset + MYFS_BLKS_SZ(myfs_super_d.max_ino);//数据块的偏移量

        myfs_super_d.sz_usage = 0;
        is_init = 1;
    }

    // 建立in-memory结构
    myfs_super.sz_usage = myfs_super_d.sz_usage;
    myfs_super.map_inode = (uint8_t *)malloc(MYFS_BLKS_SZ(myfs_super_d.map_inode_blks));//分配索引位图空间
    myfs_super.map_inode_blks = myfs_super_d.map_inode_blks;
    myfs_super.map_inode_offset = myfs_super_d.map_inode_offset;
	myfs_super.map_data = (uint8_t *)malloc(MYFS_BLKS_SZ(myfs_super_d.map_data_blks));//分配数据位图空间
	myfs_super.map_data_blks = myfs_super_d.map_data_blks;
    myfs_super.map_data_offset = myfs_super_d.map_data_offset;
    myfs_super.inode_offset = myfs_super_d.inode_offset;
    myfs_super.data_offset = myfs_super_d.data_offset;
	if (myfs_driver_read(myfs_super_d.map_inode_offset, (uint8_t *)(myfs_super.map_inode), MYFS_BLKS_SZ(myfs_super_d.map_inode_blks)) != 0)
        return -MYFS_ERROR_IO;//读取索引位图
	if (myfs_driver_read(myfs_super_d.map_data_offset, (uint8_t *)(myfs_super.map_data), MYFS_BLKS_SZ(myfs_super_d.map_data_blks)) != 0)
        return -MYFS_ERROR_IO;//读取数据位图
    
    printf("super_blks:%d\n",super_blks);
    printf("max_ino:%d\n",myfs_super.max_ino);
    printf("map_inode_blks:%d\n",map_inode_blks);
    printf("map_data_blks:%d\n",map_data_blks);
    printf("is_init = %d\n",is_init);
    // 分配根节点
    if (is_init) {
        root_inode = myfs_alloc_inode(root_dentry);
        myfs_sync_inode(root_inode);
    }
    root_inode            = myfs_read_inode(root_dentry, MYFS_ROOT_INO);
    root_dentry->inode    = root_inode;
    myfs_super.root_dentry = root_dentry;
    myfs_super.is_mounted  = TRUE;
    //myfs_dump_map();//调试信息
    return ret;
}

//开始卸载*
int myfs_umount() {
    struct myfs_super_d  myfs_super_d; 

    if (!myfs_super.is_mounted) {//如果没有被挂载,超级块为全局变量
        return MYFS_ERROR_NONE;
    }

    myfs_sync_inode(myfs_super.root_dentry->inode);     /* 从根节点向下刷写节点写回磁盘块 */
                                                    
    myfs_super_d.magic_num           = MYFS_MAGIC_NUM;
    myfs_super_d.max_ino             = myfs_super.max_ino;
    myfs_super_d.map_inode_blks      = myfs_super.map_inode_blks;
    myfs_super_d.map_inode_offset    = myfs_super.map_inode_offset;
    myfs_super_d.map_data_blks       = myfs_super.map_data_blks;
    myfs_super_d.map_data_offset     = myfs_super.map_data_offset;
    myfs_super_d.inode_offset        = myfs_super.inode_offset;
    myfs_super_d.data_offset         = myfs_super.data_offset;
    myfs_super_d.sz_usage            = myfs_super.sz_usage;


    
    if (myfs_driver_write(MYFS_SUPER_OFS, (uint8_t *)&myfs_super_d, 
                     sizeof(struct myfs_super_d)) != 0) {
        return -MYFS_ERROR_IO;
    }//将超级块写入磁盘

    

    if (myfs_driver_write(myfs_super_d.map_inode_offset, (uint8_t *)(myfs_super.map_inode), 
                         MYFS_BLKS_SZ(myfs_super_d.map_inode_blks)) != 0) {
        return -MYFS_ERROR_IO;
    }//将索引位图写入磁盘

    if (myfs_driver_write(myfs_super_d.map_data_offset, (uint8_t *)(myfs_super.map_data), 
                         MYFS_BLKS_SZ(myfs_super_d.map_data_blks)) != 0) {
        return -MYFS_ERROR_IO;
    }//将数据位图写入磁盘

    free(myfs_super.map_inode);//释放索引位图在内存中存储的空间
    free(myfs_super.map_data);//释放数据位图在内存中存储的空间
    ddriver_close(MYFS_DRIVER());
    return MYFS_ERROR_NONE;
}






/******************************************************************************
* SECTION: 必做函数实现
*******************************************************************************/
/**
 * @brief 挂载（mount）文件系统
 * 
 * @param conn_info 可忽略，一些建立连接相关的信息 
 * @return void*
 */
void* myfs_init(struct fuse_conn_info * conn_info) {
	if (myfs_mount(myfs_options) != 0) {
        MYFS_DBG("[%s] mount error\n", __func__);
		fuse_exit(fuse_get_context()->fuse);
		return NULL;
	} 
	return NULL;
}

/**
 * @brief 卸载（umount）文件系统
 * 
 * @param p 可忽略
 * @return void
 */
void myfs_destroy(void* p) {
	if (myfs_umount() != 0) {
		MYFS_DBG("[%s] unmount error\n", __func__);
		fuse_exit(fuse_get_context()->fuse);
		return;
	}
	return;
}

/**
 * @brief 创建目录
 * 
 * @param path 相对于挂载点的路径
 * @param mode 创建模式（只读？只写？），可忽略
 * @return int 0成功，否则失败
 */
int myfs_mkdir(const char* path, mode_t mode) {
    (void)mode;
	int is_find, is_root;
	char* fname;
	struct myfs_dentry* last_dentry = myfs_lookup(path, &is_find, &is_root);//根据路径查看是否已有指向该名字的同级目录项
	struct myfs_dentry* dentry;                                            //并返回最后一级的目录项
	struct myfs_inode*  inode;
	if (is_find) {
		return -MYFS_ERROR_EXISTS;//已有同级目录名
	}

	if (MYFS_IS_REG(last_dentry->inode)) {//最后一级目录项指向文件，无法创建
		return -MYFS_ERROR_UNSUPPORTED;
	}
	fname  = myfs_get_fname(path);
	dentry = new_dentry(fname, MYFS_DIR);//新建指向该文件的目录项
	dentry->parent = last_dentry;
	inode  = myfs_alloc_inode(dentry);//为该文件新建索引节点
	myfs_alloc_dentry(last_dentry->inode, dentry);
	return 0;
}

/**
 * @brief 获取文件或目录的属性，该函数非常重要
 * 
 * @param path 相对于挂载点的路径
 * @param myfs_stat 返回状态
 * @return int 0成功，否则失败
 */
int myfs_getattr(const char* path, struct stat * myfs_stat) {
	/* TODO: 解析路径，获取Inode，填充myfs_stat，可参考/fs/simplefs/sfs.c的sfs_getattr()函数实现 */
	boolean	is_find, is_root;
	struct myfs_dentry* dentry = myfs_lookup(path, &is_find, &is_root);
	if (is_find == 0) {
		return -MYFS_ERROR_NOTFOUND;
	}//找不到该文件

    if (MYFS_IS_DIR(dentry->inode)) {//该目录项指向目录文件
		myfs_stat->st_mode = S_IFDIR | MYFS_DEFAULT_PERM;
		myfs_stat->st_size = dentry->inode->dir_cnt * sizeof(struct myfs_dentry_d);
	}
	else if (MYFS_IS_REG(dentry->inode)) {//该目录项指向普通文件
		myfs_stat->st_mode = S_IFREG | MYFS_DEFAULT_PERM;
		myfs_stat->st_size = dentry->inode->size;
	}

    myfs_stat->st_nlink = 1;
	myfs_stat->st_uid 	 = getuid();
	myfs_stat->st_gid 	 = getgid();
	myfs_stat->st_atime   = time(NULL);
	myfs_stat->st_mtime   = time(NULL);
    myfs_stat->st_blksize =  MYFS_IO_SZ();
	if (is_root) {//根节点
		myfs_stat->st_size	= myfs_super.sz_usage;//用户已使用的大小
        myfs_stat->st_blocks = MYFS_DISK_SZ() / MYFS_IO_SZ();
		myfs_stat->st_nlink  = 2;		/* !特殊，根目录link数为2 */
	}
    return 0;
}

/**
 * @brief 遍历目录项，填充至buf，并交给FUSE输出
 * 
 * @param path 相对于挂载点的路径
 * @param buf 输出buffer
 * @param filler 参数讲解:
 * 
 * typedef int (*fuse_fill_dir_t) (void *buf, const char *name,
 *				const struct stat *stbuf, off_t off)
 * buf: name会被复制到buf中
 * name: dentry名字
 * stbuf: 文件状态，可忽略
 * off: 下一次offset从哪里开始，这里可以理解为第几个dentry
 * 
 * @param offset 第几个目录项？
 * @param fi 可忽略
 * @return int 0成功，否则失败
 */
int myfs_readdir(const char * path, void * buf, fuse_fill_dir_t filler, off_t offset,
			    		 struct fuse_file_info * fi) {
    /* TODO: 解析路径，获取目录的Inode，并读取目录项，利用filler填充到buf，可参考/fs/simplefs/sfs.c的sfs_readdir()函数实现 */
    boolean	is_find, is_root;
	int		cur_dir = offset; //偏移量，找到对应的目录项
	struct myfs_dentry* dentry = myfs_lookup(path, &is_find, &is_root); //找到目录项
	struct myfs_dentry* sub_dentry; //暂存找到的目录项
	struct myfs_inode* inode;
	if (is_find) {
		inode = dentry->inode; //目录项指向的索引节点
		sub_dentry = myfs_get_dentry(inode, cur_dir); //根据偏移量找到对应的目录项
		if (sub_dentry) { //找到目录项则进行填充
			filler(buf, sub_dentry->fname, NULL, ++offset);
		}
        return MYFS_ERROR_NONE;
	}
	return -MYFS_ERROR_NOTFOUND;//未找到目录项
}

/**
 * @brief 创建文件
 * 
 * @param path 相对于挂载点的路径
 * @param mode 创建文件的模式，可忽略
 * @param dev 设备类型，可忽略
 * @return int 0成功，否则失败
 */
int myfs_mknod(const char* path, mode_t mode, dev_t dev) {
	/* TODO: 解析路径，并创建相应的文件 */
    boolean	is_find, is_root;
	struct myfs_dentry* last_dentry = myfs_lookup(path, &is_find, &is_root);//找到最后一级目录项
	struct myfs_dentry* dentry;
	struct myfs_inode* inode;
	char* fname;
	
	if (is_find == 1) {//如果已存在同名的同级文件
		return -MYFS_ERROR_EXISTS;
	}

	fname = myfs_get_fname(path);//获取要创建的文件名
	
	if (S_ISREG(mode)) {//根据myfs_stat获取文件属性 普通文件
		dentry = new_dentry(fname, MYFS_REG_FILE);//创建目录项
	}
	else if (S_ISDIR(mode)) {//目录文件
		dentry = new_dentry(fname, MYFS_DIR);
	}
	dentry->parent = last_dentry;//上一级目录
	inode = myfs_alloc_inode(dentry);//为该文件加索引节点
	myfs_alloc_dentry(last_dentry->inode, dentry);//将该目录项插入索引节点
    return MYFS_ERROR_NONE;
}

/**
 * @brief 修改时间，为了不让touch报错 
 * 
 * @param path 相对于挂载点的路径
 * @param tv 实践
 * @return int 0成功，否则失败
 */
int myfs_utimens(const char* path, const struct timespec tv[2]) {
	(void)path;
	return 0;
}
/******************************************************************************
* SECTION: 选做函数实现
*******************************************************************************/
/**
 * @brief 写入文件
 * 
 * @param path 相对于挂载点的路径
 * @param buf 写入的内容
 * @param size 写入的字节数
 * @param offset 相对文件的偏移
 * @param fi 可忽略
 * @return int 写入大小
 */
int myfs_write(const char* path, const char* buf, size_t size, off_t offset,
		        struct fuse_file_info* fi) {
	/* 选做 */
	return size;
}

/**
 * @brief 读取文件
 * 
 * @param path 相对于挂载点的路径
 * @param buf 读取的内容
 * @param size 读取的字节数
 * @param offset 相对文件的偏移
 * @param fi 可忽略
 * @return int 读取大小
 */
int myfs_read(const char* path, char* buf, size_t size, off_t offset,
		       struct fuse_file_info* fi) {
	/* 选做 */
	return size;			   
}

/**
 * @brief 删除文件
 * 
 * @param path 相对于挂载点的路径
 * @return int 0成功，否则失败
 */
int myfs_unlink(const char* path) {
	/* 选做 */
	return 0;
}

/**
 * @brief 删除目录
 * 
 * 一个可能的删除目录操作如下：
 * rm ./tests/mnt/j/ -r
 *  1) Step 1. rm ./tests/mnt/j/j
 *  2) Step 2. rm ./tests/mnt/j
 * 即，先删除最深层的文件，再删除目录文件本身
 * 
 * @param path 相对于挂载点的路径
 * @return int 0成功，否则失败
 */
int myfs_rmdir(const char* path) {
	/* 选做 */
	return 0;
}

/**
 * @brief 重命名文件 
 * 
 * @param from 源文件路径
 * @param to 目标文件路径
 * @return int 0成功，否则失败
 */
int myfs_rename(const char* from, const char* to) {
	/* 选做 */
	return 0;
}

/**
 * @brief 打开文件，可以在这里维护fi的信息，例如，fi->fh可以理解为一个64位指针，可以把自己想保存的数据结构
 * 保存在fh中
 * 
 * @param path 相对于挂载点的路径
 * @param fi 文件信息
 * @return int 0成功，否则失败
 */
int myfs_open(const char* path, struct fuse_file_info* fi) {
	/* 选做 */
	return 0;
}

/**
 * @brief 打开目录文件
 * 
 * @param path 相对于挂载点的路径
 * @param fi 文件信息
 * @return int 0成功，否则失败
 */
int myfs_opendir(const char* path, struct fuse_file_info* fi) {
	/* 选做 */
	return 0;
}

/**
 * @brief 改变文件大小
 * 
 * @param path 相对于挂载点的路径
 * @param offset 改变后文件大小
 * @return int 0成功，否则失败
 */
int myfs_truncate(const char* path, off_t offset) {
	/* 选做 */
	return 0;
}


/**
 * @brief 访问文件，因为读写文件时需要查看权限
 * 
 * @param path 相对于挂载点的路径
 * @param type 访问类别
 * R_OK: Test for read permission. 
 * W_OK: Test for write permission.
 * X_OK: Test for execute permission.
 * F_OK: Test for existence. 
 * 
 * @return int 0成功，否则失败
 */
int myfs_access(const char* path, int type) {
	/* 选做: 解析路径，判断是否存在 */
	return 0;
}	
/******************************************************************************
* SECTION: FUSE入口
*******************************************************************************/
int main(int argc, char **argv)
{
    int ret;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	myfs_options.device = strdup("/home/guests/190110214/ddriver");

	if (fuse_opt_parse(&args, &myfs_options, option_spec, NULL) == -1)
		return -1;
	
	ret = fuse_main(args.argc, args.argv, &operations, NULL);
	fuse_opt_free_args(&args);
	return ret;
}