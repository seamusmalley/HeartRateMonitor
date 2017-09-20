#ifndef _HISTOGRAM_H_
#define _HISTOGRAM_H_

#include "comms.h"

#define LOWEST_RATE 30
#define HIGHEST_RATE 200
#define NUM_BUCKETS 96

int* init_histogram(void);

void free_histogram(int* histo);

int histogram_print(int time, int* data, int size);

double histogram_std(int* data, int size);

double histogram_mean(int* data, int size);

int determine_bucket(struct tm* timestamp);

void histogram_insert(int* histo, int* bucket_arr, struct ar2c_pkt* rcv_pkt);

int determine_num_data_points(int* bucket_arr, int size);

int* fetch_at_bucket(int* histo, int bucket_num);

void histogram_reset(int* histo);

void set_bpm_data_written_bit(int* histo, int val);

int fetch_bpm_data_written_bit(int* histo);

void set_env_data_written_bit(int* histo, int val);

int fetch_env_data_written_bit(int* histo);

void set_last_bpm_reading(int* histo, int val);

void set_last_env_reading(int* histo, int val);

int fetch_last_env_reading(int* histo);

int fetch_last_bpm_reading(int* histo);

#endif
