#define FUSE_USE_VERSION 30

#include <fuse.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "isfs_config.h"

/* a pointer which points to a super block data structure */
static struct super_block* super_ptr;

/* a pointer which points to a block-bitmap data structure */
static struct block_bitmap blk_bitmap;
static int blk_bitmap_cur_idx = 0;

/* a pointer which points to a inode-bitmap data structure */
static struct inode_bitmap i_bitmap;
static int i_bitmap_cur_idx = ROOT_DIR_INODE_NUM;

/* a pointer which points to inode data structure: indicating the first inode address */
static struct inode* inode_table_ptr = 0;

static int init_super();
static int init_block_bitmap();
static int init_inode_bitmap();
static int init_inode_table_ptr();
static int create_root_dir();

struct timespec return_current();
struct data_block* allocateDataBlock();

char* MNT_POINT;

static int do_getattr( const char *path, struct stat *st )
{
	printf("--- Getting attributes of files/directories ...\n");
	st->st_uid = getuid(); // The owner of the file/directory is the user who mounted the filesystem
	st->st_gid = getgid(); // The group of the file/directory is the same as the group of the user who mounted the filesystem
	
	printf("path: %s\n", path);
	const char* new_path = path+1;
	printf("new path: %s\n", new_path);
	struct inode* root = inode_table_ptr+ ( super_ptr->root_dir_inode_num );
	if ( strcmp( path, "/" ) == 0 ) {
		st->st_mode = S_IFDIR | 0744;		// Permission bits (octal)
		st->st_nlink = 2;
		st->st_ino = super_ptr->root_dir_inode_num;
		st->st_atime = root->atime.tv_sec;
		st->st_ctime = root->ctime.tv_sec;
		st->st_mtime = root->mtime.tv_sec;

		return 0;
	}
	else {
		// iterate through the directory table of the root dir
		struct directory_entry* tmp_entry;
		struct data_block* tmp_data_blk;
		for(int i=0; i<DIRECT_BLOCK_PTR_NUM; i++) {
			tmp_data_blk = root->direct_blk_ptr[i];
			if(tmp_data_blk==NULL)
				return -ENOENT;
			tmp_entry = (struct directory_entry* ) tmp_data_blk;
			if( strcmp(tmp_entry->name, new_path) == 0 && tmp_entry->inode_num != 0 ) {	// found the file/directory by the path parameter
				printf("found the file %d\n", i);
				struct	inode* tmp_found = inode_table_ptr + (tmp_entry->inode_num);
				st->st_mode = ( tmp_entry->file_type == DIRECTORY) ? S_IFDIR | 0755 : S_IFREG | 0644;
				st->st_size = super_ptr->blk_size;		// unsupport big file ( only <= 512 bytes )
				st->st_nlink = ( tmp_entry->file_type == DIRECTORY) ? 2 : 1;
				st->st_ino = tmp_entry->inode_num;		// need run with this option: -o use_ino
				st->st_atime = tmp_found->atime.tv_sec;
				st->st_ctime = tmp_found->ctime.tv_sec;
				st->st_mtime = tmp_found->mtime.tv_sec;
				break;
			}
		}
		return 0;
	}
	return -ENOENT;
}

static int do_readdir( const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi )
{
	printf("--- Reading directories ...\n");
	const char* new_path = path+1;
	printf("path: %s\n", path);
	printf("new path: %s\n", new_path);
	struct inode* root = inode_table_ptr+ ( super_ptr->root_dir_inode_num );
	if ( strcmp( path, "/" ) == 0 ) {
		int dir_entry_table_idx = 0;
		struct directory_entry* cur_entry = (struct directory_entry*) root->direct_blk_ptr[dir_entry_table_idx];
		while(cur_entry!=NULL) {
			if(cur_entry->inode_num!=0) {
				printf("- entry name: %s\n", cur_entry->name);
				filler( buffer, cur_entry->name, NULL, 0);
			}
			cur_entry = (struct directory_entry*) root->direct_blk_ptr[++dir_entry_table_idx];
		}
		return 0;
	}
	else {
		// read directories other than rootdir
		// iterate through the directory table of the root dir
		struct directory_entry* cur_entry;
		struct data_block* tmp_data_blk;
		for(int i=0; i<DIRECT_BLOCK_PTR_NUM; i++) {
			tmp_data_blk = root->direct_blk_ptr[i];
			if(tmp_data_blk==NULL)
				return -ENOENT;
			cur_entry = (struct directory_entry* ) tmp_data_blk;
			if( strcmp(cur_entry->name, new_path) == 0 && cur_entry->inode_num != 0 ) {	// found the directory by the path parameter
				printf("found the directory: %s at entry: %d\n", cur_entry->name, i);
				struct	inode* tmp_found = inode_table_ptr + (cur_entry->inode_num);

				//iterate through the directory of this directory
				int dir_entry_table_idx = 0;
				struct directory_entry* entry = (struct directory_entry*) tmp_found->direct_blk_ptr[dir_entry_table_idx];
				while(entry!=NULL) {
					if(cur_entry->inode_num!=0) {
						printf("entry name: %s\n", entry->name);
						filler( buffer, entry->name, NULL, 0);
					}
					entry = (struct directory_entry*) tmp_found->direct_blk_ptr[++dir_entry_table_idx];
				}
				break;
			}
		}
		return 0;
	}
	return -1;
}

static int do_read( const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi )
{
	printf("--- Reading a file ...\n");
	const char* filename = path+1;
	char* file_content;
	struct inode* root = inode_table_ptr+ ( super_ptr->root_dir_inode_num );

	// iterate through the directory table of the root dir
	struct data_block* tmp_data_blk;
	struct directory_entry* tmp_entry;
	for(int i=0; i<DIRECT_BLOCK_PTR_NUM; i++) {
		tmp_data_blk = root->direct_blk_ptr[i];
		if(tmp_data_blk==NULL)
			return -ENOENT;
		tmp_entry = (struct directory_entry* ) tmp_data_blk;
		if( strcmp(tmp_entry->name, filename) == 0 && tmp_entry->inode_num != 0 ) {		// found the file by the path parameter
			printf("found the file:\nfilename: %s at entry: %d\n", filename, i);
			struct	inode* tmp_found = inode_table_ptr + (tmp_entry->inode_num);

			// !!! big file need to iterate through data blocks !!!
			file_content = tmp_found->direct_blk_ptr[0]->buf;
			printf("file content: %s\n", tmp_found->direct_blk_ptr[0]->buf);
			memcpy(buffer, file_content + offset, size);	// copy file content to the buffer
			printf("cat buffer: %s\n", buffer);
			
			tmp_found->mtime = return_current();
			break;
		}
	}
	return strlen( file_content ) - offset;
}

static int do_mkdir( const char *path, mode_t mode )
{
	printf("Creating a new directory ...\n");
	const char* new_dirname = path+1;
	printf("new dir name: %s\n", new_dirname);
	printf("i_bitmap_cur_idx is: %d\n", i_bitmap_cur_idx);
	i_bitmap_cur_idx++;
	printf("i_bitmap_cur_idx is: %d\n", i_bitmap_cur_idx);

	struct inode* cur_inode_ptr = inode_table_ptr+i_bitmap_cur_idx;
	struct data_block* tmp_data_blk = allocateDataBlock();
	cur_inode_ptr->direct_blk_ptr[0] = tmp_data_blk;

	struct directory_entry* dir_entry_table_ptr = (struct directory_entry*) tmp_data_blk;
	dir_entry_table_ptr->inode_num = i_bitmap_cur_idx;
	dir_entry_table_ptr->file_type = DIRECTORY;
	strncpy(dir_entry_table_ptr->name, ".", DIR_ENTRY_NAME_LEN);

	struct data_block* tmp_data_blk_2 = allocateDataBlock();
	cur_inode_ptr->direct_blk_ptr[1] = tmp_data_blk_2;
	
	struct directory_entry* dir_entry_table_ptr_2 = (struct directory_entry*) tmp_data_blk_2;
	dir_entry_table_ptr_2->inode_num = i_bitmap_cur_idx;
	dir_entry_table_ptr_2->file_type = DIRECTORY;
	strncpy(dir_entry_table_ptr_2->name, "..", DIR_ENTRY_NAME_LEN);

	cur_inode_ptr->atime = return_current();
	cur_inode_ptr->ctime = return_current();
	cur_inode_ptr->mtime = return_current();

	// update inode bitmap
	i_bitmap.inode_bitmap[i_bitmap_cur_idx] = 1;

	// add new directory entry under the directory table of the root dir
	struct inode* root = inode_table_ptr+(super_ptr->root_dir_inode_num);
	for(int i=0; i<DIRECT_BLOCK_PTR_NUM; i++) {
		printf("int i: %d\n", i);
		if(root->direct_blk_ptr[i]==NULL) {
			// add new entry
			printf("add new entry at int i: %d\n", i);
			struct data_block* new_entry = allocateDataBlock();
			root->direct_blk_ptr[i] = new_entry;

			struct directory_entry* new_entry_ptr = (struct directory_entry*) new_entry;
			new_entry_ptr->inode_num = i_bitmap_cur_idx;
			new_entry_ptr->file_type = DIRECTORY;
			strncpy(new_entry_ptr->name, new_dirname, DIR_ENTRY_NAME_LEN);
			break;
		}
	}

	return 0;
}

static int do_mknod( const char *path, mode_t mode, dev_t rdev )
{
	printf("--- Creating a new file ...\n");
	const char* new_filename = path+1;
	printf("new file name: %s\n", new_filename);
	printf("i_bitmap_cur_idx is: %d\n", i_bitmap_cur_idx);
	i_bitmap_cur_idx++;
	printf("i_bitmap_cur_idx is: %d\n", i_bitmap_cur_idx);

	// allocate new inode
	struct inode* cur_inode_ptr = inode_table_ptr+i_bitmap_cur_idx;
	struct data_block* tmp_data_blk = allocateDataBlock();
	cur_inode_ptr->direct_blk_ptr[0] = tmp_data_blk;

	cur_inode_ptr->atime = return_current();
	cur_inode_ptr->ctime = return_current();
	cur_inode_ptr->mtime = return_current();

	// update inode bitmap
	i_bitmap.inode_bitmap[i_bitmap_cur_idx] = 1;

	// add new directory entry under the directory table of the root dir
	struct inode* root = inode_table_ptr+(super_ptr->root_dir_inode_num);
	for(int i=0; i<DIRECT_BLOCK_PTR_NUM; i++) {
		printf("int i: %d\n", i);
		if(root->direct_blk_ptr[i]==NULL) {
			// add new entry
			printf("add new entry at int i: %d\n", i);
			struct data_block* new_entry = allocateDataBlock();
			root->direct_blk_ptr[i] = new_entry;

			struct directory_entry* new_entry_ptr = (struct directory_entry*) new_entry;
			new_entry_ptr->inode_num = i_bitmap_cur_idx;
			new_entry_ptr->file_type = REGULAR;
			strncpy(new_entry_ptr->name, new_filename, DIR_ENTRY_NAME_LEN);
			break;
		}
	}

	return 0;
}

static int do_write( const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *info )
{
	printf("--- Writing to a file ...\n");
	const char* new_file_path = path+1;
	struct inode* root = inode_table_ptr+ ( super_ptr->root_dir_inode_num );
	
	if(strlen(buffer)<512) {
		// iterate through the directory table of the root dir
		struct directory_entry* tmp_entry;
		struct data_block* tmp_data_blk;
		for(int i=0; i<DIRECT_BLOCK_PTR_NUM; i++) {
			tmp_data_blk = root->direct_blk_ptr[i];
			if(tmp_data_blk==NULL)
				return -ENOENT;
			tmp_entry = (struct directory_entry* ) tmp_data_blk;
			if( strcmp(tmp_entry->name, new_file_path) == 0 && tmp_entry->inode_num != 0 ) {	// found the file/directory by the path parameter
				printf("found the file %d\n", i);
				struct	inode* tmp_found = inode_table_ptr + (tmp_entry->inode_num);
				strcpy(tmp_found->direct_blk_ptr[0]->buf, buffer);
				printf("file content: %s\n", tmp_found->direct_blk_ptr[0]->buf);
				printf("buffer: %s\n", buffer);
				tmp_found->mtime = return_current();
				break;
			}
		}
		return size;
	}

	return -1;
}

static int do_unlink( const char *path )
{
	const char* new_path = path+1;
	printf("--- Removing the file: %s ...\n", new_path);
	
	struct inode* root = inode_table_ptr+ ( super_ptr->root_dir_inode_num );
	struct directory_entry* tmp_entry;
	struct data_block* tmp_data_blk;

	// iterate through the directory table of the root dir
	for(int i=0; i<DIRECT_BLOCK_PTR_NUM; i++) {
		tmp_data_blk = root->direct_blk_ptr[i];
		if(tmp_data_blk==NULL)
			return -ENOENT;
		tmp_entry = (struct directory_entry* ) tmp_data_blk;
		if( strcmp(tmp_entry->name, new_path) == 0 && tmp_entry->inode_num != 0 ) {	// found the file by the path parameter
			printf("found the file %s\n", new_path);
			if(tmp_entry->file_type == 1) {	
				tmp_entry->inode_num = 0;
				return 0;
			}
			else
				return -1;
		}
	}
	
	return -ENOENT;
}

static int do_rmdir(const char * path)
{
	const char* new_path = path+1;
	printf("--- Removing the directory: %s ...\n", new_path);
	
	struct inode* root = inode_table_ptr+ ( super_ptr->root_dir_inode_num );
	struct directory_entry* tmp_entry;
	struct data_block* tmp_data_blk;

	// iterate through the directory table of the root dir
	for(int i=0; i<DIRECT_BLOCK_PTR_NUM; i++) {
		tmp_data_blk = root->direct_blk_ptr[i];
		if(tmp_data_blk==NULL)
			return -ENOENT;
		tmp_entry = (struct directory_entry* ) tmp_data_blk;
		if( strcmp(tmp_entry->name, new_path) == 0 && tmp_entry->inode_num != 0 ) {	// found the directory by the path parameter
			printf("found the directory %s\n", tmp_entry->name);
			if(tmp_entry->file_type == 2) {		
				tmp_entry->inode_num = 0;
				return 0;
			}
			else
				return -1;
		}
	}
	
	return -ENOENT;
}

static int do_utimens(const char * path, const struct timespec tv[2])//, struct fuse_file_info *fi)
{
	printf("--- Changing files/directories timestamps ...\n");
	
	printf("path: %s\n", path);
	const char* new_path = path+1;
	printf("new path: %s\n", new_path);

	struct inode* root = inode_table_ptr+ ( super_ptr->root_dir_inode_num );
	if ( strcmp( path, "/" ) == 0 ) {
		root->atime = return_current();
		root->ctime = return_current();
		root->mtime = return_current();
		return 0;
	}
	else {
		// iterate through the directory table of the root dir
		struct directory_entry* tmp_entry;
		struct data_block* tmp_data_blk;
		for(int i=0; i<DIRECT_BLOCK_PTR_NUM; i++) {
			tmp_data_blk = root->direct_blk_ptr[i];
			if(tmp_data_blk==NULL)
				return -ENOENT;
			tmp_entry = (struct directory_entry* ) tmp_data_blk;
			if( strcmp(tmp_entry->name, new_path) == 0 && tmp_entry->inode_num != 0 ) {	// found the file/directory by the path parameter
				printf("found the file %d\n", i);
				struct	inode* tmp_found = inode_table_ptr + (tmp_entry->inode_num);
				tmp_found->atime = return_current();
				tmp_found->ctime = return_current();
				tmp_found->mtime = return_current();
				break;
			}
		}
		return 0;
	}
	return -ENOENT;
}


static void* do_init(struct fuse_conn_info *conn)//, struct fuse_config *cfg)
{
	// Initialize filesystem
	printf("Initializing ISFS... \n");

	// 1. initialize the super block of ISFS
	init_super();

	// 2. initialize the block-bitmap of ISFS
	init_block_bitmap();

	// 3. initialize the inode-bitmap of ISFS
	init_inode_bitmap();

	// 4. initialize the inode table of ISFS
	init_inode_table_ptr();

	// 5. Creates the /root directory
	create_root_dir();

	// 6. Update the block bitmap and inode bitmap after creating /root directory

	blk_bitmap.blk_bitmap[blk_bitmap_cur_idx++] = 1;
	// i_bitmap.inode_bitmap[i_bitmap_cur_idx++] = 1;
	i_bitmap.inode_bitmap[i_bitmap_cur_idx] = 1;
	super_ptr->free_blk_count = super_ptr->free_blk_count-1;
	super_ptr->free_inode_count = super_ptr->free_inode_count-1;

	return NULL;
}

static struct fuse_operations operations = {
    .getattr	= do_getattr,
    .readdir	= do_readdir,
    .read		= do_read,
    .mkdir		= do_mkdir,
    .mknod		= do_mknod,
    .write		= do_write,
	.unlink		= do_unlink,
	.rmdir		= do_rmdir,
	.utimens	= do_utimens,
	.init		= do_init,
};

int main( int argc, char *argv[] )
{
	MNT_POINT = argv[2];

	return fuse_main( argc, argv, &operations, NULL );
}

static int init_super()
{
	super_ptr = (struct super_block*) calloc(1, sizeof(struct super_block));
	if (super_ptr == NULL) { 
		printf("Failed to initialize super block.\n"); 
		exit(0); 
	}
	printf("Successfully allocated super block.\n");
	super_ptr->blk_size = BLK_SIZE;
	super_ptr->inode_size = INODE_SIZE;
	super_ptr->blk_count = BLK_COUNT;
	super_ptr->inode_count = INODE_COUNT;
	super_ptr->free_blk_count = BLK_COUNT;
	super_ptr->free_inode_count = INODE_COUNT;
	super_ptr->root_dir_inode_num = ROOT_DIR_INODE_NUM;
	return 0;
}

static int init_block_bitmap()
{
	memset(blk_bitmap.blk_bitmap, 0, sizeof(int)*BLK_COUNT);
	printf("Successfully allocated block-bitmap.\n");
	return 0;
}

static int init_inode_bitmap()
{
	memset(i_bitmap.inode_bitmap, 0, sizeof(int)*INODE_COUNT);
	printf("Successfully allocated inode-bitmap.\n");
	return 0;
}

static int init_inode_table_ptr()
{
	inode_table_ptr = inode_table;
	if (inode_table_ptr == NULL) { 
		printf("Failed to initialize inode table pointer.\n"); 
		exit(0); 
	}
	printf("Successfully initialize inode table pointer.\n");
	return 0;
}

static int create_root_dir()
{
	// allocate one directory
	// allocate one data block
	// use inode table pointer, find one inode and points to the data block
	struct inode* cur_inode_ptr = inode_table_ptr+i_bitmap_cur_idx;

	/* allocate memory space for one data block */
	struct data_block* tmp_data_blk = allocateDataBlock();
	cur_inode_ptr->direct_blk_ptr[0] = tmp_data_blk;

	struct directory_entry* dir_entry_table_ptr = (struct directory_entry*) tmp_data_blk;
	dir_entry_table_ptr->inode_num = super_ptr->root_dir_inode_num;
	dir_entry_table_ptr->file_type = DIRECTORY;
	strncpy(dir_entry_table_ptr->name, ".", DIR_ENTRY_NAME_LEN);

	struct data_block* tmp_data_blk_2 = allocateDataBlock();
	cur_inode_ptr->direct_blk_ptr[1] = tmp_data_blk_2;
	
	struct directory_entry* dir_entry_table_ptr_2 = (struct directory_entry*) tmp_data_blk_2;
	dir_entry_table_ptr_2->inode_num = super_ptr->root_dir_inode_num;
	dir_entry_table_ptr_2->file_type = DIRECTORY;
	strncpy(dir_entry_table_ptr_2->name, "..", DIR_ENTRY_NAME_LEN);

	cur_inode_ptr->atime = return_current();
	cur_inode_ptr->ctime = return_current();
	cur_inode_ptr->mtime = return_current();

	printf("inode_table_ptr addr: %p\n", cur_inode_ptr->direct_blk_ptr[0]);
	struct directory_entry* dir_ptr = (struct directory_entry*) (cur_inode_ptr)->direct_blk_ptr[0];
	printf("dir_ptr addr: %p\n", dir_ptr);
	printf("dir_ptr inode num: %d\n", dir_ptr->inode_num);
	printf("dir_ptr filetype: %d\n", dir_ptr->file_type);
	printf("dir_ptr name: %s\n", dir_ptr->name);
	dir_ptr = (struct directory_entry*) (cur_inode_ptr)->direct_blk_ptr[1];
	printf("dir_ptr addr: %p\n", dir_ptr);
	printf("dir_ptr inode num: %d\n", dir_ptr->inode_num);
	printf("dir_ptr filetype: %d\n", dir_ptr->file_type);
	printf("dir_ptr name: %s\n", dir_ptr->name);
	dir_ptr = (struct directory_entry*) (cur_inode_ptr)->direct_blk_ptr[2];
	if(dir_ptr == NULL)
		printf("dir_ptr is NULL\n");
	else
		printf("dir_ptr addr: %p\n", dir_ptr);

	return 0;
}

void open_storage()
{
	// char* path = strcat(MNT_POINT, "test1.txt");
	// printf("%s\n", path);
	
	// printf("%s\n", MNT_POINT); 

	// int fd = open("/dev/sdg", O_RDWR);

	// if (fd == -1) {
	// 	fprintf(stderr, "error open storage.\n");
	// 	exit(1);
	// }
  	// printf("storage fd = %d\n", fd);

	// int sz = write(fd, "hello geeks\n", strlen("hello geeks\n"));
  
  	// printf("called write(%d, \"hello geeks\\n\", %ld). It returned %d\n", fd, strlen("hello geeks\n"), sz);
  
  	// close(fd);

	// char *buf = (char *) calloc(100, sizeof(char));
  
  	// fd = open("/dev/sdg", O_RDONLY);

	// if (fd < 0) { perror("r1"); exit(1); } 
	
	// sz = read(fd, buf, 10); 
	// printf("called read(%d, buf, 10).  returned that %d bytes  were read.\n", fd, sz); 
	// buf[sz] = '\0'; 
	// printf("Those bytes are as follows: %s\n", buf);

	// close(fd);

	return;
}

struct timespec return_current()
{
	struct timespec current_time;

	clock_gettime(CLOCK_REALTIME, &current_time);

	return current_time;
}

struct data_block* allocateDataBlock()
{
	struct data_block* tmp_data_blk = (struct data_block*) malloc(sizeof(struct data_block));	
	memset(tmp_data_blk, 0, sizeof(struct data_block));
	return tmp_data_blk;
}