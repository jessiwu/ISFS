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

/* a pointer which points to a data blocks table data structure */
static struct data_block* data_blk_table_ptr = 0;
static int last_read_idx = 0;

static int init_super();
static int init_block_bitmap();
static int init_inode_bitmap();
static int init_inode_table_ptr();
static int init_data_blk_table_ptr();
static int create_rootdir();

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

		struct directory_entry* tmp_entry;
		struct data_block* tmp_data_blk;
		// iterate through direct blk ptr
		for(int i=0; i<DIRECT_BLOCK_PTR_NUM; i++) {
			if(root->direct[i]==-1){
				return -ENOENT;
			}
			tmp_data_blk = data_blk_table_ptr + root->direct[i];
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
				return 0;
			}
		}
		// iterate through single indirect blk ptr
		if(root->single_indirect!=-1) {
			struct indirect_data_block* single_indir =  (struct indirect_data_block* ) data_blk_table_ptr + root->single_indirect;
			for(int j=0;j<INDIRECT_BLOCK_PTR_NUM; j++) {
				if(single_indir->idx[j] == NULL)
					break;
				tmp_data_blk = data_blk_table_ptr + single_indir->idx[j];
				tmp_entry = (struct directory_entry* ) tmp_data_blk;
				if( strcmp(tmp_entry->name, new_path) == 0 && tmp_entry->inode_num != 0 ) {	// found the file/directory by the path parameter
					printf("found the file: %s at blk idx %d\n", tmp_entry->name, single_indir->idx[j]);
					struct	inode* tmp_found = inode_table_ptr + (tmp_entry->inode_num);
					st->st_mode = ( tmp_entry->file_type == 2) ? S_IFDIR | 0755 : S_IFREG | 0644;
					st->st_size = super_ptr->blk_size;		// unsupport big file ( only <= 512 bytes )
					st->st_nlink = ( tmp_entry->file_type == 2) ? 2 : 1;
					st->st_ino = tmp_entry->inode_num;		// need run with this option: -o use_ino
					st->st_atime = tmp_found->atime.tv_sec;
					st->st_ctime = tmp_found->ctime.tv_sec;
					st->st_mtime = tmp_found->mtime.tv_sec;
					return 0;
				}
			}
		}
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
		struct directory_entry* tmp_entry;
		struct data_block* tmp_data_blk;
		// iterate through direct blk ptr
		for(int i=0; i<DIRECT_BLOCK_PTR_NUM; i++) {
			if(root->direct[i]!=-1){
				tmp_data_blk = data_blk_table_ptr + root->direct[i];
				tmp_entry = (struct directory_entry* ) tmp_data_blk;
				if(tmp_entry->inode_num!=0) {
					printf("- entry name: %s\n", tmp_entry->name);
					filler( buffer, tmp_entry->name, NULL, 0);
				}
			}
		}
		// iterate through single indirect blk ptr
		if(root->single_indirect!=-1) {
			struct indirect_data_block* single_indir =  (struct indirect_data_block* ) data_blk_table_ptr + root->single_indirect;
			for(int i=0;i<INDIRECT_BLOCK_PTR_NUM; i++) {
				if(single_indir->idx[i]==NULL)
					break;
				tmp_data_blk = data_blk_table_ptr + single_indir->idx[i];
				tmp_entry = (struct directory_entry* ) tmp_data_blk;
				if(tmp_entry->inode_num!=0) {
					printf("- entry name: %s\n", tmp_entry->name);
					filler( buffer, tmp_entry->name, NULL, 0);
				}
			}
		}
		return 0;
	}
	else {	// read directories other than rootdir

		struct directory_entry* tmp_entry;
		struct data_block* tmp_data_blk;
		printf("!!! read directories other than rootdir !!!!\n");
		// iterate through the directory table of the root dir
		// iterate through direct blk ptr of the rootdir
		for(int i=0; i<DIRECT_BLOCK_PTR_NUM; i++) {
			if(root->direct[i]!=-1){	
				tmp_data_blk = data_blk_table_ptr + root->direct[i];
				tmp_entry = (struct directory_entry* ) tmp_data_blk;
				if( strcmp(tmp_entry->name, new_path) == 0 && tmp_entry->inode_num != 0 ) {	// found this requested directory by the path parameter
					struct	inode* tmp_found = inode_table_ptr + (tmp_entry->inode_num);

					struct data_block* tmp_data_blk_2;
					struct directory_entry* entry;
					//iterate through the directories of this requested directory
					for(int j=0; j< DIRECT_BLOCK_PTR_NUM; j++) {
						tmp_data_blk_2 = data_blk_table_ptr + tmp_found->direct[j];
						if(tmp_found->direct[j]!=-1){
							entry = (struct directory_entry*) tmp_data_blk_2;
							if(entry->inode_num!=0) {
								printf("entry name: %s\n", entry->name);
								filler( buffer, entry->name, NULL, 0);
							}
						}
					}
					// iterate through single indirect blk ptr of this requested directory
					if(tmp_found->single_indirect!=-1) {
						struct indirect_data_block* single_indir =  (struct indirect_data_block* ) data_blk_table_ptr + tmp_found->single_indirect;
						for(int k=0;k<INDIRECT_BLOCK_PTR_NUM; k++) {
							if(single_indir->idx[k]==NULL)
								break;
							tmp_data_blk_2 = data_blk_table_ptr + single_indir->idx[k];
							entry = (struct directory_entry* ) tmp_data_blk_2;
							if(entry->inode_num!=0) {
								printf("- entry name: %s\n", entry->name);
								filler( buffer, entry->name, NULL, 0);
							}
						}
					}
					return 0;
				}
			}
		}
		// iterate through single indirect blk ptr of the rootdir
		if(root->single_indirect!=-1) {
			struct indirect_data_block* single_indir =  (struct indirect_data_block* ) data_blk_table_ptr + root->single_indirect;

			for(int i=0;i<INDIRECT_BLOCK_PTR_NUM; i++) {
				if(single_indir->idx[i]==NULL)
					break;
				tmp_data_blk = data_blk_table_ptr + single_indir->idx[i];
				tmp_entry = (struct directory_entry* ) tmp_data_blk;
				// found the requested directory by the path parameter
				if( strcmp(tmp_entry->name, new_path) == 0 && tmp_entry->inode_num != 0 ) {	
					printf("found the directory at single indirect blk at idx: %d\n", root->single_indirect);

					struct	inode* tmp_found = inode_table_ptr + (tmp_entry->inode_num);

					struct data_block* tmp_data_blk_2;
					struct directory_entry* entry;

					//iterate through the directories of this requested directory
					for(int i=0; i< DIRECT_BLOCK_PTR_NUM; i++) {
						tmp_data_blk_2 = data_blk_table_ptr + tmp_found->direct[i];
						if(tmp_found->direct[i]!=-1){
							entry = (struct directory_entry*) tmp_data_blk_2;
							if(entry->inode_num!=0) {
								printf("entry name: %s\n", entry->name);
								filler( buffer, entry->name, NULL, 0);
							}
						}
					}
					// iterate through single indirect blk ptr of this requested directory
					if(tmp_found->single_indirect!=-1) {
						struct indirect_data_block* single_indir =  (struct indirect_data_block* ) data_blk_table_ptr + tmp_found->single_indirect;
						for(int i=0;i<INDIRECT_BLOCK_PTR_NUM; i++) {
							if(single_indir->idx[i]==NULL)
								break;
							tmp_data_blk_2 = data_blk_table_ptr + single_indir->idx[i];
							entry = (struct directory_entry* ) tmp_data_blk_2;
							if(entry->inode_num!=0) {
								printf("- entry name: %s\n", entry->name);
								filler( buffer, entry->name, NULL, 0);
							}
						}
					}
					return 0;
				}
			}
		}
	}
	return -1;
}

static int do_read( const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi )
{
	printf("--- Reading a file ...\n");
	const char* filename = path+1;
	char* file_content;
	struct inode* root = inode_table_ptr+ ( super_ptr->root_dir_inode_num );

	struct directory_entry* tmp_entry;
	struct data_block* tmp_data_blk;
	// iterate through the directory table of the root dir
	// iterate through direct blk ptr of the rootdir
	for(int i=0; i<DIRECT_BLOCK_PTR_NUM; i++) {
		if(root->direct[i]!=-1){	
			tmp_data_blk = data_blk_table_ptr + root->direct[i];
			tmp_entry = (struct directory_entry* ) tmp_data_blk;
			if( strcmp(tmp_entry->name, filename) == 0 && tmp_entry->inode_num != 0 ) {	// found this requested directory by the path parameter
				struct	inode* tmp_found = inode_table_ptr + (tmp_entry->inode_num);

				struct data_block* tmp_data_blk_2;
				//iterate through the directories of this requested directory
				if(last_read_idx != -1) {
					for(int j=last_read_idx; j< DIRECT_BLOCK_PTR_NUM; j++) {
						tmp_data_blk_2 = data_blk_table_ptr + tmp_found->direct[j];
						if(tmp_found->direct[j]!=-1){
							file_content = tmp_data_blk_2->buf;
							printf("j: %d\n", j);
							printf("offset: %d\n", offset);
							printf("file content: %s\n", tmp_data_blk_2->buf);
							memcpy(buffer, file_content + offset, size);	// copy file content to the buffer
							printf("cat buffer: %s\n", buffer);
							tmp_found->mtime = return_current();
							last_read_idx = j;
							if(j==11)
								last_read_idx == -1;
						}
					}
				}
				
				last_read_idx = 0;
				// iterate through single indirect blk ptr of this requested directory
				if(tmp_found->single_indirect!=-1) {
					struct indirect_data_block* single_indir =  (struct indirect_data_block* ) data_blk_table_ptr + (tmp_found->single_indirect);
					printf("single_indirect: %d\n", tmp_found->single_indirect);
					printf("last_read_idx: %d\n", last_read_idx);
					for(int k=last_read_idx;k<INDIRECT_BLOCK_PTR_NUM; k++) {
						if( (single_indir->idx[k]) == NULL){
							printf("is NULL\n");
							break;
						}
						tmp_data_blk_2 = data_blk_table_ptr + (single_indir->idx[k]);
						if(tmp_found->direct[k]!=-1){
							file_content = tmp_data_blk_2->buf;
							printf("k: %d\n", k);
							printf("offset: %d\n", offset);
							printf("file content: %s\n", tmp_data_blk_2->buf);
							memcpy(buffer, file_content + offset, size);	// copy file content to the buffer
							printf("cat buffer: %s\n", buffer);
							tmp_found->mtime = return_current();
							last_read_idx = k;
							if(k==11)
								last_read_idx == -1;
						}
					}
				}
			}
		}
	}
	return strlen( file_content );
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

	/* assign data blocks to this node */
	cur_inode_ptr->direct[0] = blk_bitmap_cur_idx++;
	struct data_block* tmp_data_blk = data_blk_table_ptr + (cur_inode_ptr->direct[0]);

	/* add . directory entry in directory entry table of rootdir */
	struct directory_entry* dir_entry_table_ptr = (struct directory_entry*) tmp_data_blk;
	dir_entry_table_ptr->inode_num = super_ptr->root_dir_inode_num;
	dir_entry_table_ptr->file_type = DIRECTORY;
	strncpy(dir_entry_table_ptr->name, ".", DIR_ENTRY_NAME_LEN);

	/* assign data blocks to this node */
	cur_inode_ptr->direct[1] = blk_bitmap_cur_idx++;
	struct data_block* tmp_data_blk_2 = data_blk_table_ptr + (cur_inode_ptr->direct[1]);

	/* add .. directory entry in directory entry table of rootdir */
	struct directory_entry* dir_entry_table_ptr_2 = (struct directory_entry*) tmp_data_blk_2;
	dir_entry_table_ptr_2->inode_num = super_ptr->root_dir_inode_num;
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
		if(root->direct[i]==-1) {
			// add a new directory entry 
			printf("add new entry at int i: %d\n", i);
			root->direct[i] = blk_bitmap_cur_idx++;
			struct data_block* tmp_data_blk = data_blk_table_ptr + (root->direct[i]);
			struct directory_entry* new_entry_ptr = (struct directory_entry*) tmp_data_blk;

			new_entry_ptr->inode_num = i_bitmap_cur_idx;
			new_entry_ptr->file_type = DIRECTORY;
			strncpy(new_entry_ptr->name, new_dirname, DIR_ENTRY_NAME_LEN);
			break;
		}
	}
	//TODO: add new dir entry in indirect data blk if there's no enough direct data blk

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
	// cur_inode_ptr->direct[0] = blk_bitmap_cur_idx++;
	// struct data_block* tmp_data_blk = data_blk_table_ptr + (cur_inode_ptr->direct[0]);

	cur_inode_ptr->atime = return_current();
	cur_inode_ptr->ctime = return_current();
	cur_inode_ptr->mtime = return_current();

	// update inode bitmap
	i_bitmap.inode_bitmap[i_bitmap_cur_idx] = 1;

	// add new directory entry under the directory table of the root dir
	struct inode* root = inode_table_ptr+(super_ptr->root_dir_inode_num);
	for(int i=0; i<DIRECT_BLOCK_PTR_NUM; i++) {
		printf("int i: %d\n", i);
		if(root->direct[i]==-1) {
			// add a new directory entry 
			printf("add new entry at int i: %d\n", i);
			root->direct[i] = blk_bitmap_cur_idx++;
			struct data_block* tmp_data_blk = data_blk_table_ptr + (root->direct[i]);
			struct directory_entry* new_entry_ptr = (struct directory_entry*) tmp_data_blk;

			new_entry_ptr->inode_num = i_bitmap_cur_idx;
			new_entry_ptr->file_type = REGULAR;
			strncpy(new_entry_ptr->name, new_filename, DIR_ENTRY_NAME_LEN);
			break;
		}
	}
	// TODO: add new dir entry in indirect data blk if there's no enough direct data blk

	return 0;
}

static int do_write( const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *info )
{
	printf("--- Writing to a file ...\n");
	const char* new_file_path = path+1;
	int need_blk_num = strlen(buffer) / BLK_SIZE;
	int first_not_used_idx = -1;
	struct inode* root = inode_table_ptr+ ( super_ptr->root_dir_inode_num );
	printf("buffer len is: %d\n", strlen(buffer));
	printf("need blks num is: %d\n", need_blk_num);

	if(offset>=4096)
		offset = 0;
	
	// iterate through the directory table of the root dir
	struct directory_entry* tmp_entry;
	struct data_block* tmp_data_blk;
	for(int i=0; i<DIRECT_BLOCK_PTR_NUM; i++) {
		if(root->direct[i]!=-1) {
			tmp_data_blk = data_blk_table_ptr + (root->direct[i]);
			tmp_entry = (struct directory_entry* ) tmp_data_blk;
			if( strcmp(tmp_entry->name, new_file_path) == 0 && tmp_entry->inode_num != 0 ) {	// found the file/directory by the path parameter
				printf("found the file: %s\n", tmp_entry->name);
				struct	inode* tmp_found = inode_table_ptr + (tmp_entry->inode_num);
				
				// used or not 
				for(int i=0; i<DIRECT_BLOCK_PTR_NUM; i++) {
					if(tmp_found->direct[i]==-1) {
						first_not_used_idx = i;
						break;
					}
				}

				int ALL_DIRECT_BLKS_ARE_USED = 0;
				char file_content[512];
				struct data_block* tmp_data_blk_2;
				// there are still left some direct data blocks
				if( first_not_used_idx != -1) {
					printf(" !!! NOT ALL_DIRECT_BLKS_ARE_USED !!!\n");
					// write content to this file
					for(int i=first_not_used_idx; i<DIRECT_BLOCK_PTR_NUM; i++) {
						
						// allocate one data blocks
						tmp_found->direct[i] = blk_bitmap_cur_idx++;
						tmp_data_blk_2 = data_blk_table_ptr + (tmp_found->direct[i]);

						// split buffer to 512 bytes 
						printf("i: %d\n", i);
						printf("offset: %d\n", offset);
						strncpy(file_content, buffer+offset, BLK_SIZE-1);
						offset = offset+BLK_SIZE-1;
						if(strlen(file_content)==0)
							break;
						// write 512 bytes content into one data block
						strcpy(tmp_data_blk_2->buf, file_content);

						printf("file content: %s\n", tmp_data_blk_2->buf);
						printf("buffer: %s\n", file_content);
						tmp_found->mtime = return_current();
						if(i==11)
							ALL_DIRECT_BLKS_ARE_USED = 1;
					}
				}
				// if all the direct data blks are used 

				if(ALL_DIRECT_BLKS_ARE_USED==1 || first_not_used_idx == -1) {	// start to used single indirect blk
					if(tmp_found->single_indirect!=-1) {
						printf(" !!! ALL_DATA_BLKS_ARE_USED !!!\n");
						return -1;
					}
					// allocate one data blocks
					printf(" !!! ALL_DIRECT_BLKS_ARE_USED !!!\n");

					if(offset>=4096)
						offset = 0;

					tmp_found->single_indirect = blk_bitmap_cur_idx++;
					struct indirect_data_block* single_indir = (struct indirect_data_block* ) data_blk_table_ptr + (tmp_found->single_indirect);
					
					// used or not 
					for(int i=0; i<DIRECT_BLOCK_PTR_NUM; i++) {
						if(single_indir->idx[i] == NULL){
							first_not_used_idx = i;
						}
					}

					for(int i=first_not_used_idx; i<INDIRECT_BLOCK_PTR_NUM; i++) {
						printf("j: %d\n", i);
						printf("offset: %d\n", offset);
						single_indir->idx[i] = blk_bitmap_cur_idx++;
						tmp_data_blk_2 = data_blk_table_ptr + (single_indir->idx[i]);

						// split buffer to 512 bytes 
						strncpy(file_content, buffer+offset, BLK_SIZE-1);
						offset = offset+BLK_SIZE-1;
						if(strlen(file_content)==0)
							break;
						printf("buffer: %s\n", file_content);

						// write 512 bytes content into one data block
						strcpy(tmp_data_blk_2->buf, file_content);
						// memcpy(tmp_data_blk_2->buf, file_content, 512);
						printf("file content: %s\n", tmp_data_blk_2->buf);

						if(offset>=4096)
							offset = 0;
					}
				}
				return strlen(buffer);
			}
		}
	}
	return strlen(buffer);
}

static int do_unlink( const char *path )
{
	const char* new_path = path+1;
	printf("--- Removing the file: %s ...\n", new_path);
	
	struct inode* root = inode_table_ptr+ ( super_ptr->root_dir_inode_num );
	struct directory_entry* tmp_entry;
	struct data_block* tmp_data_blk;

	// iterate through the directory table of the root dir
	// iterate through direct blk ptr
	for(int i=0; i<DIRECT_BLOCK_PTR_NUM; i++) {
		tmp_data_blk = root->direct_blk_ptr[i];
		if(root->direct[i]!=-1){
			tmp_data_blk = data_blk_table_ptr + root->direct[i];
			tmp_entry = (struct directory_entry* ) tmp_data_blk;
			if( strcmp(tmp_entry->name, new_path) == 0 && tmp_entry->inode_num != 0 ) {	// found the file/directory by the path parameter
				printf("found the file: %s\n", tmp_entry->name);
				if(tmp_entry->file_type == REGULAR) {		
					tmp_entry->inode_num = 0;
					return 0;
				}
				else
					return -1;
			}
		}
	}
	// iterate through single indirect blk ptr
	if(root->single_indirect!=-1) {
		struct indirect_data_block* single_indir =  (struct indirect_data_block* ) data_blk_table_ptr + root->single_indirect;
		for(int j=0;j<INDIRECT_BLOCK_PTR_NUM; j++) {
			if(single_indir->idx[j] == NULL)
				break;
			tmp_data_blk = data_blk_table_ptr + single_indir->idx[j];
			tmp_entry = (struct directory_entry* ) tmp_data_blk;
			if( strcmp(tmp_entry->name, new_path) == 0 && tmp_entry->inode_num != 0 ) {	// found the file/directory by the path parameter
				printf("found the file: %s\n", tmp_entry->name);
				if(tmp_entry->file_type == REGULAR) {		
					tmp_entry->inode_num = 0;
					return 0;
				}
				else
					return -1;
			}
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
	// iterate through direct blk ptr
	for(int i=0; i<DIRECT_BLOCK_PTR_NUM; i++) {
		tmp_data_blk = root->direct_blk_ptr[i];
		if(root->direct[i]!=-1){
			tmp_data_blk = data_blk_table_ptr + root->direct[i];
			tmp_entry = (struct directory_entry* ) tmp_data_blk;
			if( strcmp(tmp_entry->name, new_path) == 0 && tmp_entry->inode_num != 0 ) {	// found the file/directory by the path parameter
				printf("found the directory: %s\n", tmp_entry->name);
				if(tmp_entry->file_type == DIRECTORY) {		
					tmp_entry->inode_num = 0;
					return 0;
				}
				else
					return -1;
			}
		}
	}
	// iterate through single indirect blk ptr
	if(root->single_indirect!=-1) {
		struct indirect_data_block* single_indir =  (struct indirect_data_block* ) data_blk_table_ptr + root->single_indirect;
		for(int j=0;j<INDIRECT_BLOCK_PTR_NUM; j++) {
			if(single_indir->idx[j] == NULL)
				break;
			tmp_data_blk = data_blk_table_ptr + single_indir->idx[j];
			tmp_entry = (struct directory_entry* ) tmp_data_blk;
			if( strcmp(tmp_entry->name, new_path) == 0 && tmp_entry->inode_num != 0 ) {	// found the file/directory by the path parameter
				printf("found the directory: %s\n", tmp_entry->name);
				if(tmp_entry->file_type == DIRECTORY) {		
					tmp_entry->inode_num = 0;
					return 0;
				}
				else
					return -1;
			}
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
		// iterate through direct blk ptr
		for(int i=0; i<DIRECT_BLOCK_PTR_NUM; i++) {
			if(root->direct[i]!=-1){
				tmp_data_blk = data_blk_table_ptr + root->direct[i];
				tmp_entry = (struct directory_entry* ) tmp_data_blk;
				if( strcmp(tmp_entry->name, new_path) == 0 && tmp_entry->inode_num != 0 ) {	// found the file/directory by the path parameter
					printf("found the file: %s at idx %d\n", tmp_entry->name, root->direct[i]);
					struct	inode* tmp_found = inode_table_ptr + (tmp_entry->inode_num);
					tmp_found->atime = return_current();
					tmp_found->ctime = return_current();
					tmp_found->mtime = return_current();
					return 0;
				}
			}
		}
		// iterate through single indirect blk ptr
		if(root->single_indirect!=-1) {
			struct indirect_data_block* single_indir =  (struct indirect_data_block* ) data_blk_table_ptr + root->single_indirect;
			for(int j=0;j<INDIRECT_BLOCK_PTR_NUM; j++) {
				if(single_indir->idx[j] == NULL)
					break;
				tmp_data_blk = data_blk_table_ptr + single_indir->idx[j];
				tmp_entry = (struct directory_entry* ) tmp_data_blk;
				if( strcmp(tmp_entry->name, new_path) == 0 && tmp_entry->inode_num != 0 ) {	// found the file/directory by the path parameter
					printf("found the file: %s at blk idx %d\n", tmp_entry->name, single_indir->idx[j]);
					struct	inode* tmp_found = inode_table_ptr + (tmp_entry->inode_num);
					tmp_found->atime = return_current();
					tmp_found->ctime = return_current();
					tmp_found->mtime = return_current();
					return 0;
				}
			}
		}
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

	// 4-1. initialize the inode table of ISFS
	init_inode_table_ptr();

	// 4-2. initialize the data block table of ISFS
	init_data_blk_table_ptr();

	// 5. Creates the /root directory
	create_rootdir();

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
	memset(inode_table, -1, sizeof(struct inode)*INODE_COUNT);
	inode_table_ptr = inode_table;
	if (inode_table_ptr == NULL) { 
		printf("Failed to initialize inode table pointer.\n"); 
		exit(0); 
	}
	printf("Successfully initialize inode table pointer.\n");
	return 0;
}

static int init_data_blk_table_ptr()
{
	data_blk_table_ptr = (struct data_block*) calloc(BLK_COUNT, sizeof(struct data_block));
	if (data_blk_table_ptr == NULL) { 
		printf("Failed to initialize data block table pointer.\n"); 
		exit(0); 
	}
	printf("Successfully initialize data block table pointer.\n");
	return 0;
}

static int create_rootdir()
{
	/* use inode table pointer, find one inode and points to the data block */
	struct inode* cur_inode_ptr = inode_table_ptr+i_bitmap_cur_idx;
	
	/* assign data blocks to this node */
	cur_inode_ptr->direct[0] = blk_bitmap_cur_idx++;
	struct data_block* tmp_data_blk = data_blk_table_ptr + (cur_inode_ptr->direct[0]);

	/* add . directory entry in directory entry table of rootdir */
	struct directory_entry* dir_entry_table_ptr = (struct directory_entry*) tmp_data_blk;
	dir_entry_table_ptr->inode_num = super_ptr->root_dir_inode_num;
	dir_entry_table_ptr->file_type = DIRECTORY;
	strncpy(dir_entry_table_ptr->name, ".", DIR_ENTRY_NAME_LEN);

	cur_inode_ptr->direct[1] = blk_bitmap_cur_idx++;
	struct data_block* tmp_data_blk_2 = data_blk_table_ptr + (cur_inode_ptr->direct[1]);

	/* add .. directory entry in directory entry table of rootdir */
	struct directory_entry* dir_entry_table_ptr_2 = (struct directory_entry*) tmp_data_blk_2;
	dir_entry_table_ptr_2->inode_num = super_ptr->root_dir_inode_num;
	dir_entry_table_ptr_2->file_type = DIRECTORY;
	strncpy(dir_entry_table_ptr_2->name, "..", DIR_ENTRY_NAME_LEN);

	cur_inode_ptr->atime = return_current();
	cur_inode_ptr->ctime = return_current();
	cur_inode_ptr->mtime = return_current();

	printf("data block: %ld\n", sizeof(struct data_block));
	printf("dir entry: %ld\n", sizeof(struct directory_entry));
	printf("indirect_data_block entry: %ld\n", sizeof(struct indirect_data_block));
	struct directory_entry* dir_ptr = (struct directory_entry*) data_blk_table_ptr + (cur_inode_ptr->direct[0]);
	printf("data block table idx addr: %d\n", cur_inode_ptr->direct[0]);
	printf("data_block_table_ptr addr: %p\n", data_blk_table_ptr + (cur_inode_ptr->direct[0]) );
	printf("dir_ptr addr: %p\n", dir_ptr);
	printf("dir_ptr inode num: %d\n", dir_ptr->inode_num);
	printf("dir_ptr filetype: %d\n", dir_ptr->file_type);
	printf("dir_ptr name: %s\n", dir_ptr->name);
	dir_ptr = (struct directory_entry*) data_blk_table_ptr + (cur_inode_ptr->direct[1]);
	printf("data block table idx addr: %d\n", cur_inode_ptr->direct[1]);
	printf("data_block_table_ptr addr: %p\n", data_blk_table_ptr + (cur_inode_ptr->direct[1]) );
	printf("dir_ptr addr: %p\n", dir_ptr);
	printf("dir_ptr inode num: %d\n", dir_ptr->inode_num);
	printf("dir_ptr filetype: %d\n", dir_ptr->file_type);
	printf("dir_ptr name: %s\n", dir_ptr->name);
	dir_ptr = (struct directory_entry*) data_blk_table_ptr + cur_inode_ptr->direct[2];
	printf("direct[2]: %d\n", cur_inode_ptr->direct[2]);
	printf("single_indirect: %d\n", cur_inode_ptr->single_indirect);
	// struct indirect_data_block* indir  = (struct indirect_data_block*) data_blk_table_ptr + cur_inode_ptr->single_indirect;
	struct indirect_data_block* indir  = (struct indirect_data_block*) data_blk_table_ptr + 5;
	// printf("indir->idx[0]: %d\n", indir->idx[0]);
	if(indir->idx[0] == NULL)
		printf("indir->idx[0] is NULL\n");
	else
		printf("indir->idx[0]: %d\n", indir->idx[0]);

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