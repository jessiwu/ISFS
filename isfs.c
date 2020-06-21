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
// static struct block_bitmap* blk_bitmap_ptr;
static struct block_bitmap blk_bitmap;
static int blk_bitmap_cur_idx = 0;

/* a pointer which points to a inode-bitmap data structure */
// static struct inode_bitmap* inode_bitmap_ptr;
static struct inode_bitmap i_bitmap;
static int i_bitmap_cur_idx;

/* a pointer which points to inode data structure: indicating the first inode address */
static struct inode* inode_table_ptr = 0;

/* a pointer which points to a data blocks table data structure */
// static struct data_block_table* storage = data_block_table;

static int init_super();
static int init_block_bitmap();
static int init_inode_bitmap();
static int init_inode_table_ptr();
static int create_root_dir();

struct timespec return_current();


// ... //

char* MNT_POINT;

char dir_list[ 256 ][ 256 ];
int curr_dir_idx = -1;

char files_list[ 256 ][ 256 ];
int curr_file_idx = -1;

char files_content[ 256 ][ 256 ];
int curr_file_content_idx = -1;

struct timespec dir_atime_list[256];
struct timespec dir_mtime_list[256];
struct timespec dir_ctime_list[256];

struct timespec file_atime_list[256];
struct timespec file_mtime_list[256];
struct timespec file_ctime_list[256];

void add_dir( const char *dir_name )
{
	curr_dir_idx++;
	strcpy( dir_list[ curr_dir_idx ], dir_name );

	struct timespec current_time;

	clock_gettime(CLOCK_REALTIME, &current_time);
	dir_atime_list[curr_dir_idx] = current_time;
	dir_mtime_list[curr_dir_idx] = current_time;
	dir_ctime_list[curr_dir_idx] = current_time;
}

int is_dir( const char *path )
{
	path++; // Eliminating "/" in the path
	
	for ( int curr_idx = 0; curr_idx <= curr_dir_idx; curr_idx++ )
		if ( strcmp( path, dir_list[ curr_idx ] ) == 0 )
			return 1;
	
	return 0;
}

void add_file( const char *filename )
{
	curr_file_idx++;
	strcpy( files_list[ curr_file_idx ], filename );
	
	curr_file_content_idx++;
	strcpy( files_content[ curr_file_content_idx ], "" );

	struct timespec current_time;

	clock_gettime(CLOCK_REALTIME, &current_time);
	file_atime_list[curr_file_idx] = current_time;
	file_mtime_list[curr_file_idx] = current_time;
	file_ctime_list[curr_file_idx] = current_time;
}

int is_file( const char *path )
{
	path++; // Eliminating "/" in the path
	
	for ( int curr_idx = 0; curr_idx <= curr_file_idx; curr_idx++ )
		if ( strcmp( path, files_list[ curr_idx ] ) == 0 )
			return 1;
	
	return 0;
}

int get_file_index( const char *path )
{
	path++; // Eliminating "/" in the path
	
	for ( int curr_idx = 0; curr_idx <= curr_file_idx; curr_idx++ )
		if ( strcmp( path, files_list[ curr_idx ] ) == 0 )
			return curr_idx;
	
	return -1;
}

int get_dir_index( const char *path )
{
	path++; // Eliminating "/" in the path
	
	for ( int curr_idx = 0; curr_idx <= curr_dir_idx; curr_idx++ )
		if ( strcmp( path, dir_list[ curr_idx ] ) == 0 )
			return curr_idx;
	
	return -1;
}


void write_to_file( const char *path, const char *new_content )
{

	int file_idx = get_file_index( path );
	
	if ( file_idx == -1 ) // No such file
		return;
		
	strcpy( files_content[ file_idx ], new_content );
	
	struct timespec current_time;

	clock_gettime(CLOCK_REALTIME, &current_time);
	file_mtime_list[file_idx] = current_time;
}

int remove_file( const char* path )
{
	if( is_file(path) ) {
		int file_idx = get_file_index( path );

		if ( file_idx == -1 ) // No such file
			return -ENOENT;
		else {	 // Delete this file
			path++;

			// test
			if(file_idx==curr_file_idx){
				memset(files_list[ curr_file_idx ], '\0', sizeof(files_list[ curr_file_idx ]));
				memset(files_content[ curr_file_idx ], '\0', sizeof(files_content[ curr_file_idx ]));
			}
			else {
				for ( int curr_idx = file_idx+1; curr_idx <= curr_file_idx; curr_idx++ ) {
					memset(files_list[ curr_idx-1 ], '\0', sizeof(files_list[ curr_idx-1 ]));
					memset(files_content[ curr_idx-1 ], '\0', sizeof(files_content[ curr_idx-1 ]));
					strcpy( files_list[ curr_idx-1  ], files_list[ curr_idx ] ); 
					strcpy( files_content[ curr_idx-1  ],  files_content[ curr_idx ]);
				}
			}

			curr_file_idx--;
			curr_file_content_idx--;
			
			return 0; // delete file successfully
		}
	}
	else
		return -1;
}

int remove_dir( const char* path )
{
	if( is_dir(path) ) {
		int dir_idx = get_dir_index( path );

		if ( dir_idx == -1 ) // No such file
			return -ENOENT;
		else {	 // Delete this file
			path++;

			// test
			if(dir_idx==curr_dir_idx){
				memset(dir_list[ curr_dir_idx ], '\0', sizeof(dir_list[ curr_dir_idx ]));
			}
			else {
				for ( int curr_idx = dir_idx+1; curr_idx <= curr_dir_idx; curr_idx++ ) {
					memset(dir_list[ curr_idx-1 ], '\0', sizeof(dir_list[ curr_idx-1 ]));
					strcpy( dir_list[ curr_idx-1  ], dir_list[ curr_idx ] ); 
				}
			}
			curr_dir_idx--;
			return 0; // delete file successfully
		}
	}
	else
		return -1;
}

// ... //

static int do_getattr( const char *path, struct stat *st )
{
	st->st_uid = getuid(); // The owner of the file/directory is the user who mounted the filesystem
	st->st_gid = getgid(); // The group of the file/directory is the same as the group of the user who mounted the filesystem
	
	struct inode* tmp = inode_table_ptr+2;
	printf("path: %s\n", path);
	if ( strcmp( path, "/" ) == 0 ) // || is_dir( path ) == 1 )
	{
		st->st_mode = S_IFDIR | 0744;		// Permission bits (octal)
		st->st_nlink = 2;
		st->st_atime = tmp->atime.tv_sec;
		st->st_ctime = tmp->ctime.tv_sec;
		st->st_mtime = tmp->mtime.tv_sec;
	}
	else if ( is_dir( path ) ==1 ) {
		int dir_idx = get_dir_index(path);
		st->st_mode = S_IFDIR | 0755;
		st->st_nlink = 2;
		st->st_atime = dir_atime_list[ dir_idx ].tv_sec;
		st->st_mtime = dir_mtime_list[ dir_idx ].tv_sec;
		st->st_ctime = dir_ctime_list[ dir_idx ].tv_sec;
	}
	else if ( is_file( path ) == 1 )
	{
		int file_idx = get_file_index( path );
		st->st_mode = S_IFREG | 0644;
		st->st_nlink = 1;
		// st->st_size = 1024;
		st->st_size = strlen(files_content[file_idx]);

		st->st_atime = file_atime_list[ file_idx ].tv_sec;
		st->st_mtime = file_mtime_list[ file_idx ].tv_sec;
		st->st_ctime = file_ctime_list[ file_idx ].tv_sec;
	}
	else
	{
		return -ENOENT;
	}
	
	return 0;
}

static int do_readdir( const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi )
{
	filler( buffer, ".", NULL, 0 ); // Current Directory
	filler( buffer, "..", NULL, 0 ); // Parent Directory
	
	if ( strcmp( path, "/" ) == 0 ) // If the user is trying to show the files/directories of the root directory show the following
	{
		return 0;
	}
	return 0;
}

static int do_read( const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi )
{
	int file_idx = get_file_index( path );
	
	if ( file_idx == -1 )
		return -1;
	
	char *content = files_content[ file_idx ];
	
	memcpy( buffer, content + offset, size );

	struct timespec current_time;

	clock_gettime(CLOCK_REALTIME, &current_time);
	file_atime_list[file_idx] = current_time;
		
	return strlen( content ) - offset;
}

static int do_mkdir( const char *path, mode_t mode )
{
	path++;
	add_dir( path );
	
	return 0;
}

static int do_mknod( const char *path, mode_t mode, dev_t rdev )
{
	path++;
	add_file( path );
	
	return 0;
}

static int do_write( const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *info )
{
	write_to_file( path, buffer );
	
	return size;
}

static int do_unlink( const char *path )
{
	int res = remove_file(path);
	
	return res;
}

static int do_rmdir(const char * path)
{
	int res = remove_dir(path);
	return res;
}

static int do_utimens(const char * path, const struct timespec tv[2])//, struct fuse_file_info *fi)
{
	if( is_dir(path) ){
		int dir_idx = get_dir_index( path );

		if( dir_idx == -1 )
			return -ENOENT;
		else{
			struct timespec current_time;

			clock_gettime(CLOCK_REALTIME, &current_time);
			dir_atime_list[dir_idx] = current_time;
			dir_mtime_list[dir_idx] = current_time;
			dir_ctime_list[dir_idx] = current_time;

			return 0;
		}
	}
	else if( is_file(path) ) {
		int file_idx = get_file_index( path );

		if( file_idx == -1 )
			return -ENOENT;
		else{
			struct timespec current_time;

			clock_gettime(CLOCK_REALTIME, &current_time);
			file_atime_list[file_idx] = current_time;
			file_mtime_list[file_idx] = current_time;
			file_ctime_list[file_idx] = current_time;
			
			return 0;
		}
	}
	else
		return -1;
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
	super_ptr->root_dir_inode_num = 2;

	// 6. Update the block bitmap and inode bitmap after creating /root directory
	// for(int i=0; i<5; i++){
	// 	printf("%d: %d\n", i, blk_bitmap.blk_bitmap[i]);
	// }

	// printf("--------\n");
	// for(int i=0; i<5; i++){
	// 	printf("%d: %d\n", i, i_bitmap.inode_bitmap[i]);
	// }
	// printf("--------\n");

	// blk_bitmap.blk_bitmap[blk_bitmap_cur_idx++] = 1;
	// i_bitmap.inode_bitmap[i_bitmap_cur_idx++] = 1;
	// super_ptr->free_blk_count = super_ptr->free_blk_count-1;
	// super_ptr->free_inode_count = super_ptr->free_inode_count-1;
	
	// for(int i=0; i<5; i++){
	// 	printf("%d: %d\n", i, blk_bitmap.blk_bitmap[i] );
	// }

	// printf("--------\n");
	// for(int i=0; i<5; i++){
	// 	printf("%d: %d\n", i, i_bitmap.inode_bitmap[i] );
	// }

	/*
	struct data_block* tmp_data_blk = (struct data_block*) malloc(sizeof(struct data_block));	// allocate memory space for one data block
	memcpy(tmp_data_blk->buf, "abc", 4);	// assign data information to this data block
	
	inode_table_ptr->direct_blk_ptr[0] = tmp_data_blk;

	printf("data block: %s\n", tmp_data_blk->buf);
	printf("data block addr: %p\n", tmp_data_blk);

	printf("inode_table_ptr: %s\n", inode_table_ptr->direct_blk_ptr[0]->buf);
	printf("inode_table_ptr: %p\n", inode_table_ptr->direct_blk_ptr[0]);

	for(int i=0; i<5; ++i) {
		printf("%d: %s\n", i, inode_table_ptr->direct_blk_ptr[0]->buf);
		printf("%d: %p\n", i, inode_table_ptr->direct_blk_ptr[0]);
		inode_table_ptr = inode_table_ptr+1;	// iterate through the inode table with a pointer
	}*/
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
	return 0;
}

static int init_block_bitmap()
{

	memset(blk_bitmap.blk_bitmap, 0, sizeof(int)*BLK_COUNT);
	// blk_bitmap_ptr = (struct block_bitmap*) calloc(1, sizeof(struct block_bitmap));

	// if (blk_bitmap_ptr == NULL) { 
	// 	printf("Failed to initialize block-bitmap.\n"); 
	// 	exit(0); 
	// }
	// memset(blk_bitmap_ptr, 0, sizeof(struct block_bitmap));
	printf("Successfully allocated block-bitmap.\n");
	return 0;
}

static int init_inode_bitmap()
{

	memset(i_bitmap.inode_bitmap, 0, sizeof(int)*INODE_COUNT);
	// inode_bitmap_ptr = (struct inode_bitmap*) calloc(1, sizeof(struct inode_bitmap));
	// if (inode_bitmap_ptr == NULL) { 
	// 	printf("Failed to initialize inode-bitmap.\n"); 
	// 	exit(0); 
	// }
	// memset(inode_bitmap_ptr, 0, sizeof(struct inode_bitmap));
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
	struct inode* cur_inode_ptr = inode_table_ptr+2;

	struct data_block* tmp_data_blk = (struct data_block*) malloc(sizeof(struct data_block));	// allocate memory space for one data block
	memset(tmp_data_blk, 0, sizeof(struct data_block));
	cur_inode_ptr->direct_blk_ptr[0] = tmp_data_blk;

	struct directory_entry* dir_entry_table_ptr = (struct directory_entry*) tmp_data_blk;
	dir_entry_table_ptr->inode_num = 2;
	dir_entry_table_ptr->file_type = DIRECTORY;
	strncpy(dir_entry_table_ptr->name, ".", DIR_ENTRY_NAME_LEN);
	// dir_entry_table_ptr++;
	// dir_entry_table_ptr->inode_num = 2;
	// dir_entry_table_ptr->file_type = DIRECTORY;
	// strncpy(dir_entry_table_ptr->name, "..", DIR_ENTRY_NAME_LEN);
	// dir_entry_table_ptr++;

	cur_inode_ptr->atime = return_current();
	cur_inode_ptr->ctime = return_current();
	cur_inode_ptr->mtime = return_current();

	printf("inode_table_ptr addr: %p\n", cur_inode_ptr->direct_blk_ptr[0]);
	struct directory_entry* dir_ptr = (struct directory_entry*) (cur_inode_ptr)->direct_blk_ptr[0];
	printf("dir_ptr addr: %p\n", dir_ptr);
	printf("dir_ptr inode num: %d\n", dir_ptr->inode_num);
	printf("dir_ptr filetype: %d\n", dir_ptr->file_type);
	printf("dir_ptr name: %s\n", dir_ptr->name);
	dir_ptr++;
	printf("dir_ptr addr: %p\n", dir_ptr);
	printf("dir_ptr inode num: %d\n", dir_ptr->inode_num);
	printf("dir_ptr filetype: %d\n", dir_ptr->file_type);
	printf("dir_ptr name: %s\n", dir_ptr->name);

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