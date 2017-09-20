#define _GNU_SOURCE
#include <sqlite3.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "db.h"

sqlite3_stmt* statement;

char* create_table_template;


static int compute_time_val(struct tm* timestamp);

// initialize the database
sqlite3* create_db(char* file_name){
        sqlite3* db;

        int retval = sqlite3_open(file_name, &db);
        if (retval != SQLITE_OK) {
                printf("Error creating db: %s\n", sqlite3_errmsg(db));
                return NULL;
        }
        return db;
}

// create tables via SQL create statements contained in the char**
int create_tables(sqlite3* db, char** create_statements, int num_statements){
        int retval;

        for (int i = 0; i < num_statements; i++) {
                sqlite3_prepare_v2(db, create_statements[i], -1, &statement, NULL);
                retval = sqlite3_step(statement);
                if (retval != SQLITE_DONE) {
                        printf("Error creating table: %s\n", sqlite3_errmsg(db));
                        return -1;
                }
                sqlite3_finalize(statement);
        }
        return 0;
}

// insert heartbeat data and time data
int insert_bpm_data(sqlite3* db, char* insert_bpm_template, int bpm, struct tm* timestamp){
        int retval;

        char* query = NULL;
        int time = compute_time_val(timestamp);
        asprintf(&query, insert_bpm_template, time, bpm);

        sqlite3_prepare_v2(db, query, strlen(query), &statement, NULL);
        retval = sqlite3_step(statement);
        if (retval != SQLITE_DONE) {
                return -1;
        }
        sqlite3_finalize(statement);
        free(query);
        return 0;
}

// insert env data and time data (env data is temp in degrees C)
int insert_env_data(sqlite3* db, char* insert_env_template, int env, struct tm* timestamp){
        int retval;

        char* query = NULL;
        int time = compute_time_val(timestamp);

        asprintf(&query, insert_env_template, time, env);
        sqlite3_prepare_v2(db, query, strlen(query), &statement, NULL);
        retval = sqlite3_step(statement);
        if (retval != SQLITE_DONE) {
                return -1;
        }
        sqlite3_finalize(statement);
        free(query);
        return 0;
}

// handle a query
int handle_query(sqlite3* db, char* query, callback_fn fn){
        char* error_msg;

        sqlite3_exec(db, query, fn, NULL, &error_msg);

        if(error_msg != NULL){
                printf("Error executing query: %s\n", error_msg);
                sqlite3_free(error_msg);
                return -1;
        }

        return 0;
}

void close_db(sqlite3* db){
        sqlite3_close(db);
}

static int compute_time_val(struct tm* timestamp){
        return timestamp->tm_min + (60 * timestamp->tm_hour);
}
