#include "sp.h"

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "fileService.h"

#define int32u unsigned int

#define MAX_MESSLEN     102400
#define MAX_VSSETS      10
#define MAX_MEMBERS     100
#define MAX_PARTICIPANTS 100
///////////////////////// Data Structures   //////////////////////////////////////////////////////

enum MessageType {
	TYPE_LOGIN = 'u',
	TYPE_CONNECT = 'c',
	TYPE_APPEND = 'a',
	TYPE_JOIN = 'j',
	TYPE_LIKE = 'l',
	TYPE_UNLIKE = 'r',
	TYPE_HISTORY = 'h',
	TYPE_MEMBERSHIP_STATUS = 'v',
	TYPE_CLIENT_UPDATE = 'i',
	TYPE_MEMBERSHIP_STATUS_RESPONSE = 'm',
	TYPE_SERVER_UPDATE = 's',
	TYPE_ANTY_ENTROPY = 'e',
	TYPE_PARTICIPANT_UPDATE = 'p'
};

typedef struct Session_t {
	u_int32_t logged_in;
	u_int32_t connected_server;
	u_int32_t is_connected;
	u_int32_t is_joined;

	u_int32_t username;
	u_int32_t chatroom;

	Message messages[25];
	u_int32_t numOfMessages;

	char *listOfParticipants[MAX_PARTICIPANTS];
	u_int32_t numOfParticipants;

} Session;

///////////////////////// Global Variables //////////////////////////////////////////////////////

Session current_session;
static char User[80];
static char Spread_name[80];

static char Private_group[MAX_GROUP_NAME];
static mailbox Mbox;
static int Num_sent;

static int To_exit = 0;

//////////////////////////   Declarations    ////////////////////////////////////////////////////

static void Print_menu();
static void User_command();
static void Read_message();
static void Usage(int argc, char *argv[]);
static void Print_help();
static void Bye();

static int initialize();

static int handle_login(char *username);
static int handle_connect(int server_id);
static int join(char *chatroom);
static int handle_append(char *message, int size);
static int handle_like(int line_number);
static int handle_unlike(int line_number);
static int handle_history();
static int handle_membership_status();

static int parse(char *message, int size, int num_groups, char **groups);
static int handle_membership_message(char *sender, int num_groups, struct membership_info *mem_info, int service_type);
static int handle_update_response(char *message, int size, int num_groups, char **groups);
static int handle_membership_status_response(char *message, int size, int num_groups, char **groups);

//////////////////////////   Core Functions  ////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
	int ret;
	int mver, miver, pver;
	sp_time test_timeout;

	test_timeout.sec = 5;
	test_timeout.usec = 0;

	Usage(argc, argv);
	if (!SP_version(&mver, &miver, &pver)) {
		printf("main: Illegal variables passed to SP_version()\n");
		Bye();
	}
	printf("Spread library version is %d.%d.%d\n", mver, miver, pver);

	ret = SP_connect_timeout(Spread_name, User, 0, 1, &Mbox, Private_group, test_timeout);
	if (ret != ACCEPT_SESSION) {
		SP_error(ret);
		Bye();
	}
	printf("User: connected to %s with private group %s\n", Spread_name, Private_group);

	E_init();
	initialize();

	E_attach_fd(0, READ_FD, User_command, 0, NULL, LOW_PRIORITY);

	E_attach_fd(Mbox, READ_FD, Read_message, 0, NULL, HIGH_PRIORITY);

	Print_menu();

	printf("\nUser> ");
	fflush(stdout);

	Num_sent = 0;

	E_handle_events();

	return (0);
}

static void User_command() {
	char command[130];  //
	char mess[MAX_MESSLEN];  //
	char argument[80]; //
	unsigned int mess_len; //
	int ret; //
	int i; //
	int server_id, line_id;

	for (i = 0; i < sizeof(command); i++)
		command[i] = 0;
	if (fgets(command, 130, stdin) == NULL)
		Bye();

	switch (command[0]) {
	case TYPE_LOGIN:	// login with username
		ret = sscanf(&command[2], "%s", argument);
		if (ret < 1) {
			printf(" invalid username \n");
			break;
		}
		handle_login(argument);
		break;

	case TYPE_CONNECT:	// connect to server [1-5]
		ret = sscanf(&command[2], "%s", argument);
		if (ret < 1) {
			printf(" invalid server id \n");
			break;
		}
		if (current_session.logged_in) {
			server_id = atoi(argument);
			handle_connect(server_id);
		} else {
			printf(" not logged in yet! \n");
		}
		break;

	case TYPE_JOIN:	// join a chatroom
		ret = sscanf(&command[2], "%s", argument);
		if (ret < 1) {
			printf(" invalid chatroom \n");
			break;
		}
		if (current_session.logged_in) {
			if (current_session.connected_server)
				join(argument);
			else
				printf(" not connected to any server! \n");
		} else {
			printf(" not logged in yet! \n");
		}
		break;

	case TYPE_APPEND:	// append to chatroom
		if (!current_session.logged_in) {
			printf(" not logged in yet! \n");
			break;
		}
		if (!current_session.connected_server) {
			printf(" not connected to any server \n");
			break;
		}
		if (!current_session.is_joined) {
			printf(" not joined any chatroom yet! \n");
			break;
		}
		printf("enter message: ");
		if (fgets(mess, 200, stdin) == NULL)
			Bye();
		mess_len = strlen(mess);
		handle_append(mess, mess_len);
		break;

	case TYPE_LIKE:	// like a message
		ret = sscanf(&command[2], "%s", argument);
		if (ret < 1) {
			printf(" invalid chatroom \n");
			break;
		}
		if (!current_session.logged_in) {
			printf(" not logged in yet! \n");
			break;
		}
		if (!current_session.connected_server) {
			printf(" not connected to any server \n");
			break;
		}
		if (!current_session.is_joined) {
			printf(" not joined any chatroom yet! \n");
			break;
		}
		line_id = atoi(argument);
		handle_like(line_id);
		break;

	case TYPE_UNLIKE:		// unlike a message
		ret = sscanf(&command[2], "%s", argument);
		if (ret < 1) {
			printf(" invalid chatroom \n");
			break;
		}
		if (!current_session.logged_in) {
			printf(" not logged in yet! \n");
			break;
		}
		if (!current_session.connected_server) {
			printf(" not connected to any server \n");
			break;
		}
		if (!current_session.is_joined) {
			printf(" not joined any chatroom yet! \n");
			break;
		}
		line_id = atoi(argument);
		handle_unlike(line_id);
		break;

	case TYPE_HISTORY:		// chatroom history
		if (!current_session.logged_in) {
			printf(" not logged in yet! \n");
			break;
		}
		if (!current_session.connected_server) {
			printf(" not connected to any server \n");
			break;
		}
		if (!current_session.is_joined) {
			printf(" not joined any chatroom yet! \n");
			break;
		}
		handle_history();
		break;

	case TYPE_MEMBERSHIP_STATUS:
		if (!current_session.logged_in) {
			printf(" not logged in yet! \n");
			break;
		}
		if (!current_session.connected_server) {
			printf(" not connected to any server \n");
			break;
		}

		handle_membership_status();
		break;

	case 'q':
		Bye();
		break;

	default:
		printf("\nUnknown command\n");
		Print_menu();

		break;
	}
	printf("\nUser> ");
	fflush(stdout);

}

static void Print_menu() {
	printf("\n");
	printf("==========\n");
	printf("User Menu:\n");
	printf("----------\n");
	printf("\n");
	printf("\tu <username> -- login with username\n");
	printf("\tc <server index [1-5]> -- connect to server [1-5]\n");
	printf("\tj <chatroom> -- join a chatroom\n");
	printf("\n");
	printf("\ta -- send a message to chatroom\n");
	printf("\tm <group> -- send a multiline message to group. Terminate with empty line\n");
	printf("\tl <line number> -- like a message\n");
	printf("\tr <line number> -- un-like a message\n");
	printf("\n");
	printf("\th -- print chatroom history \n");
	printf("\tv -- view server membership status\n");
	printf("\n");
	printf("\tq -- quit\n");
	fflush(stdout);
}

static void Read_message() {

	static char mess[MAX_MESSLEN];
	char sender[MAX_GROUP_NAME];
	char target_groups[MAX_MEMBERS][MAX_GROUP_NAME];
	membership_info memb_info;
	vs_set_info vssets[MAX_VSSETS];
	unsigned int my_vsset_index;
	int num_vs_sets;
	char members[MAX_MEMBERS][MAX_GROUP_NAME];
	int num_groups;
	int service_type;
	int16 mess_type;
	int endian_mismatch;
	int i, j;
	int ret;

	service_type = 0;

	ret = SP_receive(Mbox, &service_type, sender, 100, &num_groups, target_groups, &mess_type, &endian_mismatch, sizeof(mess), mess);
	if (ret < 0) {
		if ((ret == GROUPS_TOO_SHORT) || (ret == BUFFER_TOO_SHORT)) {
			service_type = DROP_RECV;
			printf("\n========Buffers or Groups too Short=======\n");
			ret = SP_receive(Mbox, &service_type, sender, MAX_MEMBERS, &num_groups, target_groups, &mess_type, &endian_mismatch, sizeof(mess), mess);
		}
	}
	if (ret < 0) {
		if (!To_exit) {
			SP_error(ret);
			printf("\n============================\n");
			printf("\nBye.\n");
		}
		exit(0);
	}
	if (Is_regular_mess(service_type)) {
		parse(mess, ret, num_groups, &target_groups);

	} else if (Is_membership_mess(service_type)) {
		ret = SP_get_memb_info(mess, service_type, &memb_info);
		if (ret < 0) {
			printf("BUG: membership message does not have valid body\n");
			SP_error(ret);
			exit(1);
		}
		if (Is_reg_memb_mess(service_type)) {
			handle_membership_message(sender, num_groups, &memb_info, service_type);

		} else if (Is_transition_mess(service_type)) {
			printf("received TRANSITIONAL membership for group %s\n", sender);
		} else if (Is_caused_leave_mess(service_type)) {
			printf("received membership message that left group %s\n", sender);
		} else
			printf("received incorrecty membership message of type 0x%x\n", service_type);
	} else if (Is_reject_mess(service_type)) {
		printf("REJECTED message from %s, of servicetype 0x%x messtype %d, (endian %d) to %d groups \n(%d bytes): %s\n", sender, service_type, mess_type,
				endian_mismatch, num_groups, ret, mess);
	} else
		printf("received message of unknown message type 0x%x with ret %d\n", service_type, ret);

	printf("\n");
	printf("User> ");
	fflush(stdout);
}

static void Usage(int argc, char *argv[]) {
	sprintf(User, "user");
	sprintf(Spread_name, "225.1.3.30:10330");
	while (--argc > 0) {
		argv++;

		if (!strncmp(*argv, "-u", 2)) {
			if (argc < 2)
				Print_help();
			strcpy(User, argv[1]);
			argc--;
			argv++;
		} else if (!strncmp(*argv, "-r", 2)) {
			strcpy(User, "");
		} else if (!strncmp(*argv, "-s", 2)) {
			if (argc < 2)
				Print_help();
			strcpy(Spread_name, argv[1]);
			argc--;
			argv++;
		} else {
			Print_help();
		}
	}
}
static void Print_help() {
	printf("Usage: spuser\n%s\n%s\n%s\n", "\t[-u <user name>]  : unique (in this machine) user name", "\t[-s <address>]    : either port or port@machine",
			"\t[-r ]    : use random user name");
	exit(0);
}
static void Bye() {
	To_exit = 1;

	printf("\nBye.\n");

	SP_disconnect(Mbox);

	exit(0);
}

static int initialize() {
	current_session.logged_in = 0;
	current_session.is_joined = 0;
	current_session.connected_server = 0;
	current_session.is_connected = 0;
	current_session.numOfMessages = 0;
	int i;
	for (i = 0; i < MAX_PARTICIPANTS; i++){
		current_session.listOfParticipants [i] = (char*) malloc(20);
	}
	return 0;
}

//////////////////////////   User Event Handlers ////////////////////////////////////////////////////
static int sendToServer(char type, char *payload, u_int32_t size) {
	char serverPrivateGroup[80], message[size + 1];
	u_int32_t username_length = (u_int32_t) strlen(current_session.username)
	message[0] = type;
	memcpy(message + 1, &username_length, 4);
	memcpy(message + 5, current_session.username, username_length);
	memcpy(message + 5 + username_length, payload, size);
	sprintf(serverPrivateGroup, "server%d", server_id);
	SP_multicast(Mbox, AGREED_MESS, serverPrivateGroup, 2, size + username_length + 5, message);
	return 0;

}

static int sendConnectionRequestToServer() {
	sendToServer(TYPE_CONNECT, "", 0);
	return 0;

}

static int sendJoinRequestToServer(char *chatroom) {
	int length = strlen(chatroom);
	char payload[length + 4];
	memcpy(payload, &length, 4);
	memcpy(payload + 4, chatroom, length);
	sendToServer(TYPE_JOIN, payload, length + 4);
	return 0;

}

static int sendAppendRequestToServer(char *chatroom, char *message) {
	int c_length = strlen(chatroom);
	int m_length = strlen(message);
	char payload[c_length + m_length + 8];
	memcpy(payload, &c_length, 4);
	memcpy(payload + 4, chatroom, c_length);
	memcpy(payload + 4 + c_length, &m_length, 4);
	memcpy(payload + 8 + c_length, message, m_length);
	sendToServer(TYPE_APPEND, payload, c_length + m_length + 8);
	return 0;

}

static int sendLikeRequestToServer(u_int32_t pid, u_int32_t counter, char *chatroom, char type) {
	int c_length = strlen(chatroom);
	char payload[c_length + 8];
	memcpy(payload, &c_length, 4);
	memcpy(payload + 4, chatroom, c_length);
	memcpy(payload + 4 + c_length, &pid, 4);
	memcpy(payload + 8 + c_length, &counter, 4);
	sendToServer(type, payload, c_length + 8);
	return 0;

}

static int sendHistoryRequestToServer(char *chatroom) {
	int length = strlen(chatroom);
	char payload[length + 4];
	memcpy(payload, &length, 4);
	memcpy(payload + 4, chatroom, length);
	sendToServer(TYPE_HISTORY, payload, length + 4);
	return 0;
}

static int sendMembershipRequestToServer() {
	sendToServer(TYPE_MEMBERSHIP_STATUS, "", 0);
	return 0;
}

static void lineNumberToLTS(int line_number, u_int32_t *pid, u_int32_t *counter) {
	*pid = current_session.messages[line_number - 1].serverID;
	*counter = current_session.messages[line_number - 1].lamportCounter;
}

static int handle_login(char *username) {
	memcpy(current_session.username, username, strlen(username));
	current_session.logged_in = 1;
	return 0;
}

static int handle_connect(int server_id) {
	int ret;
	char server_group_name[80];
	if (server_id != current_session.connected_server || !current_session.is_connected) {
		if (current_session.is_connected) {
			log_info("disconnecting from %d", current_session.connected_server);
			sprintf(server_group_name, "%s_%d", current_session.username, current_session.connected_server);
			ret = SP_leave(Mbox, server_group_name);
			if (ret < 0)
				SP_error(ret);
		}
		log_info("requesting to join %d", server_id);
		ret = SP_join(Mbox, server_group_name);
		if (ret < 0)
			SP_error(ret);

		sendConnectionRequestToServer();
		current_session.connected_server = server_id;
		return 0;
	}
	log_warn("Already connected to server %d", server_id);
	// TODO: When you receive server's membership on this group, set is_connected = true

}

static int join(char *chatroom) {
	char chatroom_group_name[80];
	int ret;
	sprintf(chatroom_group_name, "#%s_%d", chatroom, current_session.connected_server);
	ret = SP_join(Mbox, chatroom_group_name);
	if (ret < 0)
		SP_error(ret);
	sendJoinRequestToServer(chatroom);
	return 0;
}

static int handle_append(char *message, int size) {
	sendAppendRequestToServer(current_session.chatroom, message);
	return 0;
}

static int handle_like(int line_number) {
	u_int32_t pid, counter;
	lineNumberToLTS(line_number, &pid, &counter);
	sendLikeRequestToServer(pid, counter, current_session.chatroom, TYPE_LIKE)
	return 0;
}

static int handle_unlike(int line_number) {
	u_int32_t pid, counter;
	lineNumberToLTS(line_number, &pid, &counter);
	sendLikeRequestToServer(pid, counter, current_session.chatroom, TYPE_UNLIKE)
	return 0;
}

static int handle_history() {
	sendHistoryRequestToServer(current_session.chatroom);
	return 0;
}
static int handle_membership_status() {
	sendMembershipRequestToServer();
	return 0;
}

//////////////////////////   Server Event Handlers ////////////////////////////////////////////////////

static int parse(char *message, int size, int num_groups, char **groups) {
	char type = message[0];
	switch (type) {
	case TYPE_CLIENT_UPDATE:
		handle_update_response(message, size, num_groups, groups);
		break;
	case TYPE_MEMBERSHIP_STATUS_RESPONSE:
		handle_membership_status_response(message, size, num_groups, groups)
		break;
	default:
		break;
	}
	return 0;
}

static void display_disconnection_to_user()
{
	printf("-----------------------\n");
	printf("You are disconnected from the server. Try connecting again.\n");
	Print_menu();
}

static int handle_membership_message(char *sender, int num_groups, struct membership_info *mem_info, int service_type) {

	char username[20];
	int serverID;
	sscanf(sender, "%s_%d", username, &serverID);
	int ret = strcmp(username, current_session.username);
	if (ret == 0)
	{
		// this is the group between me and the server
		if(Is_caused_leave_mess( service_type ) || Is_caused_disconnect_mess(service_type))
		{
			// server is disconnected
			current_session.connected_server = 0;
			current_session.is_connected = 0;
			current_session.is_joined = 0;
			display_disconnection_to_user();
		}
		if(Is_caused_join_mess(service_type))
		{
			current_session.is_connected = 1;
		}
	}


	return 0;
}

static void displayMessages(){
	printf("\n");
		printf("==========\n");
		printf("room: %s\n", current_session.chatroom);
		printf("currentParticipants: ");
		int i;
		for (i=0; i< current_session.numOfParticipants; i++){
			printf("%s,",current_session.listOfParticipants[i]);
		}
		printf("\n");
		printf("----------\n");
		int i;
		for (i = 0; i < current_session.numOfMessages; i++) {
			printf("%d. %s: %s \t likes: %d\n", current_session.messages[i].userName,
					current_session.messages[i].message, current_session.messages[i].numOfLikes);
		}
		printf("----------\n");
		fflush(stdout);
		Print_menu();
}
static int handle_update_response(char *message, int size, int num_groups, char **groups) {

	memcpy(&current_session.numOfParticipants, message + 1, 4);
	u_int32_t participantsList[current_session.numOfParticipants];
	int offset = 5;
	u_int32_t username_size;
	int i;
	for (i = 0; i < current_session.numOfParticipants; i++) {
		memcpy(&username_size, message + offset, 4);
		memcpy(&current_session.listOfParticipants[i], message + offset + 4, username_size);
		offset += (4+username_size);
	}
	u_int32_t numOfMessages;
	memccpy(&numOfMessages, message + offset + (4 * current_session.numOfParticipants), 4);
	int pointer = 0;
	int i;
	for (i = 0; i < numOfMessages; i++){
		memcpy(&current_session.messages[i].serverID, message + offset + pointer, 4);
		memcpy(&current_session.messages[i].lamportCounter, message + offset + pointer + 4, 4);
		u_int32_t messageSize ;
		memcpy(&messageSize, message + offset + pointer + 8, 4);
		memcpy(&current_session.messages[i].message, message + offset + pointer +12, messageSize);
		memcpy(&current_session.messages[i].numOfLikes, message + offset + pointer + 16 + messageSize, 4);
		pointer += (16 + messageSize);
	}
	current_session.numOfMessages = numOfMessages;
	displayMessages();
	return 0;
}

static void displayMembershipStatus(u_int32_t *list, u_int32_t size) {
	printf("\n");
	printf("==========\n");
	printf("Servers in the membership group of the connected server are:\n");
	printf("----------\n");
	int i;
	for (i = 0; i < size; i++) {
		printf("Server %d\n", list[i]);
	}
	printf("----------\n");
	fflush(stdout);
	Print_menu();
}
static int handle_membership_status_response(char *message, int size, int num_groups, char **groups) {

	u_int32_t numOfMembers;
	memcpy(&numOfMembers, message + 1, 4);
	u_int32_t membersList[numOfMembers];
	int i;
	for (i = 0; i < numOfMembers; i++) {
		memcpy(&membersList[i], message + 5 + (4 * i), 4);
	}
	displayMembershipStatus(membersList, numOfMembers);
	return 0;
}
