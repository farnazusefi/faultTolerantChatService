#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include "log.h"
#include "fileService.h"

void getline_test()
{

    FILE * fp = fopen("2_server2.log", "r");
    char * line = NULL;
    size_t len = 0;
    ssize_t read;
    //logEvent e;
    int offset = 0;
    //*length = 0;
    while ((read = getline(&line, &len, fp)) != -1) {
        log_debug("Retrieved line of length %zu from server %d log file:\n", read, 2);
        log_debug("%s", line);
        //parseLineInLogFile(line, &e);
        /*if(e.lamportCounter > lamport_counter)
        {
            memcpy(logs + offset, &e, sizeof(e));
            offset+= sizeof(e);
            (*length)++;
        }
       */
    }
}


int main()
{
    getline_test();
    //create_chatroom_from_logs();
}
