
#include <stdio.h>
#include <fstream>
#include <iostream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>

#include <unistd.h>

static inline size_t fsize(int fd)
{
	struct stat results;
    if (fstat(fd, &results) == 0)
	{
		if(results.st_mode & S_IFREG)
			return results.st_size;
	}
    return 0;
}

/*
int unaligned_int(uintptr_t p, ptrdiff_t offs)
{
	return (*(unsigned char*)(p+offs) << 0) |
		(*(unsigned char*)(p+offs+1) << 8) |
		(*(unsigned char*)(p+offs+2) << 16) |
		(*(unsigned char*)(p+offs+3) << 24);
}
*/

int unaligned_int_msb(uintptr_t p, ptrdiff_t offs)
{
	return (*(unsigned char*)(p+offs) << 24) |
		(*(unsigned char*)(p+offs+1) << 16) |
		(*(unsigned char*)(p+offs+2) << 8) |
		(*(unsigned char*)(p+offs+3) << 0);
}

int fdcreate(const char* name, int nfile, uintptr_t* buf)
{
	int fd = open(name, O_RDWR | O_CREAT);
	
	fchmod(fd, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
	
	ftruncate(fd, nfile);
	*buf = (uintptr_t) mmap(NULL, nfile, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	memset((void*)*buf, 0, nfile);

	return fd;
}

void fdclose(int fd, uintptr_t* buf)
{
	int oldn = fsize(fd);
	munmap((void*) *buf, oldn);
	close(fd);
}




int main(int argc, char** argv)
{
	int ch;
	
	const char* fname = argv[1];
	
	int fd = open(fname, O_RDONLY);
	
	if(fd == -1)
		return -1;
	size_t fd_n = fsize(fd);
	
	//uint64_t pagesize = getpagesize();
	//pagesize = 512;
	//uint64_t npages = (fd_n+pagesize-1)/pagesize;
	
	//fprintf(stdout, "scanning %lx size which has %lx pages of size %lx\n", fd_n, npages, pagesize);
	
	int outfile_no = 0;
	
	intptr_t fbuf = (uintptr_t) mmap(NULL, fd_n, PROT_READ, MAP_SHARED, fd, 0);
	if(fbuf < 0)
	{
		fprintf(stdout, "Unable to open buffer: %d\n", errno);
		return -1;
	}
	
	fbuf = fbuf + 0x8000 * 512;
	
	
	intptr_t vbr = fbuf + 0xc * 512; // 0xc is the second mbr?
	
	uint64_t partoffs = *(uint64_t*)(vbr+64);
	uint32_t fatoffs = *(uint32_t*)(vbr+80);
	uint32_t fatlen = *(uint32_t*)(vbr+84);
	
	uint64_t sec_sz = 1<<*(uint8_t*)(vbr+108);
	
	printf("partition offset = %llx\n", partoffs);
	printf("fat offset = %x\n", fatoffs);
	printf("fat length = %x\n", fatlen);
	printf("bytes/sector = %llu\n", sec_sz);
	
	uint64_t nsectors = *(uint64_t*)(vbr+72);

	printf("sectors = %llu (%llu)\n", nsectors, nsectors * sec_sz);
	
	intptr_t fat = fbuf + fatoffs * sec_sz;
	
	int64_t clus_sz = (1<<*(uint8_t*)(vbr+109))*sec_sz;
	
	printf("cluster size = %llu\n", clus_sz);

	//return -1;
	
	/*
	for(int i=0; i<32; i++)
	{
		for(int j=0; j<16; j++)
		{
			printf("%08x ", *(uint32_t*)(fat+i*16*4+j*4));
		}
		printf("\n");
	}
	return -1;
	*/
	
	
	uint32_t heapoffs = *(uint32_t*)(vbr+88); 
	printf("heap offset = %x\n", heapoffs);
	
	intptr_t heap = fbuf + heapoffs * sec_sz;
	
	
	for(ptrdiff_t i=0; i<nsectors; i++)
	{
		
		
		if(unaligned_int_msb(heap,i*sec_sz+4) == 'ftyp') // ftyp
		{
			//fprintf(stdout, "found something at offset %llx\n", i*pagesize);

			int recovered = 0;
			
			int block_offs = unaligned_int_msb(heap, i*sec_sz+0);
			
			//fprintf(stdout, "first block size: %llx\n", block_offs);
			
			if(unaligned_int_msb(heap,i*sec_sz+block_offs+4) == 'mdat')
			{
				block_offs += unaligned_int_msb(heap,i*sec_sz+block_offs);
				
				//int pluspages = block_offs / pagesize;
				
								
				fprintf(stdout, "second block size: %llx\n", block_offs);

				if(unaligned_int_msb(heap, i*sec_sz+block_offs+4) == 'moov')
				{
					block_offs += unaligned_int_msb(heap,i*sec_sz+block_offs);
					
					fprintf(stdout, "last block size: %llx\n", block_offs);

					char outfname[50];
					
					sprintf(outfname, "file%04d.mp4", outfile_no);
					
					outfile_no++;
					
					fprintf(stdout, "recovering file (size %llx) to %s\n", block_offs, outfname);
					
					uintptr_t obuf;
					int ofp = fdcreate(outfname, block_offs, &obuf);
					memcpy((void*)obuf, (void*)(fbuf+i*sec_sz), block_offs);
					
					fdclose(ofp, &obuf);
					
					
					
					recovered = 1;
					
				}
				else
				{
					fprintf(stdout, "Looking in FAT table for %llx (%llx)\n", i*sec_sz/clus_sz, block_offs/clus_sz);
					
					int myblk = i*sec_sz/clus_sz + 2;
					fprintf(stdout, "%x ", myblk);
					int k;
					for(k=0; k<block_offs/clus_sz; k++)
					{
						myblk = *(uint32_t*)(fat + 4*myblk);
						//fprintf(stdout, "%x ", myblk);
						if(myblk == -1)
							break;
					}
					myblk -= 2;
					
					//fprintf(stdout, "\n");
					
					if(k<block_offs/clus_sz)
					{
						fprintf(stderr, "could not scan far enough! %d/%d\n", k, block_offs/clus_sz);
						continue;
					}
					
					if(unaligned_int_msb(heap, myblk * clus_sz + ((block_offs+i*sec_sz)%clus_sz)+4) == 'moov')
					{
						block_offs += unaligned_int_msb(heap, myblk * clus_sz + ((block_offs+i*sec_sz)%clus_sz)+0);

						char outfname[50];
						sprintf(outfname, "file%04d.mp4", outfile_no);
						outfile_no++;
						fprintf(stdout, "recovering file (size %llx) to %s\n", block_offs, outfname);
						
						
						uintptr_t obuf;
						int ofp = fdcreate(outfname, block_offs, &obuf);
						
						
						// first copy...unaligned
						uint64_t ofptr = 0;
						
						uint64_t copysz = (clus_sz - i*sec_sz)%clus_sz;
						if(copysz==0)
							copysz=clus_sz;
						memcpy((void*)(obuf+ofptr), (void*)(heap+i*sec_sz), copysz);
						ofptr += copysz;
						
						int myblk = i*sec_sz/clus_sz + 2;
						for(int k=0; k <block_offs/clus_sz; k++)
						{
							myblk = *(uint32_t*)(fat + 4*myblk);
							//fprintf(stdout, "%x ", myblk);
							if(myblk == -1)
								break;
							
							int mylocal = myblk - 2;
							copysz = ofptr + clus_sz > block_offs ? block_offs - ofptr : clus_sz;
							memcpy((void*)(obuf+ofptr), (void*)(heap+mylocal * clus_sz), copysz);
							ofptr += copysz;
						}
						
						fdclose(ofp, &obuf);
						recovered = 1;
						
						
						//fprintf(stderr, "Found end!\n");
					}
					else
					{
						fprintf(stderr, "Scanned forward...halp! %x %x\n", myblk*clus_sz, ((block_offs+i*sec_sz)%clus_sz)+4);
					}
					
					//fprintf(stderr, "End...\n");
					//return -1;
					/*
					fprintf(stdout, "%08x %llx %llx\n", unaligned_int_msb(fbuf, i*pagesize+block_offs+4), i*pagesize, block_offs);
					for(int j=0; j<32; j++)
					{
						fprintf(stdout, "%c", *(char*)(fbuf+i*pagesize + block_offs - 4 +j));
					}
					fprintf(stdout, "\n");
					*/
				}
			}
			
			if(!recovered)
			{
				fprintf(stdout, "failed to recover file at offset %llx\n", i*sec_sz);
			}
			else
			{
				fprintf(stdout, "recovered file at offset %llx\n", i*sec_sz);
			}
			
		}
		
	}
	munmap((void*)fbuf, fd_n);
	
	close(fd);
	return 0;
}
