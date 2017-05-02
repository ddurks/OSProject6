//David Durkin and Chris Beaufils
//OS Project 6

#include "fs.h"
#include "disk.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>

#define FS_MAGIC           0xf0f03410
#define INODES_PER_BLOCK   128
#define POINTERS_PER_INODE 5
#define POINTERS_PER_BLOCK 1024

int *bitmap;
int bitmap_size;
int *inode_table;
int num_inodes;
bool is_mounted = false;

struct fs_superblock {
	int magic;
	int nblocks;
	int ninodeblocks;
	int ninodes;
};

struct fs_inode {
	int isvalid;
	int size;
	int direct[POINTERS_PER_INODE];
	int indirect;
};

union fs_block {
	struct fs_superblock super;
	struct fs_inode inode[INODES_PER_BLOCK];
	int pointers[POINTERS_PER_BLOCK];
	char data[DISK_BLOCK_SIZE];
};

// loads data from an inode at a certain number
void inode_load(int inumber, struct fs_inode *inode) {

    union fs_block block;
    disk_read((int) (inumber/INODES_PER_BLOCK) + 1, block.data);

    *inode = block.inode[inumber%INODES_PER_BLOCK];
}

// saves the data from passed inode to the inode at a certain number
void inode_save(int inumber, struct fs_inode *inode) {

    union fs_block block;
    disk_read((int) (inumber/INODES_PER_BLOCK) + 1, block.data);

    block.inode[inumber%INODES_PER_BLOCK] = *inode;
    disk_write((int) (inumber/INODES_PER_BLOCK) + 1, block.data);
}

int fs_format()
{
	union fs_block block;

	disk_read(0, block.data);

	if (is_mounted == true) {
		fprintf(stderr, "Cannot format mounted disk\n");
		return 0;
	};

	// write the super block
	block.super.magic = FS_MAGIC;
	block.super.nblocks = disk_size();
	block.super.ninodes = block.super.ninodeblocks*INODES_PER_BLOCK;
	block.super.ninodeblocks = (int)((block.super.nblocks*0.1)+1);
	disk_write(0, block.data);

	int ninodeblocks = block.super.ninodeblocks;
	int i;
	int j;
	// clear inodes in order to create new filesystem
	for (i=1; i<=ninodeblocks; i++) {

		disk_read(i, block.data);

		for (j=0; j<INODES_PER_BLOCK; j++) {

			block.inode[j].isvalid = 0;
		}

		// write inodes back to disk
		disk_write(i, block.data);
	}
	return 1;
}

void fs_debug()
{
	union fs_block block;

	disk_read(0,block.data);

	printf("superblock:\n");
	printf("    %d blocks\n",block.super.nblocks);
	printf("    %d inode blocks\n",block.super.ninodeblocks);
	printf("    %d inodes\n",block.super.ninodes);

    int ninodeblocks = block.super.ninodeblocks;

    int i, j, l, m;
	// print inode summary
    for (i=1; i<=ninodeblocks; i++) {

        for (j=0; j<INODES_PER_BLOCK; j++) {

            disk_read(i, block.data);

			// if block is valid/not empty
            if (block.inode[j].isvalid) {
                printf("inode %d:\n", j);
                printf("\tsize: %d bytes\n", block.inode[j].size);
                printf("\tdirect blocks:");

				// print direct blocks
                for(l = 0; l < POINTERS_PER_INODE; l++){
                    if(block.inode[j].direct[l]) printf(" %d",block.inode[j].direct[l]);
                }
                printf("\n");

				// print indirect blocks
                if(block.inode[j].indirect){
                    printf("\tindirect block: %d\n", block.inode[j].indirect);
                    printf("\tindirect data blocks:");

					disk_read(block.inode[j].indirect, block.data);

                    for (m=0; m<POINTERS_PER_BLOCK; m++) {
                        if(block.pointers[m]) printf(" %d", block.pointers[m]);
                    }
                    printf("\n");
                }
            }
        }
    }
}

int fs_mount()
{
    if (is_mounted == true) {
		fprintf(stderr, "Disk already mounted\n");
        return 0;
    }

    union fs_block block;

    disk_read(0, block.data);

	int i, j, l, m;
	// initialize and initially fill free block bitmap
    bitmap_size = block.super.nblocks;
    bitmap = (int) malloc(bitmap_size*sizeof(int));

    for (i=0; i<bitmap_size; i++) {
        bitmap[i] = 0;
    }

	// initialize and initially fill inode table
    num_inodes = (block.super.ninodeblocks*INODES_PER_BLOCK);
    inode_table = (int) malloc(num_inodes*sizeof(int));

    for (i=0; i<num_inodes; i++) {
        inode_table[i] = 0;
    }

	// set first item in bitmap to 1 to be used as super block
    bitmap[0] = 1;

    int ninodeblocks = block.super.ninodeblocks;
    for (i=1; i<=ninodeblocks; i++) {
        bitmap[i] = 1;

        for (j=0; j<INODES_PER_BLOCK; j++) {

            disk_read(i, block.data);

            if (block.inode[j].isvalid) {

                inode_table[((i-1)*INODES_PER_BLOCK) + j] = 1;

				// print the direct blocks
                for(l = 0; l < POINTERS_PER_INODE; l++){
                    if(block.inode[j].direct[l]) bitmap[block.inode[j].direct[l]] = 1;
                }

				// print the indirect blocks
                if(block.inode[j].indirect){

                    bitmap[block.inode[j].indirect] = 1;
                    disk_read(block.inode[j].indirect, block.data);

					for (m=0; m<POINTERS_PER_BLOCK; m++) {
                        if(block.pointers[m]) bitmap[block.pointers[m]] = 1;
                    }
                }
            }
        }
    }

    is_mounted = true;
	return 1;
}

int fs_create()
{

    struct fs_inode inode;

    if (is_mounted == false) {
        printf("Disk not yet mounted!\n");
        return 0;
    }

	int i, j;

	// search for an empty node
    for (i=1; i<num_inodes; i++) {

		if (inode_table[i]==0) {
            inode_table[i] = 1;
            inode_load(i, &inode);
            inode.isvalid=1;
            inode.size=0;

			for (j = 0; j < POINTERS_PER_INODE; j++) {
                inode.direct[j] = 0;
            }

            inode.indirect=0;
            inode_save(i, &inode);
            return i;
        }
    }

	// returns 0 if table is full
	return 0;
}

int fs_delete( int inumber )
{

    union fs_block block;
    struct fs_inode inode;

    if (is_mounted == false) {
        printf("Disk not yet mounted!\n");
        return 0;
    }

    disk_read(0, block.data);

    if(inumber > block.super.ninodes || inumber < 0) return 0;

    inode_load(inumber, &inode);

    if (inode.isvalid == 0) return 1;

    inode.isvalid = 0;
    inode.size = 0;

    inode_save(inumber, &inode);

	int i;
	// go thru direct pointers and free them
    for (i=0; i<POINTERS_PER_INODE; i++) {

        if (inode.direct[i]) bitmap[inode.direct[i]] = 0;
    }

	// go thru indirect pointers and free them
    if(inode.indirect){

        disk_read(inode.indirect, block.data);

		for(i = 0; i < POINTERS_PER_BLOCK; i++){
            if(block.pointers[i]) bitmap[block.pointers[i]] = 0;
        }
    }
    return 1;
}

int fs_getsize( int inumber )
{

	if (is_mounted == false) {
        printf("Disk not yet mounted!\n");
        return 0;
    }

    union fs_block block;
    struct fs_inode inode;

    disk_read(0, block.data);

    if(inumber > block.super.ninodes || inumber < 0) return -1;

    inode_load(inumber, &inode);

    if(inode.isvalid) return inode.size;

	return -1;
}

int fs_read( int inumber, char *data, int length, int offset )
{
	if (is_mounted == false) {
        printf("Disk not yet mounted!\n");
        return 0;
    }

    union fs_block block;
    struct fs_inode inode;

    disk_read(0, block.data);

    if(inumber > block.super.ninodes || inumber < 0) return 0;

    num_inodes = (block.super.ninodeblocks*INODES_PER_BLOCK);

    int current_byte = 0, first = 0;
    inode_load(inumber, &inode);

	if (inode.isvalid == 0) return 0;

    if (offset >= inode.size) return 0;

    int startBlock = (int)(offset/DISK_BLOCK_SIZE);
    int curroffset = offset%4096;

	int i, j;
	// direct nodes
    for(i = startBlock; i < POINTERS_PER_INODE; i++){

		if(inode.direct[i]){

			if (first == 0) {
                disk_read(inode.direct[i], block.data);

				for(j = 0; j+curroffset < DISK_BLOCK_SIZE; j++){

					if(block.data[j+curroffset]){

                        data[current_byte] = block.data[j+curroffset];
                        current_byte++;

						if(current_byte+offset >= inode.size) return current_byte;
                    }
                    else{

                        return current_byte;
                    }

					if (current_byte == length) return current_byte;
                }
                first = 1;
            }
            else{
                disk_read(inode.direct[i], block.data);

				for(j = 0; j < DISK_BLOCK_SIZE; j++){

					if(block.data[j]){

                        data[current_byte] = block.data[j];
                        current_byte++;

						if(current_byte+offset >= inode.size) return current_byte;
                    }
                    else{

                        return current_byte;
                    }

					if (current_byte == length) return current_byte;
                }
            }
        }
    }

	// indirect nodes
    union fs_block indirectBlock;
    int startIndirect = startBlock - 5;
    if(inode.indirect){

		disk_read(inode.indirect, indirectBlock.data);

		for(i = startIndirect; i < POINTERS_PER_BLOCK; i++){

			if (indirectBlock.pointers[i]){
                disk_read(indirectBlock.pointers[i], block.data);

				for(j = 0; j < DISK_BLOCK_SIZE; j++){

					if(block.data[j]){

                        data[current_byte] = block.data[j];
                        current_byte++;

						if(current_byte+offset >= inode.size) return current_byte;
                    }
                    else{

                        return current_byte;
                    }

					if (current_byte == length) return current_byte;
                }
            }
        }
    }
    return current_byte;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
	if (is_mounted == false) {
        printf("Disk not yet mounted!\n");
        return 0;
    }

	struct fs_inode inode;
    union fs_block block;

	// read the super blocks and check
    disk_read(0, block.data);

    if(inumber > block.super.ninodes || inumber < 0) return 0;
    num_inodes = (block.super.ninodeblocks*INODES_PER_BLOCK);
    int nonDiskBlocks = (block.super.ninodeblocks+1);

    int current_byte = 0, first = 0;
    inode_load(inumber, &inode);

    if (inode.isvalid == 0) return 0;

	int i, j, k;
    if (inode.isvalid == 1 && inode.size > 0){

		for (i = 0; i < POINTERS_PER_INODE; i++){

            if(inode.direct[i]) bitmap[inode.direct[i]] = 0;
        }

		if(inode.indirect){

            disk_read(inode.indirect, block.data);
            bitmap[inode.indirect] = 0;

			for(j = 0; j < POINTERS_PER_BLOCK; j++){

                if(block.pointers[j]) bitmap[block.pointers[j]] = 0;
            }
        }
    }

    int startBlock = (int)(offset/DISK_BLOCK_SIZE);
    int curroffset = offset%4096;
    for(i = startBlock; i < POINTERS_PER_INODE; i++){

		// go thru bitmap to look for an empty block
		for (k = nonDiskBlocks; k < bitmap_size; k++) {

			if (bitmap[k]==0) {

                inode.direct[i] = k;
                bitmap[k] = 1;
                break;
            }
        }

		if (first == 0) {

			disk_read(inode.direct[i], block.data);

			for(j = 0; j+curroffset < DISK_BLOCK_SIZE; j++){

                block.data[j+curroffset] = data[current_byte];
                current_byte++;

				if (current_byte == length){

                    disk_write(inode.direct[i], block.data);
                    inode.size = current_byte + offset;
                    inode_save(inumber, &inode);
                    return current_byte;
                }

            }

			first = 1;
            disk_write(inode.direct[i], block.data);
        }
        else{

			disk_read(inode.direct[i], block.data);

			for(j = 0; j < DISK_BLOCK_SIZE; j++){

                block.data[j] = data[current_byte];
                current_byte++;

				if (current_byte == length){

                    disk_write(inode.direct[i], block.data);
                    inode.size = current_byte + offset;
                    inode_save(inumber, &inode);
                    return current_byte;
                }
            }

			disk_write(inode.direct[i], block.data);
        }
    }

	// indirect nodes
    union fs_block indirectBlock;
    for (i = nonDiskBlocks; i < bitmap_size; i++) {
		// go thru bitmap to look for empty
		if (!bitmap[i]) {

            inode.indirect = i;
            bitmap[i] = 1;
            break;
        }
    }

    int startIndirect = startBlock - 5;

    if(inode.indirect){

		disk_read(inode.indirect, indirectBlock.data);

		for(i = startIndirect; i < POINTERS_PER_BLOCK; i++){

			// go thru bitmap to look for empty
			for (k = nonDiskBlocks; k < bitmap_size; k++) {

				if (!bitmap[k]) {

                    indirectBlock.pointers[i] = k;
                    bitmap[k] = 1;
                    break;
                }
            }

            disk_read(indirectBlock.pointers[i], block.data);

			for(j = 0; j < DISK_BLOCK_SIZE; j++){
                block.data[j] = data[current_byte];
                current_byte++;

				if (current_byte == length){

                    disk_write(indirectBlock.pointers[i], block.data);
                    inode.size = current_byte + offset;
                    inode_save(inumber, &inode);
                    return current_byte;
                }
            }

			disk_write(indirectBlock.pointers[i], block.data);
        }

		disk_write(inode.indirect, indirectBlock.data);
    }

	// update inode size
    inode.size = current_byte + offset;
    inode_save(inumber, &inode);
    return current_byte;
}
