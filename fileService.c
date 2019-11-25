#include "fileService.h"

void get_chatroom_file_name(u_int32_t me, char *chatroom, char *filename)
{
    sprintf(filename, "%d_%s.chatroom", me, chatroom);
}

void create_log_files(u_int32_t me, u_int32_t num_of_servers, int recreate)
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
    sscanf(line, "%d~%c~%[^\t\n~] ~ %s", &e->lamportCounter, &e->eventType, e->payload, e->additionalInfo);
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

Message parseLineInMessagesFile(char *line, Message *m)
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

}
