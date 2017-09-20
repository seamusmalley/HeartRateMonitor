#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "db.h"

static struct tm* get_time(void);
static int env_query_handler(void* arg, int argc, char** argv, char** col_name);
static int bpm_query_handler(void* arg, int argc, char** argv, char** col_name);

#define UNUSED(x) (void)(x)

int main(void){

        char* insert_bpm_template = "INSERT INTO BPM (minute_num, bpm) VALUES (%d, %d);";
        char* insert_env_template = "INSERT INTO ENV (minute_num, env) VALUES (%d, %d);";
        char* db_name = "tlopv2.db";

        char** create_statements;
        sqlite3* db;
        int retval;
        int i;

        create_statements = (char**)malloc(2 * sizeof(char*));
        for(i = 0; i < 2; ++i){
                create_statements[i] = (char*)malloc(512 * sizeof(char));
        }
        create_statements[0] = "CREATE TABLE IF NOT EXISTS BPM (minute_num INT NOT NULL, bpm INT NOT NULL);";
        create_statements[1] = "CREATE TABLE IF NOT EXISTS ENV (minute_num INT NOT NULL, env INT NOT NULL);";

        db = create_db(db_name);
        if(db == NULL){
                printf("Error creating db!\n");
                return -1;
        }

        retval = create_tables(db, create_statements, 2);
        if(retval == -1){
                printf("Error creating tables!\n");
                return -1;
        }



        retval = insert_bpm_data(db, insert_bpm_template, 100, get_time());
        if(retval == -1){
                printf("Error inserting bpm data!\n");
                return -1;
        }

        retval = insert_env_data(db, insert_env_template, 72, get_time());
        if(retval == -1){
                printf("Error inserting env data!\n");
                return -1;
        }

        retval = handle_query(db, "SELECT AVG(env), minute_num, COUNT(*) AS readings_in_minute FROM ENV GROUP BY minute_num;", env_query_handler);
        if(retval == -1){
                printf("Error querying env data!\n");
                return -1;
        }


        printf("\n");

        retval = handle_query(db, "SELECT AVG(bpm), minute_num, COUNT(*) AS readings_in_minute FROM BPM GROUP BY minute_num;", bpm_query_handler);
        if(retval == -1){
                printf("Error querying bpm data!\n");
                return -1;
        }

        printf("It works!\n");
}

static struct tm* get_time(void){
        time_t rawtime;

        time(&rawtime);
        return localtime(&rawtime);
}

static int env_query_handler(void* arg, int argc, char** argv, char** col_name){
        UNUSED(arg);

        int i;

        for(i = 0; i < argc - 1; ++i){
                printf("%s: %s, ", col_name[i], argv[i]);
        }
        printf("%s: %s\n", col_name[argc - 1], argv[argc - 1]);

        return 0;
}

static int bpm_query_handler(void* arg, int argc, char** argv, char** col_name){
        UNUSED(arg);

        int i;

        for(i = 0; i < argc - 1; ++i){
                printf("%s: %s, ", col_name[i], argv[i]);
        }
        printf("%s: %s\n", col_name[argc - 1], argv[argc - 1]);

        return 0;
}
