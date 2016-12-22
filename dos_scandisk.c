#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include "bpb.h"
#include "dos.h"
#include "direntry.h"
#include "fat.h"

//Function taken from provided code
void write_dirent(struct direntry * dirent, char * filename,
    uint16_t start_cluster, uint32_t size) {
    char * p, * p2;
    char * uppername;
    int len, i;
    memset(dirent, 0, sizeof(struct direntry));
    uppername = strdup(filename);
    p2 = uppername;
    for (i = 0; i < strlen(filename); i++) {
        if (p2[i] == '/' || p2[i] == '\\') {
            uppername = p2 + i + 1;
        }
    }
    for (i = 0; i < strlen(uppername); i++) {
        uppername[i] = toupper(uppername[i]);
    }
    memset(dirent -> deName, ' ', 8);
    p = strchr(uppername, '.');
    memcpy(dirent -> deExtension, "___", 3);
    if (p == NULL) {
        fprintf(stderr, "No filename extension given - defaulting to .___\n");
    } else { *
        p = '\0';
        p++;
        len = strlen(p);
        if (len > 3) len = 3;
        memcpy(dirent -> deExtension, p, len);
    }
    if (strlen(uppername) > 8) {
        uppername[8] = '\0';
    }
    memcpy(dirent -> deName, uppername, strlen(uppername));
    free(p2);
    dirent -> deAttributes = ATTR_NORMAL;
    putushort(dirent -> deStartCluster, start_cluster);
    putulong(dirent -> deFileSize, size);
}
//Function taken from provided code
void create_dirent(struct direntry * dirent, char * filename,
    uint16_t start_cluster, uint32_t size,
    uint8_t * image_buf, struct bpb33 * bpb) {
    while (1) {
        if (dirent -> deName[0] == SLOT_EMPTY) {
            write_dirent(dirent, filename, start_cluster, size);
            dirent++;
            memset((uint8_t * ) dirent, 0, sizeof(struct direntry));
            dirent -> deName[0] = SLOT_EMPTY;
            return;
        }
        if (dirent -> deName[0] == SLOT_DELETED) {
            write_dirent(dirent, filename, start_cluster, size);
            return;
        }
        dirent++;
    }
}

//This function returns the length from given cluster to the end of the file
int getLength(uint16_t cluster, uint8_t *image_buf, struct bpb33* bpb){
    cluster = get_fat_entry(cluster, image_buf, bpb);
    if (is_end_of_file(cluster)){
        return 1;
    }
    return 1 + getLength(cluster,image_buf,bpb);
}

//This function creates a dirent from the given size, cluster and filename
void createFile(char *filename,uint8_t *image_buf,struct bpb33* bpb, int index, int clusterSize){
    uint16_t size = getLength(index, image_buf, bpb);
    printf("Lost File: %d %u\n", index, size);
    struct direntry *dirent = (struct direntry*) cluster_to_addr(0, image_buf, bpb);
    create_dirent(dirent,filename,index,size*clusterSize,image_buf,bpb);   
}
//Function gets the filename. Taken for provided code
void getName(char *name, char *extension, struct direntry * dirent) {
    int i;
    name[8] = ' ';
    extension[3] = ' ';
    memcpy(name, & (dirent -> deName[0]), 8);
    memcpy(extension, dirent -> deExtension, 3);
    for (i = 8; i > 0; i--) {
        if (name[i] == ' ')
            name[i] = '\0';
        else
            break;
    }
    for (i = 3; i > 0; i--) {
        if (extension[i] == ' ')
            extension[i] = '\0';
        else
            break;
    }
}

//This function checks if the cluster in the clusters array is free and cluster in the FAT is used
int checkIfUnreferenced(int cluster,uint16_t clust){
    if (cluster == 0){
        if (clust != CLUST_FREE){
            return 1;
        }
    }
    return 0;
}

//Function taken from doc.c and modified
void follow_dir(uint16_t cluster, uint8_t * image_buf, struct bpb33 * bpb, int clusters[]) {
    clusters[cluster] = 1;
    struct direntry * dirent;
    int d;
    dirent = (struct direntry * ) cluster_to_addr(cluster, image_buf, bpb);
    while (1) {
        for (d = 0; d < bpb -> bpbBytesPerSec * bpb -> bpbSecPerClust; d += sizeof(struct direntry)) {
            char name[9],extension[4];
            uint16_t file_cluster;
            getName(name,extension,dirent);
            if (name[0] == SLOT_EMPTY)
                return;
            if (((uint8_t) name[0]) == SLOT_DELETED)
                continue;
            if (strcmp(name, ".") == 0) {
                dirent++;
                continue;
            }
            if (strcmp(name, "..") == 0) {
                dirent++;
                continue;
            }
            if ((dirent -> deAttributes & ATTR_VOLUME) != 0) {
            } else if ((dirent -> deAttributes & ATTR_DIRECTORY) != 0) {
                file_cluster = getushort(dirent -> deStartCluster);
                follow_dir(file_cluster, image_buf, bpb, clusters);
            } else {
                file_cluster = getushort(dirent->deStartCluster);
                //The current cluster is set to used in the array
                clusters[file_cluster] = 1;
                //The rest of the file is set to used in the array
                while(!is_end_of_file(file_cluster)){
                    file_cluster = get_fat_entry(file_cluster, image_buf, bpb);
                    clusters[file_cluster] = 1;
                }
            }
            dirent++;
        }
        if (cluster == 0) {
            dirent++;
        } else {
            cluster = get_fat_entry(cluster, image_buf, bpb);
            dirent = (struct direntry * ) cluster_to_addr(cluster,image_buf, bpb);
        }
    }
}

void scanfForUnreferencedClusters(uint8_t *image_buf,struct bpb33* bpb,int clusters[]){
    int i;
    int size = bpb->bpbSectors/bpb->bpbSecPerClust;
    int clusterSize = bpb->bpbSecPerClust * bpb->bpbBytesPerSec;
    //Calls the follow_dir function to traverse the clusters and set the used cluster in the clusters array
    follow_dir(0,image_buf,bpb,clusters);
    int printError = 1; //variable to print "Unreferenced" once at start"
    for (i = 2; i < size; i++){
        if (checkIfUnreferenced(clusters[i],get_fat_entry(i,image_buf,bpb))){ //Checks if there is an inconsistency
            if (printError){
                printf("Unreferenced: ");
                printError = 0;
            }
            printf("%d ",i);
        }
    }
    printf("\n");
    int filecount = 1; //lost files count to print lost file number in filename
    for (i = 2; i < size; i++){
        if (checkIfUnreferenced(clusters[i],get_fat_entry(i,image_buf,bpb))){//Checks if there is an inconsistency
            char filename[11];
            sprintf(filename,"found%d.dat",filecount);
            createFile(filename,image_buf,bpb,i,clusterSize); //Creates and writes the lost file into filesystem image
            follow_dir(0,image_buf,bpb,clusters); //Updates the clusters array 
            filecount++;
        }
    }
}

//Function taken from provided code and modified
void scanForWrongFileLength(uint16_t cluster, uint8_t * image_buf, struct bpb33 * bpb) {
    struct direntry * dirent;
    int d;
    int clust_size = bpb->bpbBytesPerSec * bpb->bpbSecPerClust;
    dirent = (struct direntry * ) cluster_to_addr(cluster, image_buf, bpb);
    while (1) {
        for (d = 0; d < clust_size; d += sizeof(struct direntry)) {
            char name[9],extension[4];
            uint16_t file_cluster,start,end;
            uint32_t size;
            getName(name,extension,dirent);
            if (name[0] == SLOT_EMPTY)
                return;
            if (((uint8_t) name[0]) == SLOT_DELETED)
                continue;
            if (strcmp(name, ".") == 0) {
                dirent++;
                continue;
            }
            if (strcmp(name, "..") == 0) {
                dirent++;
                continue;
            }
            if ((dirent -> deAttributes & ATTR_VOLUME) != 0) {
            } else if ((dirent -> deAttributes & ATTR_DIRECTORY) != 0) {
                file_cluster = getushort(dirent -> deStartCluster);
                //If the dirent is a directory then calls the current function to retrieve its files
                scanForWrongFileLength(file_cluster, image_buf, bpb);
            } else {
                size = getulong(dirent->deFileSize); //Cluster size
                file_cluster = getushort(dirent -> deStartCluster);// Cluster
                int clusterLength = getLength(file_cluster,image_buf,bpb); //Cluster length
                if (abs(size/clust_size - clusterLength) > 1){ //Checks if the cluster length is not same as the file size from dirent
                    printf("%s.%s %u %u\n", name, extension, size,clusterLength*clust_size);
                    start = 1+size/clust_size;
                    end = clusterLength+file_cluster;
                    //While loop frees all the cluster from start to end
                    while (start != end && !is_end_of_file(start)){
                        uint16_t  next_cluster = get_fat_entry(start, image_buf, bpb);
                        set_fat_entry(start, FAT12_MASK&CLUST_FREE, image_buf, bpb);
                        start = next_cluster;
                    }
                    //Sets the start_cluster to start of cluster 
                    set_fat_entry(1+size/clust_size, FAT12_MASK&CLUST_EOFS, image_buf, bpb);
                }
            }
            dirent++;
        }
        if (cluster == 0) {
            dirent++;
        } else {
            cluster = get_fat_entry(cluster, image_buf, bpb);
            dirent = (struct direntry * ) cluster_to_addr(cluster,image_buf, bpb);
        }
    }
}

void scandisk(char *filename){
	int file;
	uint8_t *image_buf = mmap_file(filename,&file);
	struct bpb33* bpb = check_bootsector(image_buf);
	int size = bpb->bpbSectors/bpb->bpbSecPerClust;
    int clusters[size]; //Array is intiliased with the number of clusters
    scanfForUnreferencedClusters(image_buf,bpb,clusters); //Checks for unreferenced clusters
    scanForWrongFileLength(0,image_buf,bpb); //Checks for wrong file length
    close(file);
    exit(0);
}

int main(int argc, char *argv[]){
	if (argc != 2){
		printf("Usage: dos_ls <imagename>\n");
	} else {
		scandisk(argv[1]);
	}
	return 0;
}