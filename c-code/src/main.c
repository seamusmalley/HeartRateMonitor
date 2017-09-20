#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <stdint.h>
#include <ctype.h>
#include <math.h>

#include "histogram.h"
#include "comms.h"
#include "db.h"
#include "error.h"

// baud rate to talk to arduino
#define BAUD_RATE B9600

// static functions
static int init_tty(int fd);
static void enter_cli(int fd);
static void init_system(int fd);
static int locate_space(char* str);
static void clear_screen(void);
static int debug_query_handler(void* arg, int argc, char** argv, char** col_name);
static int regression_query_handler(void* arg, int argc, char** argv, char** col_name);
static void reset_regression(void);
static int sqrt_query_handler(void* arg, int argc, char** argv, char** col_name);
static struct tm* get_time(void);
static double compute_a(void);
static double compute_b(void);
static double compute_rr(void);

// parent process tracks the child's pid
int child_pid;

// strings dealing with the DB
char* insert_bpm_template = "INSERT INTO BPM (minute_num, bpm) VALUES (%d, %d);";
char* insert_env_template = "INSERT INTO ENV (minute_num, env) VALUES (%d, %d);";
char* db_name = "tlopv2.db";

// numbers used for computation of regression
double n;
double sum_bpm;
double sum_env;
double sum_bpmenv;
double sum_envenv;
double sum_bpmbpm;

int main(int argc, char** argv){
        char* arduino_path;
        int fd;
        int result;
        int pid;

        // determine the arduino path
        if(argc >= 2){
                arduino_path = argv[1];
        }else{
                arduino_path = "/dev/ttyACM0";
        }

        printf("Arduino Path: %s\n", arduino_path);
        printf("Connecting...\n");

        // open the arduino file
        fd = open(arduino_path, O_RDWR | O_NOCTTY | O_NDELAY);
        BAIL_NEG_ONE(fd);

        // connect to the arduino over serial
        result = init_tty(fd);
        BAIL_NEG_ONE(result);

        // wait 1 second for arduino to reboot
        usleep(3000 * 1000);

        // flush buffer
        tcflush(fd, TCIFLUSH);

        printf("Connected successfully!\n");

        // fork into child and parent processes
        pid = fork();
        BAIL_NEG_ONE(pid);

        if(pid > 0){
                // we are the parent
                // track the child pid
                child_pid = pid;

                // enter the system, which polls the arduino every second
                init_system(fd);

                printf("Parent exiting!\n");
        }else{
                // we are the child

                // enter the cli, which prompts the user for commands
                enter_cli(fd);
                printf("Child exiting!\n");
        }

        return 0;

        // cleanup for error
 error: printf("Failed to setup connection!\n");
        printf("Shutting down...\n");
        close(fd);
        return -1;
}

/*
 * We enter into our CLI, which provides the user with
 * a multitude of commands. It loops through prompting,
 * accepting, and executing a command until the
 * user enters exit.  This is run by the CHILD process.
 */
static void enter_cli(int fd){
        printf("Fd: %d\n", fd);
        printf("Entered child process!\n");

        int* histo;
        sqlite3* db;
        int retval;

        // get a file descriptor to histogram in shared memory
        histo = init_histogram();
        BAIL_NULL(histo);

        // connect to the database
        db = create_db(db_name);
        if(db == NULL){
                printf("Error creating db!\n");
                _exit(-1);
        }

        while(1){

                char line[1024];
                char first_word[1024];
                char tmp[1024];
                int space_index;
                int val, result;
                struct c2ar_pkt* send_pkt;
                struct ar2c_pkt* rcv_pkt;

                // prompt for command
                printf("Enter a command:\n");
                fgets(line, sizeof(line), stdin);

                space_index = locate_space(line);
                if(space_index != -1){
                        strncpy(first_word, line, space_index);
                        first_word[space_index] = '\0';

                        printf("First word: %s\n", first_word);
                }else{
                        memset(first_word, 0, 1024);
                }


                val = -1;

                // show command
                if(strcmp("show", first_word) == 0){
                        sscanf(line, "%s %d", tmp, &val);
                        if(val == -1){
                                goto looperror;
                        }

                        printf("Show: %d\n", val);

                        // send a request to the arduino
                        send_pkt = init_c2ar_pkt(SHOW);
                        if(send_pkt == NULL){
                                continue;
                        }

                        send2ar(fd, send_pkt);
                        free_c2ar_pkt(send_pkt);

                        write(fd, &val, 1);
                }else if(strcmp("pause\n", line) == 0){
                        // pause command

                        // create a packet telling arduino to pause
                        send_pkt = init_c2ar_pkt(PAUSE);
                        if(send_pkt == NULL){
                                continue;
                        }

                        // send packet to arduino and free it
                        send2ar(fd, send_pkt);
                        free_c2ar_pkt(send_pkt);
                }else if(strcmp("resume\n", line) == 0){
                        // resume command

                        // create a packet telling arduino to resume
                        send_pkt = init_c2ar_pkt(RESUME);
                        if(send_pkt == NULL){
                                continue;
                        }

                        // send packet to arduino and free it
                        send2ar(fd, send_pkt);
                        free_c2ar_pkt(send_pkt);
                }else if(strcmp("date\n", line) == 0){
                        // date command
                        // repeatedly try to fetch date until we do it successfully
                        while(1){
                                send_pkt = init_c2ar_pkt(DATA_REQUEST);
                                if(send_pkt == NULL){
                                        //printf ("Failed to create send pkt\n");
                                        continue;
                                }

                                result = send2ar(fd, send_pkt);
                                free_c2ar_pkt(send_pkt);
                                if(result == ERROR){
                                        //if it fails to send, continue the loop
                                        //printf("Failed to send pkt\n");
                                        continue;
                                }

                                // sleep 0.5 seconds
                                usleep(1000 * 500);

                                //read the response
                                rcv_pkt = readfromar(fd);
                                if(rcv_pkt == NULL){
                                        // if it fails to send, continue the loop
                                        //printf("Failed to read pkt\n");
                                        continue;
                                }

                                //print_rcv_pkt(rcv_pkt);
                                if(invalid(rcv_pkt)){
                                        free_ar2c_pkt(rcv_pkt);
                                        //printf("Invalid rcv pkt\n");
                                        continue;
                                }

                                // print the result
                                struct tm* timestamp = rcv_pkt->timestamp;
                                printf("%d:%d:%d %d %d %d\n", timestamp->tm_hour, timestamp->tm_min, timestamp->tm_sec, timestamp->tm_mon, timestamp->tm_mday, timestamp->tm_year);
                                free_ar2c_pkt(rcv_pkt);

                                break;
                        }
                }else if(strcmp("rate\n", line) == 0){
                        // rate command
                        // fetch last reading of bpm from end-1 index of histogram
                        printf("Rate: %d\n", fetch_last_bpm_reading(histo));

                }else if(strcmp("env\n", line) == 0){
                        // env command
                        // fetch last reading of env from end-2 index of histogram
                        printf("Env: %d degrees C\n", fetch_last_env_reading(histo));

                }else if(strcmp("regression\n", line) == 0){
                        // regression for current time block
                        struct tm* current_time;
                        int bucket_num, next_bucket_num;
                        char* query;

                        current_time = get_time();
                        bucket_num = determine_bucket(current_time);
                        next_bucket_num = bucket_num + 1;

                        if(fetch_bpm_data_written_bit(histo) == 0 || fetch_env_data_written_bit(histo) == 0){
                                printf("Not enough data to calculate regression!\n");
                        }else{
                                reset_regression();

                                asprintf(&query, "SELECT bpm_avg.avg_bpm AS bpm_reading, env_avg.avg_env AS env_reading FROM (SELECT AVG(bpm) AS avg_bpm, minute_num FROM BPM GROUP BY minute_num) as bpm_avg, (SELECT AVG(env) AS avg_env, minute_num FROM ENV GROUP BY minute_num) as env_avg WHERE bpm_avg.minute_num = env_avg.minute_num AND bpm_avg.minute_num >= %d AND bpm_avg.minute_num < %d;", bucket_num * 15, next_bucket_num * 15);

                                retval = handle_query(db, query, regression_query_handler);
                                if(retval == -1){
                                        printf("Error querying regression data!\n");
                                }

                                printf("N: %lf\n", n);
                                printf("Sum bpm: %lf\n", sum_bpm);
                                printf("Sum env: %lf\n", sum_env);
                                printf("Sum bpm*env: %lf\n", sum_bpmenv);
                                printf("Sum env*env: %lf\n", sum_envenv);
                                printf("Sum bpm*bpm: %lf\n", sum_bpmbpm);

                                double a = compute_a();
                                double b = compute_b();

                                double rsquared = compute_rr();

                                printf("y = %lf + (%lf * x)\n", a, b);
                                printf("r squared: %lf\n", rsquared);

                                free(query);
                        }


                }else if(strcmp("regression", first_word) == 0){
                        // regression for specified time block
                        int bucket_num;
                        int next_bucket_num;
                        char* query;

                        sscanf(line, "%s %d", tmp, &val);

                        bucket_num = val;
                        next_bucket_num = bucket_num + 1;

                        if(fetch_bpm_data_written_bit(histo) == 0 || fetch_env_data_written_bit(histo) == 0){
                                printf("Not enough data to calculate regression!\n");
                        }else{
                                reset_regression();

                                asprintf(&query, "SELECT bpm_avg.avg_bpm AS bpm_reading, env_avg.avg_env AS env_reading FROM (SELECT AVG(bpm) AS avg_bpm, minute_num FROM BPM GROUP BY minute_num) as bpm_avg, (SELECT AVG(env) AS avg_env, minute_num FROM ENV GROUP BY minute_num) as env_avg WHERE bpm_avg.minute_num = env_avg.minute_num AND bpm_avg.minute_num >= %d AND bpm_avg.minute_num < %d;", bucket_num * 15, next_bucket_num * 15);

                                retval = handle_query(db, query, regression_query_handler);
                                if(retval == -1){
                                        printf("Error querying regression data!\n");
                                }

                                printf("N: %lf\n", n);
                                printf("Sum bpm: %lf\n", sum_bpm);
                                printf("Sum env: %lf\n", sum_env);
                                printf("Sum bpm*env: %lf\n", sum_bpmenv);
                                printf("Sum env*env: %lf\n", sum_envenv);
                                printf("Sum bpm*bpm: %lf\n", sum_bpmbpm);

                                double a = compute_a();
                                double b = compute_b();

                                double rsquared = compute_rr();

                                printf("y = %lf + (%lf * x)\n", a, b);
                                printf("r squared: %lf\n", rsquared);

                                free(query);
                        }

                        // regression for <val> time block
                }else if(strcmp("stat\n", line) == 0){
                        // stat for current time block
                        struct tm* current_time;
                        int bucket_num;
                        int next_bucket_num;
                        char* query = NULL;

                        current_time = get_time();
                        bucket_num = determine_bucket(current_time);
                        next_bucket_num = bucket_num + 1;

                        printf("Data for bucket %d (current bucket)\n", bucket_num);
                        printf("\nBPM Data\n--------\n");

                        if(fetch_bpm_data_written_bit(histo) == 0){
                                printf("No data to analyze!\n");
                        }else{


                                //printf("Time frame: %d -- %d\n", bucket_num * 15, next_bucket_num * 15);
                                asprintf(&query, "SELECT AVG(bpm) as mean_bpm FROM BPM WHERE minute_num >= %d AND minute_num < %d;", bucket_num * 15, next_bucket_num * 15);

                                retval = handle_query(db, query, debug_query_handler);
                                if(retval == -1){
                                        printf("Error querying bpm data!\n");
                                }

                                free(query);

                                query = NULL;
                                asprintf(&query, "SELECT COUNT(*) as reading_count_bpm FROM BPM WHERE minute_num >= %d AND minute_num < %d;", bucket_num * 15, next_bucket_num * 15);

                                retval = handle_query(db, query, debug_query_handler);
                                if(retval == -1){
                                        printf("Error querying bpm data!\n");
                                }

                                free(query);
                                query = NULL;

                                asprintf(&query, "SELECT MAX(mode_candidate) as mode_bpm, bpm FROM (SELECT COUNT(*) AS mode_candidate, bpm FROM BPM WHERE minute_num >= %d AND minute_num < %d GROUP BY bpm);", bucket_num * 15, next_bucket_num * 15);

                                retval = handle_query(db, query, debug_query_handler);
                                if(retval == -1){
                                        printf("Error querying bpm data!\n");
                                }

                                free(query);
                                query = NULL;

                                asprintf(&query, "SELECT AVG(bpm) AS median_bpm FROM (SELECT bpm FROM BPM WHERE minute_num >= %d AND minute_num < %d ORDER BY bpm LIMIT 2 - (SELECT COUNT(*) FROM BPM WHERE minute_num >= %d AND minute_num < %d) %% 2 OFFSET (SELECT (COUNT(*) - 1) / 2 FROM BPM WHERE minute_num >= %d AND minute_num < %d));", bucket_num * 15, next_bucket_num * 15, bucket_num * 15, next_bucket_num * 15, bucket_num * 15, next_bucket_num * 15);

                                retval = handle_query(db, query, debug_query_handler);
                                if(retval == -1){
                                        printf("Error querying bpm data!\n");
                                }

                                free(query);
                                query = NULL;

                                asprintf(&query, "SELECT AVG((BPM.bpm - Mean.avg) * (BPM.bpm - Mean.avg)) AS variance FROM BPM, (SELECT AVG(bpm) AS avg FROM BPM) as Mean;");

                                retval = handle_query(db, query, sqrt_query_handler);
                                if(retval == -1){
                                        printf("Error querying bpm data!\n");
                                }

                                free(query);
                        }

                        printf("\nENV Data\n--------\n");

                        if(fetch_env_data_written_bit(histo) == 0){
                                printf("No data to analyze!\n");
                        }else{


                                //printf("Time frame: %d -- %d\n", bucket_num * 15, next_bucket_num * 15);
                                asprintf(&query, "SELECT AVG(env) as mean_env FROM ENV WHERE minute_num >= %d AND minute_num < %d;", bucket_num * 15, next_bucket_num * 15);

                                retval = handle_query(db, query, debug_query_handler);
                                if(retval == -1){
                                        printf("Error querying env data!\n");
                                }

                                free(query);

                                query = NULL;
                                asprintf(&query, "SELECT COUNT(*) as reading_count_env FROM ENV WHERE minute_num >= %d AND minute_num < %d;", bucket_num * 15, next_bucket_num * 15);

                                retval = handle_query(db, query, debug_query_handler);
                                if(retval == -1){
                                        printf("Error querying env data!\n");
                                }

                                free(query);
                                query = NULL;

                                asprintf(&query, "SELECT MAX(mode_candidate) as mode_env, env FROM (SELECT COUNT(*) AS mode_candidate, env FROM ENV WHERE minute_num >= %d AND minute_num < %d GROUP BY env);", bucket_num * 15, next_bucket_num * 15);

                                retval = handle_query(db, query, debug_query_handler);
                                if(retval == -1){
                                        printf("Error querying env data!\n");
                                }

                                free(query);
                                query = NULL;

                                asprintf(&query, "SELECT AVG(env) AS median_env FROM (SELECT env FROM ENV WHERE minute_num >= %d AND minute_num < %d ORDER BY env LIMIT 2 - (SELECT COUNT(*) FROM ENV WHERE minute_num >= %d AND minute_num < %d) %% 2 OFFSET (SELECT (COUNT(*) - 1) / 2 FROM ENV WHERE minute_num >= %d AND minute_num < %d));", bucket_num * 15, next_bucket_num * 15, bucket_num * 15, next_bucket_num * 15, bucket_num * 15, next_bucket_num * 15);

                                retval = handle_query(db, query, debug_query_handler);
                                if(retval == -1){
                                        printf("Error querying env data!\n");
                                }

                                free(query);
                                query = NULL;

                                asprintf(&query, "SELECT AVG((ENV.env - Mean.avg) * (ENV.env - Mean.avg)) AS variance FROM ENV, (SELECT AVG(env) AS avg FROM ENV) as Mean;");

                                retval = handle_query(db, query, sqrt_query_handler);
                                if(retval == -1){
                                        printf("Error querying env data!\n");
                                }

                                free(query);

                                printf("\n");

                        }

                }else if(strcmp("stat", first_word) == 0){
                        // stat for <val> time block
                        sscanf(line, "%s %d", tmp, &val);

                        int bucket_num;
                        int next_bucket_num;
                        char* query;

                        bucket_num = val;
                        next_bucket_num = bucket_num + 1;

                        printf("Data for bucket %d (current bucket)\n", bucket_num);
                        printf("\nBPM Data\n--------\n");

                        if(fetch_bpm_data_written_bit(histo) == 0){
                                printf("No data to analyze!\n");
                        }else{


                                //printf("Time frame: %d -- %d\n", bucket_num * 15, next_bucket_num * 15);
                                asprintf(&query, "SELECT AVG(bpm) as mean_bpm FROM BPM WHERE minute_num >= %d AND minute_num < %d;", bucket_num * 15, next_bucket_num * 15);

                                retval = handle_query(db, query, debug_query_handler);
                                if(retval == -1){
                                        printf("Error querying bpm data!\n");
                                }

                                free(query);

                                query = NULL;
                                asprintf(&query, "SELECT COUNT(*) as reading_count_bpm FROM BPM WHERE minute_num >= %d AND minute_num < %d;", bucket_num * 15, next_bucket_num * 15);

                                retval = handle_query(db, query, debug_query_handler);
                                if(retval == -1){
                                        printf("Error querying bpm data!\n");
                                }

                                free(query);
                                query = NULL;

                                asprintf(&query, "SELECT MAX(mode_candidate) as mode_bpm, bpm FROM (SELECT COUNT(*) AS mode_candidate, bpm FROM BPM WHERE minute_num >= %d AND minute_num < %d GROUP BY bpm);", bucket_num * 15, next_bucket_num * 15);

                                retval = handle_query(db, query, debug_query_handler);
                                if(retval == -1){
                                        printf("Error querying bpm data!\n");
                                }

                                free(query);
                                query = NULL;

                                asprintf(&query, "SELECT AVG(bpm) AS median_bpm FROM (SELECT bpm FROM BPM WHERE minute_num >= %d AND minute_num < %d ORDER BY bpm LIMIT 2 - (SELECT COUNT(*) FROM BPM WHERE minute_num >= %d AND minute_num < %d) %% 2 OFFSET (SELECT (COUNT(*) - 1) / 2 FROM BPM WHERE minute_num >= %d AND minute_num < %d));", bucket_num * 15, next_bucket_num * 15, bucket_num * 15, next_bucket_num * 15, bucket_num * 15, next_bucket_num * 15);

                                retval = handle_query(db, query, debug_query_handler);
                                if(retval == -1){
                                        printf("Error querying bpm data!\n");
                                }

                                free(query);
                                query = NULL;

                                asprintf(&query, "SELECT AVG((BPM.bpm - Mean.avg) * (BPM.bpm - Mean.avg)) AS variance FROM BPM, (SELECT AVG(bpm) AS avg FROM BPM) as Mean;");

                                retval = handle_query(db, query, sqrt_query_handler);
                                if(retval == -1){
                                        printf("Error querying bpm data!\n");
                                }

                                free(query);
                        }

                        printf("\nENV Data\n--------\n");

                        if(fetch_env_data_written_bit(histo) == 0){
                                printf("No data to analyze!\n");
                        }else{


                                //printf("Time frame: %d -- %d\n", bucket_num * 15, next_bucket_num * 15);
                                asprintf(&query, "SELECT AVG(env) as mean_env FROM ENV WHERE minute_num >= %d AND minute_num < %d;", bucket_num * 15, next_bucket_num * 15);

                                retval = handle_query(db, query, debug_query_handler);
                                if(retval == -1){
                                        printf("Error querying env data!\n");
                                }

                                free(query);

                                query = NULL;
                                asprintf(&query, "SELECT COUNT(*) as reading_count_env FROM ENV WHERE minute_num >= %d AND minute_num < %d;", bucket_num * 15, next_bucket_num * 15);

                                retval = handle_query(db, query, debug_query_handler);
                                if(retval == -1){
                                        printf("Error querying env data!\n");
                                }

                                free(query);
                                query = NULL;

                                asprintf(&query, "SELECT MAX(mode_candidate) as mode_env, env FROM (SELECT COUNT(*) AS mode_candidate, env FROM ENV WHERE minute_num >= %d AND minute_num < %d GROUP BY env);", bucket_num * 15, next_bucket_num * 15);

                                retval = handle_query(db, query, debug_query_handler);
                                if(retval == -1){
                                        printf("Error querying env data!\n");
                                }

                                free(query);
                                query = NULL;

                                asprintf(&query, "SELECT AVG(env) AS median_env FROM (SELECT env FROM ENV WHERE minute_num >= %d AND minute_num < %d ORDER BY env LIMIT 2 - (SELECT COUNT(*) FROM ENV WHERE minute_num >= %d AND minute_num < %d) %% 2 OFFSET (SELECT (COUNT(*) - 1) / 2 FROM ENV WHERE minute_num >= %d AND minute_num < %d));", bucket_num * 15, next_bucket_num * 15, bucket_num * 15, next_bucket_num * 15, bucket_num * 15, next_bucket_num * 15);

                                retval = handle_query(db, query, debug_query_handler);
                                if(retval == -1){
                                        printf("Error querying env data!\n");
                                }

                                free(query);
                                query = NULL;

                                asprintf(&query, "SELECT AVG((ENV.env - Mean.avg) * (ENV.env - Mean.avg)) AS variance FROM ENV, (SELECT AVG(env) AS avg FROM ENV) as Mean;");

                                retval = handle_query(db, query, sqrt_query_handler);
                                if(retval == -1){
                                        printf("Error querying env data!\n");
                                }

                                free(query);

                                printf("\n");

                        }



                        // stat for <val> time block
                }else if(strcmp("hist\n", line) == 0){
                        // hist for current time block
                        while(1){
                                int bucket_num;

                                send_pkt = init_c2ar_pkt(DATA_REQUEST);
                                if(send_pkt == NULL){
                                        continue;
                                }

                                result = send2ar(fd, send_pkt);
                                free_c2ar_pkt(send_pkt);
                                if(result == ERROR){
                                        //if it fails to send, continue the loop
                                        continue;
                                }

                                // sleep 0.5 seconds
                                usleep(1000 * 500);

                                //read the response
                                rcv_pkt = readfromar(fd);
                                if(rcv_pkt == NULL){
                                        // if it fails to send, continue the loop
                                        continue;
                                }


                                //print_rcv_pkt(rcv_pkt);
                                if(invalid(rcv_pkt)){
                                        continue;
                                }

                                bucket_num = determine_bucket(rcv_pkt->timestamp);
                                int* bucket_arr = fetch_at_bucket(histo, bucket_num);

                                histogram_print(bucket_num, bucket_arr, HIGHEST_RATE - LOWEST_RATE);
                                break;
                        }
                }else if(strcmp("hist", first_word) == 0){
                        // hist for val time block
                        sscanf(line, "%s %d", tmp, &val);
                        if(val == -1){
                                goto looperror;
                        }

                        printf("Bucket num: %d\n", val);

                        int* bucket_arr = fetch_at_bucket(histo, val);
                        histogram_print(val, bucket_arr, HIGHEST_RATE - LOWEST_RATE);
                }else if(strcmp("reset\n", line) == 0){
                        // reset histogram
                        histogram_reset(histo);
                }else if(strcmp("clear\n", line) == 0){
                        // clear the screen
                        clear_screen();
                }else if(strcmp("exit\n", line) == 0){
                        // exit the cli and child process
                        break;
                }else if(strcmp("help\n", line) == 0){
                        // help command, print usage info
                        printf("\nAvailable commands:\n");
                        printf("show X\npause\nresume\nrate\ndate\nenv\nhist\nhist X\nreset\nexit\nhelp\nclear\nqpbm\nqenv\nstat X\nregression X\n\n");
                }else if(strcmp("qbpm\n", line) == 0){
                        /*
                         * this is a debug command we introduced
                         * prints info from the bpm table
                         */
                        retval = handle_query(db, "SELECT AVG(bpm), minute_num, COUNT(*) AS readings_in_minute FROM BPM GROUP BY minute_num;", debug_query_handler);
                        if(retval == -1){
                                printf("Error querying bpm data!\n");
                        }
                }else if(strcmp("qenv\n", line) == 0){
                        /*
                         * this is a debug command we introduced
                         * prints info from the env table
                         */
                        retval = handle_query(db, "SELECT AVG(env), minute_num, COUNT(*) AS readings_in_minute FROM ENV GROUP BY minute_num;", debug_query_handler);
                        if(retval == -1){
                                printf("Error querying env data!\n");
                        }
                }else{
                        // invalid command
                        goto looperror;
                }

                continue;

        looperror: printf("Invalid command!\n");
        }

        return;

        // error handling
 error: printf("Failed to mmap data for command line!\n");
        return;
}

// handler for clearing the screen
static void clear_screen(void){
        system("clear");
}

// returns index of first space in a string
static int locate_space(char* str){
        int i;

        i = 0;
        while(*str != '\0'){
                if(*str == ' '){
                        return i;
                }
                ++str;
                ++i;
        }

        return -1;
}

/*
 * Enter into our polling system.  This is run
 * by the PARENT process and interacts with the
 * Arduino over Serial.
 */
static void init_system(int fd){
        int result;
        int* histo;
        struct c2ar_pkt* send_pkt;
        struct ar2c_pkt* rcv_pkt;
        struct c2ar_pkt* response_pkt;
        int bucket_num;
        int is_outlier;
        double histo_mean;
        double histo_std;
        int num_data_points;
        int count;
        int child_status;
        int is_env;

        sqlite3* db;
        char** create_statements;
        int retval;
        int i;

        // create the create statements
        create_statements = (char**)malloc(2 * sizeof(char*));
        for(i = 0; i < 2; ++i){
                create_statements[i] = (char*)malloc(512 * sizeof(char));
        }
        create_statements[0] = "CREATE TABLE IF NOT EXISTS BPM (minute_num INT NOT NULL, bpm INT NOT NULL);";
        create_statements[1] = "CREATE TABLE IF NOT EXISTS ENV (minute_num INT NOT NULL, env INT NOT NULL);";

        // connect to the database
        db = create_db(db_name);
        if(db == NULL){
                printf("Error creating db!\n");
                _exit(-1);
        }

        // create the tables
        retval = create_tables(db, create_statements, 2);
        if(retval == -1){
                printf("Error creating tables!\n");
                _exit(-1);
        }

        histo = init_histogram();
        BAIL_NULL(histo);

        //printf("histo: %p\n", (void*)histo);

        // synchronize the RTC with this computer's clock
        send_time_synchronization(fd);

	usleep(1000 * 2000);

        count = 0;

        while(waitpid(child_pid, &child_status, WNOHANG) == 0){

                is_env = 0;

                ++count;

                usleep(1000 * 500); // sleep 0.5 second

                if(count == 10){
                        send_pkt = init_c2ar_pkt(ENV);
                        is_env = 1;
                        count = 0;
                }else{
                        send_pkt = init_c2ar_pkt(DATA_REQUEST);
                }

                // send a request to the arduino
                if(send_pkt == NULL){
                        continue;
                }

                result = send2ar(fd, send_pkt);
                free_c2ar_pkt(send_pkt);
                if(result == ERROR){
                        //if it fails to send, continue the loop
                        continue;
                }

                // sleep 0.5 seconds
                usleep(1000 * 500);

                //read the response
                rcv_pkt = readfromar(fd);
                if(rcv_pkt == NULL){
                        // if it fails to send, continue the loop
                        continue;
                }


                //print_rcv_pkt(rcv_pkt);
                if(invalid(rcv_pkt)){
                        free_ar2c_pkt(rcv_pkt);
                        continue;
                }

                if(is_env == 0){
                        // heart rate data
                        bucket_num = determine_bucket(rcv_pkt->timestamp);
                        int* bucket_arr = fetch_at_bucket(histo, bucket_num);

                        // determine mean and standard deviation of current bucket
                        histo_mean = histogram_mean(bucket_arr, HIGHEST_RATE - LOWEST_RATE);
                        histo_std = histogram_std(bucket_arr, HIGHEST_RATE - LOWEST_RATE);

                        // count number of data points at current bucket
                        num_data_points = determine_num_data_points(bucket_arr, HIGHEST_RATE - LOWEST_RATE);

                        // determine if it is within 2 stdev of mean (this makes it valid, otherwise it is an outlier)
                        is_outlier = (abs(histo_mean - rcv_pkt->bpm) > 2 * histo_std);

                        if((is_outlier && num_data_points > 15) || (rcv_pkt->bpm >HIGHEST_RATE || rcv_pkt->bpm < LOWEST_RATE)){
                                // if its an outlier, tell the arduino
                                response_pkt = init_c2ar_pkt(OUTLIER);
                                //printf("Is outlier!\n");
                        }else{
                                // otherwise, tell arduino it is valid
                                response_pkt = init_c2ar_pkt(VALID);

                                // insert the valid value into the histogram
                                histogram_insert(histo, bucket_arr, rcv_pkt);

                                // set the last read value in histo for "caching"
                                set_last_bpm_reading(histo, rcv_pkt->bpm);

                                // if it's not an outlier, put in DB
                                retval = insert_bpm_data(db, insert_bpm_template, rcv_pkt->bpm, rcv_pkt->timestamp);

                                if(retval == -1){
                                        printf("Error inserting bpm data\n");
                                }else{
                                        set_bpm_data_written_bit(histo, 1);
                                }
                        }
                        free_ar2c_pkt(rcv_pkt);
                        //printf("\n");

                        if(response_pkt == NULL){
                                continue;
                        }

                        // send the result to the arduino
                        result = send2ar(fd, response_pkt);
                        free_c2ar_pkt(response_pkt);

                        if(result == ERROR){
                                continue;
                        }
                }else{
                        // env data
                        retval = insert_env_data(db, insert_env_template, rcv_pkt->bpm, rcv_pkt->timestamp);
                        set_last_env_reading(histo, rcv_pkt->bpm);
                        if(retval == -1){
                                printf("Error inserting env data\n");
                        }else{
                                set_env_data_written_bit(histo, 1);
                        }
                }

        }

        return;

 error: printf("Error initializing system!\n");
        return;
}

// provided function to initialize the connection with arduino over serial
static int init_tty(int fd){
        struct termios tty;
        int result;

        memset(&tty, 0, sizeof(tty));

        result = tcgetattr(fd, &tty);
        BAIL_NEG_ONE(result);

        result = cfsetospeed(&tty, (speed_t)BAUD_RATE);
        BAIL_NEG_ONE(result);

        result = cfsetispeed(&tty, (speed_t)BAUD_RATE);
        BAIL_NEG_ONE(result);

        // 8 bits, no parity, no stop bits
        tty.c_cflag &= ~PARENB;
        tty.c_cflag &= ~CSTOPB;
        tty.c_cflag &= ~CSIZE;
        tty.c_cflag |= CS8;

        // No flow control
        tty.c_cflag &= ~CRTSCTS;

        // Set local mode and enable the receiver
        tty.c_cflag |= (CLOCAL | CREAD);

        // Disable software flow control
        tty.c_iflag &= ~(IXON | IXOFF | IXANY);

        // Make raw
        tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
        tty.c_oflag &= ~OPOST;

        // Infinite timeout and return from read() with >1 byte available
        tty.c_cc[VMIN]  = 0;
        tty.c_cc[VTIME] = 0;

        result = tcsetattr(fd, TCSANOW, &tty);
        BAIL_NEG_ONE(result);

        return NO_ERROR;

 error: return ERROR;
}

// this handles a query where all we need to do is print the result set
static int debug_query_handler(void* arg, int argc, char** argv, char** col_name){
        UNUSED(arg);

        int i;

        for(i = 0; i < argc - 1; ++i){
                printf("%s: %s, ", col_name[i], argv[i]);
        }
        printf("%s: %s\n", col_name[argc - 1], argv[argc - 1]);

        return 0;
}

// this handles the regression query and using the computational variables from the top of the file
// modifies their values accordingly using result set
static int regression_query_handler(void* arg, int argc, char** argv, char** col_name){
        double bpm_reading;
        double env_reading;

        UNUSED(arg);
        UNUSED(col_name);

        if(argc != 2){
                return 0;
        }

        sscanf(argv[0], "%lf", &bpm_reading);
        sscanf(argv[1], "%lf", &env_reading);

        ++n;
        sum_bpm += bpm_reading;
        sum_env += env_reading;
        sum_bpmenv += (bpm_reading * env_reading);
        sum_bpmbpm += (bpm_reading * bpm_reading);
        sum_envenv += (env_reading * env_reading);

        printf("BPM: %lf, ENV: %lf\n", bpm_reading, env_reading);

        return 0;
}

// resets the values of the regression variables
static void reset_regression(void){
        n = 0;
        sum_bpm = 0;
        sum_env = 0;
        sum_bpmenv = 0;
        sum_envenv = 0;
        sum_bpmbpm = 0;
}

// handles the standard deviation command, which requires square rooting the result before printing
static int sqrt_query_handler(void* arg, int argc, char** argv, char** col_name){
        UNUSED(arg);
        UNUSED(argc);
        UNUSED(col_name);

        int i = 0;

        if(argc == 0 || strcmp("(null)", argv[i]) == 0 || strcmp("Nothing read!", argv[i]) == 0 || argv[i] == NULL){
                printf("stdev: (null)\n");
                return 0;
        }

        double val;
        sscanf(argv[i], "%lf", &val);

        printf("stdev: %lf\n", sqrt((double)val));

        return 0;
}

// gets current time
static struct tm* get_time(void){
        time_t rawtime;

        time(&rawtime);
        return localtime(&rawtime);
}

// bpm is y, env is x
// helper function for computing "a" portion of regression
static double compute_a(void){
        double a;

        a = ((sum_bpm * sum_envenv) - (sum_env * sum_bpmenv)) / ((n * sum_envenv) - (sum_env * sum_env));

        printf("A val: %lf\n", a);

        return a;
}

// bpm is y, env is x
// helper function for computing "b" portion of regression
static double compute_b(void){
        double top;
        double bottom;

        top = ((n * sum_bpmenv) - (sum_bpm * sum_env));
        printf("Top b: %lf\n", top);

        bottom = ((n * sum_envenv) - (sum_env * sum_env));
        printf("Bottom b: %lf\n", bottom);

        return top / bottom;
}

// bpm is y, env is x
// helper function for computing "r squared" portion of regression
static double compute_rr(void){
        double top;
        double bottomleft;
        double bottomright;
        double bottom;
        double r;

        top = (n * sum_bpmenv) - (sum_bpm * sum_env);
        bottomleft = (n * sum_envenv) - (sum_env * sum_env);
        bottomright = (n * sum_bpmbpm) - (sum_bpm * sum_bpm);

        bottom = sqrt((bottomleft * bottomright));

        r = top / bottom;

        return (r * r);
}
