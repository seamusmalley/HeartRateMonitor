#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

#include "comms.h"
#include "error.h"


struct c2ar_pkt* init_c2ar_pkt(char code){
        struct c2ar_pkt* pkt;

        pkt = (struct c2ar_pkt*)malloc(sizeof(struct c2ar_pkt));
        BAIL_NULL(pkt);

        pkt->data_code = code;

        return pkt;

 error: return NULL;
}

void free_c2ar_pkt(struct c2ar_pkt* pkt){
        if(pkt){
                free(pkt);
        }
}

void free_ar2c_pkt(struct ar2c_pkt* pkt){
        if(pkt){
                if(pkt->timestamp){
                        free(pkt->timestamp);
                }
                free(pkt);
        }
}

/*
 *
 * SEND AND RECEIVE METHODS
 *
 */

int send2ar(int fd, struct c2ar_pkt* pkt){
        size_t amt_written;
        char byte;

        byte = pkt->data_code;

        amt_written = write(fd, &byte, 1);
        BAIL_NEG_ONE((int)amt_written);

        return NO_ERROR;

 error: printf("Error writing to Arduino!\n");
        return ERROR;
}

// FIXME: THIS IS CURRENTLY BROKEN
struct ar2c_pkt* readfromar(int fd){
        char buf[32];
        struct ar2c_pkt* pkt;
        struct tm* timestamp;
        size_t amt_read;
        int bpm;
        int day;
        int month;
        int year;
        int ss;
        int mm;
        int hh;

        amt_read = read(fd, &buf, 32);
        if(amt_read == 0){
                 goto error;
        }
        buf[amt_read] = '\0';

        sscanf(buf, "%d %d %d %d %d %d %d", &bpm, &day, &month, &year, &ss, &mm, &hh);

        pkt = (struct ar2c_pkt*)malloc(sizeof(struct ar2c_pkt));
        BAIL_NULL(pkt);

        pkt->bpm = bpm;

        timestamp = (struct tm*)malloc(sizeof(struct tm));
        BAIL_NULL(timestamp);

        timestamp->tm_sec = ss;
        timestamp->tm_min = mm;
        timestamp->tm_hour = hh;
        timestamp->tm_mon = month;
        timestamp->tm_mday = day;
        timestamp->tm_year = year;

        pkt->timestamp = timestamp;


        return pkt;

 error: printf("Nothing read!\n");
        return NULL;

}

int send_time_synchronization(int fd){
        time_t t;
        struct tm current_time;
        int day;
        int month;
        int year;
        int ss;
        int mm;
        int hh;
        int amt_written;

        t = time(NULL);
        current_time = *localtime(&t);
        //BAIL_NULL(current_time);

        day = current_time.tm_mday;
        month = current_time.tm_mon + 1;
        year = current_time.tm_year + 1900;

        ss = current_time.tm_sec;
        mm = current_time.tm_min;
        hh = current_time.tm_hour;

        amt_written = write(fd, &day, 1);
        BAIL_NEG_ONE(amt_written);

        amt_written = write(fd, &month, 1);
        BAIL_NEG_ONE(amt_written);

        amt_written = write(fd, &year, 1);
        BAIL_NEG_ONE(amt_written);

        amt_written = write(fd, &ss, 1);
        BAIL_NEG_ONE(amt_written);

        amt_written = write(fd, &mm, 1);
        BAIL_NEG_ONE(amt_written);

        amt_written = write(fd, &hh, 1);
        BAIL_NEG_ONE(amt_written);

        return NO_ERROR;

 error: printf("ERROR SENDING TIME SYNC\n");
        return ERROR;
}

void print_rcv_pkt(struct ar2c_pkt* rcv_pkt){
        int bpm;
        struct tm* timestamp;

        bpm = rcv_pkt->bpm;
        timestamp = rcv_pkt->timestamp;

        printf("bpm: %d, %d:%d:%d, %d %d %d\n", bpm, timestamp->tm_hour, timestamp->tm_min, timestamp->tm_sec, timestamp->tm_mon, timestamp->tm_mday, timestamp->tm_year);
}

int invalid(struct ar2c_pkt* rcv_pkt){
        struct tm* timestamp;

        timestamp = rcv_pkt->timestamp;

        if(timestamp->tm_hour > 23 || timestamp->tm_hour < 0){
                return 1;
        }

        if(timestamp->tm_min > 60 || timestamp->tm_min < 0){
                return 1;
        }

        return 0;
}
