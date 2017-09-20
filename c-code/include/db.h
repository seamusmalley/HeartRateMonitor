#ifndef DB_H
#define DB_H

#include <sqlite3.h>
#include <time.h>

typedef int (*callback_fn)(void* arg, int argc, char** argv, char** colName);

// initialize the database
sqlite3* create_db(char* file_name);

// create tables via SQL create statements contained in the char**
int create_tables(sqlite3* db, char** create_statements, int num_statements);

// insert heartbeat data and time data
int insert_bpm_data(sqlite3* db, char* insert_bpm_template, int bpm, struct tm* timestamp);

// insert env data and time data (env data is temp in degrees C)
int insert_env_data(sqlite3* db, char* insert_env_template, int env, struct tm* timestamp);

// handle a query
int handle_query(sqlite3* db, char* query, callback_fn fn);

// close the database connection
void close_db(sqlite3* db);

#endif
