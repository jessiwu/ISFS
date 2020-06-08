#define FUSE_USE_VERSION 30

#include <fuse.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

// ... //

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
	// st->st_atime = time( NULL ); // The last "a"ccess of the file/directory is right now
	// st->st_mtime = time( NULL ); // The last "m"odification of the file/directory is right now (file contents)
	// st->st_ctime = time( NULL ); // The last "c"hange of the the file/directory is right now (ownership, permission, metadata)
	
	if ( strcmp( path, "/" ) == 0 ) // || is_dir( path ) == 1 )
	{
		st->st_mode = S_IFDIR | 0755;
		st->st_nlink = 2; // Why "two" hardlinks instead of "one"? The answer is here: http://unix.stackexchange.com/a/101536
	}
	else if ( is_dir( path) ==1 ) {
		int dir_idx = get_dir_index(path);
		st->st_mode = S_IFDIR | 0755;
		st->st_nlink = 2; // Why "two" hardlinks instead of "one"? The answer is here: http://unix.stackexchange.com/a/101536
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
		for ( int curr_idx = 0; curr_idx <= curr_dir_idx; curr_idx++ )
			filler( buffer, dir_list[ curr_idx ], NULL, 0 );
	
		for ( int curr_idx = 0; curr_idx <= curr_file_idx; curr_idx++ )
			filler( buffer, files_list[ curr_idx ], NULL, 0 );
	}

	struct timespec current_time;

	clock_gettime(CLOCK_REALTIME, &current_time);
	for ( int curr_idx = 0; curr_idx <= curr_dir_idx; curr_idx++ )
		dir_atime_list[curr_idx] = current_time;
	
	for ( int curr_idx = 0; curr_idx <= curr_file_idx; curr_idx++ )
		file_atime_list[curr_idx] = current_time;

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

static int do_open(const char * path, struct fuse_file_info * fi)
{
	if(  is_file(++path)==1 )
		return 0;
	else 
		return -1;
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

static int do_utimens(const char * path, const struct timespec tv[2], struct fuse_file_info *fi)
{
	return 0;
}

static int do_create(const char * path, mode_t mode, struct fuse_file_info * fi)
{
	path++;
	if( is_file(path) == 1) {
		return 0;
	}
	else {
		add_file(path);
		return 0;
	}
}

static struct fuse_operations operations = {
    .getattr	= do_getattr,
    .readdir	= do_readdir,
    .read		= do_read,
    .mkdir		= do_mkdir,
    .mknod		= do_mknod,
	// .open		= do_open,
    .write		= do_write,
	.unlink		= do_unlink,
	.rmdir		= do_rmdir,
	.utimens	= do_utimens,
	// .create		= do_create,
};

int main( int argc, char *argv[] )
{
	return fuse_main( argc, argv, &operations, NULL );
}
