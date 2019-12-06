#ifndef FILESERVICE_H
#define FILESERVICE_H

/////////////////////////////////////////////////////////////////////////////////////
//
//	This file contains the utilities to read/write from log/chatroom files.
//
////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "chat_include.h"

FILE ** log_files; 

void get_chatroom_file_name(u_int32_t me, char *chatroom, char *filename);

void create_log_files(u_int32_t me, u_int32_t num_of_servers, int recreate, int *fds);

void create_chatroom_file(u_int32_t me, char *chatroom_name, int recreate);

void addEventToLogFile(u_int32_t server_id, char *line);

void parseLineInLogFile(char *line, logEvent *e);

void addMessageToChatroomFile(u_int32_t me, char *chatroom, Message m);

void parseLineInMessagesFile(char *line, Message *m);

void get_logs_newer_than(u_int32_t server_id, u_int32_t lamport_counter, u_int32_t *length, logEvent *logs);

void retrieve_chatroom_history(u_int32_t me, char *chatroom, u_int32_t *num_of_messages, Message *mesages);

void retrieve_line_from_logs(logEvent *e, u_int32_t *available_data, u_int32_t num_servers, u_int32_t *last_processed_counters);

#endif
