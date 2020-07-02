CE6154 System Component Design for Emerging Memory and Storage Technologies final project: 
In-storage Filesystem (ISFS) - Persist both metadata and file data into a USB drive

fundamental requirement
- all file data can still be accessed after re-mounting ISFS.
- the support of cd, ls, mkdir, rm, rmdir, cat, touch, echo "string" >> file
- the support of "big file" and "big directory"

advanced requirement
- the support of Buffer Cache (including LRU eviction policy, dirty blocks writeback, show the numbers of reads/writes are reduced after using Buffer Cache)
- Buffer Cache optimization (method to reduce search times)

parameter settings
- 1 block size = 512 bytes
- 1 page = 4096 bytes = 8 blocks

- superblock = 1 page = 8 blocks
- block bitmap = 48 pages = 384 blocks
- inode bitmap = 48 pages = 384 blocks
- inode table = 12288 pages = 98304 blocks
- data blocks = 19608 pages = 156864 blocks

- total requires = 208993 pages = 856035328 bytes

steps to mount a USB device with EXT2 filesystem
- mkdir ~/mnt
- mkdir ~/mnt/usb32g
- edit /etc/fstab   (fstab is your OS's file system table)
- add new line "UUID=527420a1-bd6e-4b7c-9599-7b04ebf5ba02 ~/mnt/usb32g ext4 auto,user,rw 0 0" (use sudo blkid to check the UUID)

useful linux commands
- sudo blkid: to check the UUID of USB device
- sudo fdisk -l
- df -Th
- mount -t /dev/sdg ~/mnt/usb32g