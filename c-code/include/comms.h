#ifndef _COMMS_H_
#define _COMMS_H_

#include <time.h>

#define OUTLIER 0
#define VALID 1
#define DATA_REQUEST 2
#define SHOW 3
#define PAUSE 4
#define RESUME 5
#define ENV 6

struct ar2c_pkt {
        int bpm;
        struct tm* timestamp;
};

struct c2ar_pkt {
        char data_code;
};

struct c2ar_pkt* init_c2ar_pkt(char code);
void free_c2ar_pkt(struct c2ar_pkt* pkt);
void free_ar2c_pkt(struct ar2c_pkt* pkt);

// send a receive methods
int send2ar(int fd, struct c2ar_pkt* pkt);
struct ar2c_pkt* readfromar(int fd);
int send_time_synchronization(int fd);

int invalid(struct ar2c_pkt* rcv_pkt);

void print_rcv_pkt(struct ar2c_pkt* rcv_pkt);

#endif
