#ifndef _CHAT_INCLUDE_H
#define _CHAT_INCLUDE_H

#include "sp.h"
#define _GNU_SOURCE

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>

#include "log.h"

#define MAX_MESSLEN 102400
#define MAX_VSSETS 10
#define MAX_MEMBERS 100
#define MAX_PARTICIPANTS 100
#define MAX_CHATROOMS 100
#define RECREATE_FILES_IN_STARTUP 0
#define NUM_SERVERS 5
#define MAX_HISTORY_MESSAGES 100

#define int32u unsigned int


enum MessageType
{
	TYPE_LOGIN = 'u',
	TYPE_CONNECT = 'c',
	TYPE_APPEND = 'a',
	TYPE_JOIN = 'j',
	TYPE_LIKE = 'l',
	TYPE_UNLIKE = 'r',
	TYPE_HISTORY = 'h',
	TYPE_HISTORY_RESPONSE = 'H',
	TYPE_MEMBERSHIP_STATUS = 'v',
	TYPE_CLIENT_UPDATE = 'i',
	TYPE_MEMBERSHIP_STATUS_RESPONSE = 'm',
	TYPE_SERVER_UPDATE = 's',
	TYPE_ANTY_ENTROPY = 'e',
	TYPE_PARTICIPANT_UPDATE = 'p'
};

enum State
{
	STATE_PRIMARY,
	STATE_RECONCILING
};

static char User[80];
static char Spread_name[80];

static char Private_group[MAX_GROUP_NAME];
static mailbox Mbox;

static int To_exit = 0;
static int log_level;

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
} Message;

// A utility function that prints the hex value of a char array - used for debugging
// void print_hex(const char *string, int len)
// {
//         unsigned char *p = (unsigned char *) string;
// 		int i;
//         for (i=0; i < len; ++i) {
//                 if (! (i % 16) && i)
//                         printf("\n");

//                 printf("0x%02x ", p[i]);
//         }
//         printf("\n\n");
// 		fflush(stdout);
// }

#endif