#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"

typedef struct {
	u_int32_t lamportCounter;
	char eventType;
	char payload[100];
	char additionalInfo[20];
	char chatroom[20];

} logEvent;

typedef struct {
	u_int32_t serverID;
	u_int32_t lamportCounter;
	char userName[20];
	char message[80];
	char additionalInfo[200];
	u_int32_t numOfLikes;
	//hashset_t likers;
} Message;

FILE ** log_files; 

void get_chatroom_file_name(u_int32_t me, char *chatroom, char *filename);

void create_log_files(u_int32_t me, u_int32_t num_of_servers, int recreate, int *fds);

void create_chatroom_file(u_int32_t me, char *chatroom_name, int recreate);

void addEventToLogFile(u_int32_t server_id, char *line);

void refineLogFile(u_int32_t lc);

void parseLineInLogFile(char *line, logEvent *e);

void addMessageToChatroomFile(u_int32_t me, char *chatroom, Message m);

void parseLineInMessagesFile(char *line, Message *m);

void addLikerToMessage(char *chatroom, u_int32_t server_id, u_int32_t lamportCounter, char *userName);

void removeLikerOfMessage(char *chatroom, u_int32_t server_id, u_int32_t lamportCounter, char *userName);

int get_last_messages(char * chatroom, Message* output, u_int32_t num_of_messages);

void get_logs_newer_than(u_int32_t server_id, u_int32_t lamport_counter, u_int32_t *length, logEvent *logs);