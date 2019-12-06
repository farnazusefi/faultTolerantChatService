#include "fileService.h"

void get_chatroom_file_name(u_int32_t me, char *chatroom, char *filename)
{
    sprintf(filename, "%d_%s.chatroom", me, chatroom);
}

void create_log_files(u_int32_t me, u_int32_t num_of_servers, int recreate, int *fds)
{
    int i;
    char filename[20];
    log_files = (FILE **) malloc(num_of_servers * sizeof(FILE *));

    for(i = 1; i <= num_of_servers;i++)
    {
        sprintf(filename, "%d_server%d.log", me, i);
        log_info("creating/opening file %s", filename);
        if(recreate)
        	log_files[i-1] = fopen(filename, "w+");
        else
        	log_files[i-1] = fopen(filename, "a+");
        //fds[i-1] = fileno(log_files[i-1]);
    }

}

void create_chatroom_file(u_int32_t me, char *chatroom_name, int recreate)
{
    FILE *f;
    u_int32_t size = 15 + strlen(chatroom_name);
    char filename[size];
    sprintf(filename, "%d_%s.chatroom", me, chatroom_name);
    log_info("creating/opening file %s", filename);
    if(recreate)
    	f = fopen(filename, "w+");
    else
    	f = fopen(filename, "a+");
    fclose(f);
}

void addEventToLogFile(u_int32_t server_id, char *line)
{
    FILE * f = log_files[server_id - 1];
    log_info("writing to file %s", line);
    fwrite(line, 1, strlen(line), f);
    fflush(f);
}

void refineLogFile(u_int32_t lc)
{
// TODO: TBD
}

void parseLineInLogFile(char *line, logEvent *e)
{
    log_debug("line is %s", line);
    sscanf(line, "%d~%[^\t\n~]~%c~%[^\n\t]", &e->lamportCounter, e->chatroom,  &e->eventType, e->payload);
    log_debug("%d, %s, %c, %s", e->lamportCounter, e->chatroom,  e->eventType, e->payload);
}

void addMessageToChatroomFile(u_int32_t me, char *chatroom, Message m)
{
    char line[400];
    char filename[20];
    get_chatroom_file_name(me, chatroom, filename);
    FILE * f = fopen(filename, "a+");
    sprintf(line, "%d~%d~%s~%s~%s\n", m.serverID, m.lamportCounter, m.userName, m.message, m.additionalInfo);
    log_info("writing to chatroom file %s too %s", line, filename);
    fwrite(line, 1, strlen(line), f);
    fclose(f);
}

void parseLineInMessagesFile(char *line, Message *m)
{
    sscanf(line, "%d~%d~%[^\t\n~]~%[^\t\n~]~%s", &m->serverID, &m->lamportCounter, m->userName, m->message, m->additionalInfo);
}


void addLikerToMessage(char *chatroom, u_int32_t server_id, u_int32_t lamportCounter, char *userName)
{
    // maybe not nessessary for now
}

void removeLikerOfMessage(char *chatroom, u_int32_t server_id, u_int32_t lamportCounter, char *userName)
{
    // maybe not necessary for now
}

int get_last_messages(char * chatroom, Message* output, u_int32_t num_of_messages)
{
 return 0;
}

void get_logs_newer_than(u_int32_t server_id, u_int32_t lamport_counter, u_int32_t *length, logEvent *logs)
{
    FILE * fp = log_files[server_id - 1];
    char * line = NULL;
    size_t len = 0;
    ssize_t read;
    logEvent e;
    *length = 0;
    memset(&e, 0, sizeof(e));
    rewind(fp);
    while ((read = getline(&line, &len, fp)) != -1) {
        log_debug("Retrieved line of length %zu from server %d log file:\n", read, server_id);
        log_debug("%s", line);
        if(strlen(line) < 2)    // reached \n
            continue;
        parseLineInLogFile(line, &e);
        if(e.lamportCounter > lamport_counter)
        {
            logs[*length].lamportCounter = e.lamportCounter;
            logs[*length].eventType = e.eventType;
            memcpy(logs[*length].payload, e.payload, 100);
            memcpy(logs[*length].additionalInfo, e.additionalInfo, 20);
            memcpy(logs[*length].chatroom, e.chatroom, 20);
            (*length)++;
        }
    }
    fseek(fp, 0, SEEK_END);
}

void retrieve_chatroom_history(u_int32_t me, char *chatroom, u_int32_t *num_of_messages, Message *messages)
{
    char filename[20];
    int read;
    char line[400];
    u_int32_t len;
    *num_of_messages = 0;
    get_chatroom_file_name(me, chatroom, filename);
    FILE *cf = fopen(filename, "r");
	if ( cf != NULL )
	{
		while ((read = getline(&line, &len, cf)) != -1) {
            if(len < 3)
                continue;
			parseLineInMessagesFile(line, &messages[*num_of_messages]);
			log_debug("LTS = %d, %d", messages[*num_of_messages].serverID, messages[*num_of_messages].lamportCounter);
			(*num_of_messages)++;
		}
	}
}

void retrieve_line_from_logs(logEvent *e, u_int32_t *available_data, u_int32_t num_servers, u_int32_t *last_processed_counters)
{
    FILE * fp;
    int i;
    char * line = NULL;
    size_t len = 0;
    ssize_t read;
    logEvent parsed_e;
    for(i = 0;i< num_servers;i++)
    {
        fp = log_files[i];
        rewind(fp);
        available_data[i] = 0;
        while ((read = getline(&line, &len, fp)) != -1)
        {
            if(strlen(line) < 2)    // reached \n or empty line
                continue;
            parseLineInLogFile(line, &parsed_e);
            if(parsed_e.lamportCounter > last_processed_counters[i]){
                memcpy(&e[i], &parsed_e, sizeof(parsed_e));
                available_data[i] = 1;
                break;
            }
        }
    }
}
