#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "simple.h"

const uint64_t WELCOMEFILE_DATABLOCK_NUMBER = 3;
const uint64_t WELCOMEFILE_INODE_NUMBER = 2;

static int show_superblock(int fd)
{
    struct simplefs_super_block *sb;
    int ret, index;
    char *buffer = calloc(1, SIMPLEFS_DEFAULT_BLOCK_SIZE);

    ret = read(fd, buffer, SIMPLEFS_DEFAULT_BLOCK_SIZE);
	if (ret != SIMPLEFS_DEFAULT_BLOCK_SIZE) {
		printf
		    ("bytes read [%d] are not equal to the default block size\n",
		     (int)ret);
		exit(-1);
	}
    sb = (struct simplefs_super_block *)buffer;
    assert(sb->version == 1);
    assert(sb->magic == SIMPLEFS_MAGIC);
    assert(sb->block_size == SIMPLEFS_DEFAULT_BLOCK_SIZE);
    printf("super block info:\n\t");
    printf("inode count:%lu\n\t", sb->inodes_count);
    printf("used blocks:\n\t\t");
    printf("super block:0\n\t\t");
    printf("inode table block:1\n\t\t");
    printf("root inode data block:2\n\t\t");
    printf("normal inode data block:");
    for (index = 3; index < sizeof(uint64_t) * 8; index++) {
        if (!(sb->free_blocks & (1 << index)))
            printf("%d ", index);
    }
    printf("\n");

    free(buffer);

    return 0;
}

static int show_inodes(int fd)
{
	ssize_t ret, index, index2;

    struct simplefs_inode *inode;
    struct simplefs_dir_record *record;
    char *buffer = calloc(1, SIMPLEFS_DEFAULT_BLOCK_SIZE);
    char *tmp = calloc(1, SIMPLEFS_DEFAULT_BLOCK_SIZE);

    ret = read(fd, buffer, SIMPLEFS_DEFAULT_BLOCK_SIZE);
	if (ret != SIMPLEFS_DEFAULT_BLOCK_SIZE) {
		printf
		    ("can't read inode table info\n");
		exit(-1);
	}

    inode = (struct simplefs_inode *)buffer;
    for (index = 0; index < SIMPLEFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED; index++) {
        if (S_ISDIR(inode[index].mode)) {
            printf("inode %d\n\t", inode[index].inode_no);
            printf("data block:%d child:", inode[index].data_block_number);
            read(fd, tmp, SIMPLEFS_DEFAULT_BLOCK_SIZE);
            record = (struct simplefs_dir_record *)tmp;
            for (index2 = 0; index2 < inode[index].dir_children_count; index2++) {
                printf("%lu:%s ", record[index2].inode_no, record[index2].filename);
            }
            printf("\n"); 
        } else if (S_ISREG(inode[index].mode)) {
            printf("inode %d\n\t", inode[index].inode_no);
            printf("data block:%d size:%llu comment:", inode[index].data_block_number, inode[index].file_size);
            read(fd, tmp, SIMPLEFS_DEFAULT_BLOCK_SIZE);
            printf("%s\n", tmp);
        }
    }
    return 0;
}
int main(int argc, char *argv[])
{
	int fd;
	ssize_t ret;

	if (argc != 2) {
		printf("Usage: parse-simplefs <device>\n");
		return -1;
	}

	fd = open(argv[1], O_RDONLY);
	if (fd == -1) {
		perror("Error opening the device");
		return -1;
	}

    ret = 0;

    show_superblock(fd);
    show_inodes(fd);

	close(fd);
	return ret;
}
