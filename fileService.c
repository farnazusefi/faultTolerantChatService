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
    int offset = 0;
    *length = 0;
    rewind(fp);
    while ((read = getline(&line, &len, fp)) != -1) {
        log_debug("Retrieved line of length %zu from server %d log file:\n", read, server_id);
        log_debug("%s", line);
        if(strlen(line) < 2)    // reached \n
            break;
        parseLineInLogFile(line, &e);
        if(e.lamportCounter > lamport_counter)
        {
            memcpy(logs + offset, &e, sizeof(e));
            offset+= sizeof(e);
            (*length)++;
        }
    }
    fseek(fp, 0, SEEK_END);
}

void retrieve_chatroom_history(u_int32_t me, char *chatroom, u_int32_t *num_of_messages, Message *mesages)
{
    char filename[20];
    int read;
    char line[400];
    u_int32_t len;
    Message m;
    *num_of_messages = 0;
    get_chatroom_file_name(me, chatroom, filename);
    FILE *cf = fopen(filename, "r");
	if ( cf != NULL )
	{
		while ((read = getline(&line, &len, cf)) != -1) {
            if(len < 3)
                break;
			parseLineInMessagesFile(line, &mesages[*num_of_messages]);
			log_debug("LTS = %d, %d", m.serverID, m.lamportCounter);
			(*num_of_messages)++;
		}
	}
}