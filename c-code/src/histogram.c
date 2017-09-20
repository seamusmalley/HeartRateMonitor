#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <math.h>

#include "histogram.h"
#include "error.h"

static int map(int bpm, int min);

int bytes_needed;

int* init_histogram(void){
        char* name;
        int fd;
        int* histogram;
        int result;

        name = "./histo";

        // open the file, creating it if it does not already exist
        fd = open(name, O_RDWR | O_CREAT, 0660);
        BAIL_NEG_ONE(fd);

        // compute size needed
        bytes_needed = (NUM_BUCKETS * (HIGHEST_RATE - LOWEST_RATE) + 4) * sizeof(int) + 1;

        // attempt to stretch file size
        result = lseek(fd, bytes_needed - 1, SEEK_SET);
        BAIL_NEG_ONE(result);

        result = write(fd, "", 1);
        BAIL_NEG_ONE(result);

        // mmap the file
        histogram = mmap(NULL, bytes_needed, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

        if(histogram == MAP_FAILED){
                goto error;
        }

        set_bpm_data_written_bit(histogram, 0);
        set_env_data_written_bit(histogram, 0);
        set_last_env_reading(histogram, -1);
        set_last_bpm_reading(histogram, -1);

        return histogram;

 error: printf("Error initializing histogram!\n");
        return NULL;

}

void set_last_bpm_reading(int* histo, int val){
        int index = NUM_BUCKETS * (HIGHEST_RATE - LOWEST_RATE) + 3;
        histo[index] = val;

        msync(histo, bytes_needed, MS_SYNC);
}

void set_last_env_reading(int* histo, int val){
        int index = NUM_BUCKETS * (HIGHEST_RATE - LOWEST_RATE) + 4;
        histo[index] = val;

        msync(histo, bytes_needed, MS_SYNC);
}

int fetch_last_env_reading(int* histo){
        int index = NUM_BUCKETS * (HIGHEST_RATE - LOWEST_RATE) + 4;
        return histo[index];
}

int fetch_last_bpm_reading(int* histo){
        int index = NUM_BUCKETS * (HIGHEST_RATE - LOWEST_RATE) + 3;
        return histo[index];
}

void set_bpm_data_written_bit(int* histo, int val){
        int index = NUM_BUCKETS * (HIGHEST_RATE - LOWEST_RATE) + 1;
        histo[index] = val;

        msync(histo, bytes_needed, MS_SYNC);
}

int fetch_bpm_data_written_bit(int* histo){
        int index = NUM_BUCKETS * (HIGHEST_RATE - LOWEST_RATE) + 1;
        return histo[index];
}

void set_env_data_written_bit(int* histo, int val){
        int index = NUM_BUCKETS * (HIGHEST_RATE - LOWEST_RATE) + 2;
        histo[index] = val;

        msync(histo, bytes_needed, MS_SYNC);
}

int fetch_env_data_written_bit(int* histo){
        int index = NUM_BUCKETS * (HIGHEST_RATE - LOWEST_RATE) + 2;
        return histo[index];
}

void histogram_reset(int* histo){
        int i;

        for(i = 0; i < NUM_BUCKETS * (HIGHEST_RATE - LOWEST_RATE); ++i){
                histo[i] = 0;
        }

        msync(histo, bytes_needed, MS_SYNC);
}

int histogram_print(int time, int* data, int size) {
        int SCALE = 1; // Change to scale size of histogram

        if (data == NULL) {
                return -1;
        }

        if (size == 0) {
                printf("No Data!\n");
                return 0;
        }

        printf("----- Histogram for Time = %d -----\n", time);
        for (int i = 0; i < size; i++) {
                if (i < 10)  printf("0");
                if (i < 100) printf("0");
                printf("%d: ", i + LOWEST_RATE);
                for (int j = 0; j < data[i] / SCALE; j++) {
                        printf("#");
                }
                printf("\n");
        }
        printf("\n");

        return 0;
        // NOTE: this function assumes all values in data[] are >= 0
}

int determine_num_data_points(int* data, int size){
        int num_non_zero;
        int i;

        num_non_zero = 0;
        for(i = 0; i < size; ++i){
                if(data[i] != 0){
                        num_non_zero += data[i];
                }
        }

        return num_non_zero;
}

double histogram_mean(int* data, int size) {
        if (data == NULL) return -1;
        if (size == 0)    return 0;

        double sum = 0;
        int num_non_zero = 0;
        for (int i = 0; i < size; i++) {
                if(data[i] != 0){
                        sum += (i + LOWEST_RATE) * data[i];
                        num_non_zero += data[i];
                }
        }

        double mean = sum / ((double)num_non_zero);

        return mean;
}

double histogram_std(int* data, int size) {
        if (data == NULL) return -1;
        if (size == 0)    return 0;

        double mean = histogram_mean(data, size);
        double sum = 0;
        int num_non_zero;
        for (int i = 0; i < size; i++) {
                if(data[i] != 0){
                        sum += fabs(mean - (i + LOWEST_RATE)) * data[i];
                        num_non_zero += data[i];
                }

        }

        double std = sum / ((double)num_non_zero);
        return std;
}

int determine_bucket(struct tm* timestamp){
        int minutes;
        int bucket_num;

        minutes = timestamp->tm_min + (60 * timestamp->tm_hour);

        bucket_num = minutes / 15;

        return bucket_num;
}

int* fetch_at_bucket(int* histo, int bucket_num){
        int len;
        int* tmp;

        len = HIGHEST_RATE - LOWEST_RATE;

        tmp = histo + (bucket_num * len);

        return tmp;
}

static int map(int bpm, int min){
        return bpm - min;
}

void histogram_insert(int* histo, int* bucket_arr, struct ar2c_pkt* rcv_pkt){
        int bpm;
        int index;
        int result;

        bpm = rcv_pkt->bpm;

        index = map(bpm, LOWEST_RATE);

        bucket_arr[index]++;

        result = msync(histo, bytes_needed, MS_SYNC);
        BAIL_NEG_ONE(result);

        return;

 error: printf("Error syncing file!\n");
        return;
}

void free_histogram(int* histogram){

        //unmap
        if(munmap(histogram, bytes_needed) == -1) {

                exit(EXIT_FAILURE);
        }
}
