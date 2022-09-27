#include "filesys/fat.h"
#include "devices/disk.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <stdio.h>
#include <string.h>

/* Should be less than DISK_SECTOR_SIZE */
struct fat_boot {
	unsigned int magic;
	unsigned int sectors_per_cluster; /* Fixed to 1 */
	unsigned int total_sectors;
	unsigned int fat_start;
	unsigned int fat_sectors; /* Size of FAT in sectors. */
	unsigned int root_dir_cluster;
};

/* FAT FS */
struct fat_fs {
	struct fat_boot bs;
	unsigned int *fat;
	unsigned int fat_length;
	disk_sector_t data_start;
	cluster_t last_clst;
	struct lock write_lock;
};

static struct fat_fs *fat_fs;

void fat_boot_create (void);
void fat_fs_init (void);

void
fat_init (void) {
	fat_fs = calloc (1, sizeof (struct fat_fs));
	if (fat_fs == NULL)
		PANIC ("FAT init failed");

	// Read boot sector from the disk
	unsigned int *bounce = malloc (DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC ("FAT init failed");
	disk_read (filesys_disk, FAT_BOOT_SECTOR, bounce);
	memcpy (&fat_fs->bs, bounce, sizeof (fat_fs->bs));
	free (bounce);

	// Extract FAT info
	if (fat_fs->bs.magic != FAT_MAGIC)
		fat_boot_create ();
	fat_fs_init ();
}

void
fat_open (void) {
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT load failed");

	// Load FAT directly from the disk
	uint8_t *buffer = (uint8_t *) fat_fs->fat;
	off_t bytes_read = 0;
	off_t bytes_left = sizeof (fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof (cluster_t);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++) {
		bytes_left = fat_size_in_bytes - bytes_read;
		if (bytes_left >= DISK_SECTOR_SIZE) {
			disk_read (filesys_disk, fat_fs->bs.fat_start + i,
			           buffer + bytes_read);
			bytes_read += DISK_SECTOR_SIZE;
		} else {
			uint8_t *bounce = malloc (DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC ("FAT load failed");
			disk_read (filesys_disk, fat_fs->bs.fat_start + i, bounce);
			memcpy (buffer + bytes_read, bounce, bytes_left);
			bytes_read += bytes_left;
			free (bounce);
		}
	}
}

void
fat_close (void) {
	// Write FAT boot sector
	uint8_t *bounce = calloc (1, DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC ("FAT close failed");
	memcpy (bounce, &fat_fs->bs, sizeof (fat_fs->bs));
	disk_write (filesys_disk, FAT_BOOT_SECTOR, bounce);
	free (bounce);

	// Write FAT directly to the disk
	uint8_t *buffer = (uint8_t *) fat_fs->fat;
	off_t bytes_wrote = 0;
	off_t bytes_left = sizeof (fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof (cluster_t);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++) {
		bytes_left = fat_size_in_bytes - bytes_wrote;
		if (bytes_left >= DISK_SECTOR_SIZE) {
			disk_write (filesys_disk, fat_fs->bs.fat_start + i,
			            buffer + bytes_wrote);
			bytes_wrote += DISK_SECTOR_SIZE;
		} else {
			bounce = calloc (1, DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC ("FAT close failed");
			memcpy (bounce, buffer + bytes_wrote, bytes_left);
			disk_write (filesys_disk, fat_fs->bs.fat_start + i, bounce);
			bytes_wrote += bytes_left;
			free (bounce);
		}
	}
}

void
fat_create (void) {
	// Create FAT boot
	fat_boot_create ();
	fat_fs_init ();

	// Create FAT table
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT creation failed");

	// Set up ROOT_DIR_CLST
	fat_put (ROOT_DIR_CLUSTER, EOChain);

	// Fill up ROOT_DIR_CLUSTER region with 0
	uint8_t *buf = calloc (1, DISK_SECTOR_SIZE);
	if (buf == NULL)
		PANIC ("FAT create failed due to OOM");
	disk_write (filesys_disk, cluster_to_sector (ROOT_DIR_CLUSTER), buf);
	free (buf);
}

void
fat_boot_create (void) {
	unsigned int fat_sectors =
	    (disk_size (filesys_disk) - 1)
	    / (DISK_SECTOR_SIZE / sizeof (cluster_t) * SECTORS_PER_CLUSTER + 1) + 1;
	fat_fs->bs = (struct fat_boot){
	    .magic = FAT_MAGIC,
	    .sectors_per_cluster = SECTORS_PER_CLUSTER,
	    .total_sectors = disk_size (filesys_disk),
	    .fat_start = 1,
	    .fat_sectors = fat_sectors,
	    .root_dir_cluster = ROOT_DIR_CLUSTER,
	};
}

void
fat_fs_init (void) {
	/* TODO: Your code goes here. */\
	// FAT 파일시스템을 초기화
	// fat_bs->dp 저장된 일부값을 이용할 수 있음, 다른 초기화도 가능
	// todo: fat_length와 data_start 필드를 초기화
	// todo: fat_length : 클러스터 수
	// todo: data_start : 파일저장을 시작할 수 있는 섹터
	
	fat_fs->fat_length = disk_size(filesys_disk) - fat_fs->bs.fat_sectors -1;
	fat_fs->data_start = (disk_sector_t)(fat_fs->bs.fat_start + fat_fs->bs.fat_sectors);
	fat_fs->last_clst = (cluster_t)(fat_fs->bs.root_dir_cluster +1);
	lock_init(&fat_fs->write_lock);


	
}

/*----------------------------------------------------------------------------*/
/* FAT handling            
 * FAT는 sector상에서 첫번째 데이터의 섹터번호에 두번째 데이터의 섹터번호가 저장되고
 * 같은 방법으로 마지막 데이터의 섹터번호 인덱스에 -1(EOChain)이 나올때까지 반복되는 방식
 * 애초에 첫데이터는 없고 inode에서 찾은 다음 그다음 데이터에 대해서만 가지고 있음
 ? create이나 remove는 FAT 에 추가하고 지우는 거 같고..?
 ? put은 같은 파일의 순서나 체인이 바뀌는 건가봄?
 ? get은 다음꺼 찾는거 같고..
 ? to_sector는 섹터번호 가져오는 거?
   */
/*----------------------------------------------------------------------------*/

/* Add a cluster to the chain.
 * If CLST is 0, start a new chain.
 * Returns 0 if fails to allocate a new cluster. */
cluster_t
fat_create_chain (cluster_t clst) {
	/* TODO: Your code goes here. */
	// clst에 지정된 클러스터 뒤에 클러스터를 추가해서 체인을 확장
	// todo: 클러스터를 관리하는 리스트에 추가
	// todo: 새로 할당된 클러스터 번호를 반환
	if(clst==0){
		// todo: 0이면 새 체인 시작
	}


}

/* Remove the chain of clusters starting from CLST.
 * If PCLST is 0, assume CLST as the start of the chain. */
void
fat_remove_chain (cluster_t clst, cluster_t pclst) {
	/* TODO: Your code goes here. */
	// clst에서 시작해서 체인에서 클러스터를 제거
	// todo: pclst가 바로 직전 클러스터인지 확인해야하나?
	// ? pclst가 업데이트된 마지막 요소여야함 -> ???
	// todo: 리스트에서 제거

}

/* Update a value in the FAT table. */
void
fat_put (cluster_t clst, cluster_t val) {
	/* TODO: Your code goes here. */
	// clst 클러스터번호가 가리키는 FAT를 val로 업데이트
	// FAT의 항목은 다음 클러스터를 가리킴, 연결을 업데이트
	// todo: FAT의 clst의 값을 val로 바꿈
	// todo: 그럼 원래거는 지워야함?
	
}

/* Fetch a value in the FAT table. */
cluster_t
fat_get (cluster_t clst) {
	/* TODO: Your code goes here. */
	// 주어진 clst가 가리키는 클러스터 번호를 반환
	// todo: FAT 테이블에서 clst가 가리키는 다음 번호를 가져오는거 같음
}

/* Covert a cluster # to a sector number. */
disk_sector_t
cluster_to_sector (cluster_t clst) {
	/* TODO: Your code goes here. */
	// 클러스터 번호 clst를 해당 섹터번호로 바꾸고 섹터번호를 반환
	// todo: 클러스터 번호가 가리키는?
}
