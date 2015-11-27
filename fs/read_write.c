#include "testfs.h"
#include "list.h"
#include "super.h"
#include "block.h"
#include "inode.h"

#define NR_MAX_BLOCK NR_DIRECT_BLOCKS+NR_INDIRECT_BLOCKS+NR_INDIRECT_BLOCKS*NR_INDIRECT_BLOCKS
#define ADDR_SIZE (BLOCK_SIZE / 4)
/* given logical block number, read the corresponding physical block into block.
 * return physical block number.
 * returns 0 if physical block does not exist.
 * returns negative value on other errors. */
static int
testfs_read_block(struct inode *in, int log_block_nr, char *block)
{
    if(log_block_nr > NR_MAX_BLOCK)
        return EFBIG;
	int phy_block_nr = 0;
	assert(log_block_nr >= 0);
	if (log_block_nr < NR_DIRECT_BLOCKS) {
		phy_block_nr = (int)in->in.i_block_nr[log_block_nr];
	} else {
		log_block_nr -= NR_DIRECT_BLOCKS;
        // for doule indirect blocks
		if (log_block_nr >= NR_INDIRECT_BLOCKS){
            log_block_nr -= NR_INDIRECT_BLOCKS;
            int offset = log_block_nr % ADDR_SIZE;
            int pg_num = log_block_nr / ADDR_SIZE;
            if(in->in.i_dindirect > 0){
                read_blocks(in->sb, block, in->in.i_dindirect, 1);
                phy_block_nr = ((int* )block)[pg_num];
            }
            else{
                bzero(block,BLOCK_SIZE);
                return phy_block_nr;
            }
            if(phy_block_nr > 0){
                read_blocks(in->sb, block, phy_block_nr, 1);
                phy_block_nr = ((int* )block)[offset];
            }
            else{
                bzero(block,BLOCK_SIZE);
                return phy_block_nr;
            }
            if(phy_block_nr > 0){
                read_blocks(in->sb, block, phy_block_nr, 1);
            }
            else{
                bzero(block, BLOCK_SIZE);
            }
            return phy_block_nr;
        }
		else if (in->in.i_indirect > 0) {
			read_blocks(in->sb, block, in->in.i_indirect, 1);
			phy_block_nr = ((int *)block)[log_block_nr];
		}
	}
	if (phy_block_nr > 0) {
		read_blocks(in->sb, block, phy_block_nr, 1);
	} else {
		/* we support sparse files by zeroing out a block that is not
		 * allocated on disk. */
		bzero(block, BLOCK_SIZE);
	}
	return phy_block_nr;
}

int
testfs_read_data(struct inode *in, char *buf, off_t start, size_t size)
{
	char block[BLOCK_SIZE];
	long block_nr = start / BLOCK_SIZE;
	long block_ix = start % BLOCK_SIZE;
    if(block_nr >= NR_MAX_BLOCK)
        return EFBIG;
	int ret;
    int o_size = size;

	assert(buf);
	if (start + (off_t) size > in->in.i_size) {
		size = in->in.i_size - start;
	}
  reread:
	if (block_ix + size > BLOCK_SIZE) {
        if(block_nr >= NR_MAX_BLOCK)
            return EFBIG;
        size_t remain_byte = BLOCK_SIZE - block_ix;
        ret = testfs_read_block(in, block_nr, block);
        if(ret < 0)
            return ret;
        memcpy(buf, block+block_ix, remain_byte);
        size = size - remain_byte;
        block_nr++;
        block_ix = 0;
        buf += remain_byte;
        if(size > BLOCK_SIZE)
            goto reread;
		//TBD();
	}
    if(block_nr >= NR_MAX_BLOCK)
        return EFBIG;
	if ((ret = testfs_read_block(in, block_nr, block)) < 0)
		return ret;
	memcpy(buf, block + block_ix, size);
	/* return the number of bytes read or any error */
	return o_size;
}

/* given logical block number, allocate a new physical block, if it does not
 * exist already, and return the physical block number that is allocated.
 * returns negative value on error. */
static int
testfs_allocate_block(struct inode *in, int log_block_nr, char *block)
{
    if(log_block_nr >= NR_MAX_BLOCK)
        return EFBIG;
	int phy_block_nr;
	char indirect[BLOCK_SIZE];
    int indirect_allocated = 0;
	assert(log_block_nr >= 0);
	phy_block_nr = testfs_read_block(in, log_block_nr, block);

	/* phy_block_nr > 0: block exists, so we don't need to allocate it, 
	   phy_block_nr < 0: some error */
	if (phy_block_nr != 0){
		return phy_block_nr;
    }
	/* allocate a direct block */
	if (log_block_nr < NR_DIRECT_BLOCKS) {
		assert(in->in.i_block_nr[log_block_nr] == 0);
		phy_block_nr = testfs_alloc_block_for_inode(in);
		if (phy_block_nr >= 0) {
			in->in.i_block_nr[log_block_nr] = phy_block_nr;
		}
		return phy_block_nr;
	}

	log_block_nr -= NR_DIRECT_BLOCKS;
    // goes to double indirect blocks
	if (log_block_nr >= NR_INDIRECT_BLOCKS){
        // printf("new double block\n");
        log_block_nr-=NR_INDIRECT_BLOCKS;
        //printf("allocated double indirect block nr: %d\n",log_block_nr);
        int dindirect_allocated = 0;
        int dindirect_newpage = 0;
        int dp2_block_nr = 0;
        int dp1_block_nr = 0;
        char dp2_index[BLOCK_SIZE];
        char dp1_index[BLOCK_SIZE];
        bzero(dp1_index,BLOCK_SIZE);
        bzero(dp2_index,BLOCK_SIZE);
        if(in->in.i_dindirect == 0){ /* allocate an dindirect block*/
            //printf("new table\n");
            dp2_block_nr = testfs_alloc_block_for_inode(in);
            if(dp2_block_nr < 0)
                return dp2_block_nr;
            dindirect_allocated = 1;
            in->in.i_dindirect = dp2_block_nr;
        }
        else{
            // printf("old table\n");
            read_blocks(in->sb,dp2_index, in->in.i_dindirect,1);
            dp2_block_nr = in->in.i_dindirect;
        } /* read the indirect block from the system */
        //printf("Block Size %d\n",BLOCK_SIZE);
        int offset = log_block_nr % ADDR_SIZE;
        //printf("%d\n",offset);
        int pg_num = log_block_nr / ADDR_SIZE;
        //printf("%d\n",pg_num);
        if(((int*)dp2_index)[pg_num] == 0){ // allocated second level pg
            //printf("new page");
            dp1_block_nr = testfs_alloc_block_for_inode(in);
            if(dp1_block_nr < 0)
                return dp1_block_nr;
            dindirect_newpage = 1;
            ((int*)dp2_index)[pg_num] = dp1_block_nr;
        }
        else{
            // printf("old page\n");
            read_blocks(in->sb,dp1_index,((int*)dp2_index)[pg_num],1);
            dp1_block_nr = ((int*)dp2_index)[pg_num];
        }
        // printf("new entry");
        phy_block_nr = testfs_alloc_block_for_inode(in);
        if(phy_block_nr >= 0){
            ((int*)dp1_index)[offset] = phy_block_nr;
            write_blocks(in->sb, dp1_index, dp1_block_nr, 1);
            if(dindirect_newpage)
                write_blocks(in->sb,dp2_index,dp2_block_nr,1);
        }
        else{
            if(dindirect_newpage){
                testfs_free_block_from_inode(in, dp1_block_nr);
                ((int*)dp2_index)[pg_num]=0;
                write_blocks(in->sb,dp2_index,dp2_block_nr,1);
            }
            if(dindirect_allocated){
                testfs_free_block_from_inode(in, dp2_block_nr);
                in->in.i_dindirect = 0;
            }
        }
        return phy_block_nr;
    }

    
	if (in->in.i_indirect == 0) {	/* allocate an indirect block */
		bzero(indirect, BLOCK_SIZE);
		phy_block_nr = testfs_alloc_block_for_inode(in);
		if (phy_block_nr < 0)
			return phy_block_nr;
		indirect_allocated = 1;
		in->in.i_indirect = phy_block_nr;
	} else {	/* read indirect block */
		read_blocks(in->sb, indirect, in->in.i_indirect, 1);
	}

	/* allocate direct block */
	assert(((int *)indirect)[log_block_nr] == 0);	
	phy_block_nr = testfs_alloc_block_for_inode(in);

	if (phy_block_nr >= 0) {
		/* update indirect block */
		((int *)indirect)[log_block_nr] = phy_block_nr;
		write_blocks(in->sb, indirect, in->in.i_indirect, 1);
	} else if (indirect_allocated) {
		/* free the indirect block that was allocated */
		testfs_free_block_from_inode(in, in->in.i_indirect);
	}
	return phy_block_nr;
}

int
testfs_write_data(struct inode *in, const char *buf, off_t start, size_t size)
{
	char block[BLOCK_SIZE];
	long block_nr = start / BLOCK_SIZE;
	long block_ix = start % BLOCK_SIZE;
    if(block_nr >= NR_MAX_BLOCK)
        return EFBIG;
	int ret;
    int o_size = size;
  rewrite:
	if (block_ix + size > BLOCK_SIZE) {
        if(block_nr>= NR_MAX_BLOCK){
            in->in.i_size = BLOCK_SIZE * block_nr;
            in->i_flags |= I_FLAGS_DIRTY;
            return -EFBIG;
        }
        size_t remain_byte = BLOCK_SIZE - block_ix;
        ret = testfs_allocate_block(in, block_nr, block);
        if(ret < 0)
            return ret;
        memcpy(block+block_ix, buf, remain_byte);
        write_blocks(in->sb,block,ret, 1);
        size-=remain_byte;
        block_nr++;
        block_ix = 0;
        buf += remain_byte;
        if(size > BLOCK_SIZE)
            goto rewrite;
		//TBD();
	}
	/* ret is the newly allocated physical block number */
    if(block_nr >= NR_MAX_BLOCK){
        in->in.i_size = BLOCK_SIZE * block_nr;
        in->i_flags |= I_FLAGS_DIRTY;
        return -EFBIG;
    }
	ret = testfs_allocate_block(in, block_nr, block);
	if (ret < 0)
		return ret;
	memcpy(block + block_ix, buf, size);
	write_blocks(in->sb, block, ret, 1);
	/* increment i_size by the number of bytes written. */
	if (size > 0)
		in->in.i_size = MAX(in->in.i_size, start + (off_t)o_size);
	in->i_flags |= I_FLAGS_DIRTY;
	/* return the number of bytes written or any error */
	return o_size;
}

int
testfs_free_blocks(struct inode *in)
{
	int i;
	int e_block_nr;
	/* last block number */
	e_block_nr = DIVROUNDUP(in->in.i_size, BLOCK_SIZE);

	/* remove direct blocks */
	for (i = 0; i < e_block_nr && i < NR_DIRECT_BLOCKS; i++) {
		if (in->in.i_block_nr[i] == 0)
			continue;
		testfs_free_block_from_inode(in, in->in.i_block_nr[i]);
		in->in.i_block_nr[i] = 0;
	}
	e_block_nr -= NR_DIRECT_BLOCKS;

	/* remove indirect blocks */
	if (in->in.i_indirect > 0) {
		char block[BLOCK_SIZE];
		read_blocks(in->sb, block, in->in.i_indirect, 1);
		for (i = 0; i < e_block_nr && i < NR_INDIRECT_BLOCKS; i++) {
			testfs_free_block_from_inode(in, ((int *)block)[i]);
			((int *)block)[i] = 0;
		}
		testfs_free_block_from_inode(in, in->in.i_indirect);
		in->in.i_indirect = 0;
	}

	e_block_nr -= NR_INDIRECT_BLOCKS;
    // remove dindirect blocks 
	if (e_block_nr >= 0) {
		//TBD();
        if(in->in.i_dindirect >0){
            char dp1_index[BLOCK_SIZE];
            read_blocks(in->sb, dp1_index, in->in.i_dindirect, 1);
            int j;
            for(j = 0; j < NR_INDIRECT_BLOCKS; j++){
                if (((int*)dp1_index)[j] == 0)
                    continue;
                int k;
                char dp2_index[BLOCK_SIZE];
                read_blocks(in->sb,dp2_index,((int*)dp1_index)[j],1);
                for(k = 0; k < NR_INDIRECT_BLOCKS; k++){
                    if(((int*)dp2_index)[k] ==0)
                        continue;
                    testfs_free_block_from_inode(in, ((int*)dp2_index)[k]);
                    ((int*)dp2_index)[k]=0;
                }
                testfs_free_block_from_inode(in,((int*)dp1_index)[j]);
                ((int*)dp1_index)[j] = 0;
            }
            testfs_free_block_from_inode(in, in->in.i_dindirect);
            in->in.i_dindirect = 0;
        }
	}

	in->in.i_size = 0;
	in->i_flags |= I_FLAGS_DIRTY;
	return 0;
}
