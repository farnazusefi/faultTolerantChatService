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
#define MAX_CHATROOMS	100
#define DEBUG_MODE		1

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
	TYPE_ANTY_ENTROPY = 'e'
};

typedef struct Chatroom_t {
	u_int32_t numOf_messages;
	Message messages[25];
	char *participants[MAX_PARTICIPANTS];
} Chatroom;

typedef struct Session_t {
	u_int32_t server_id;
	Chatroom chatrooms[MAX_CHATROOMS];
//	char connected_clients[100][20];
	int connected_clients;

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

static int handle_disconnect(char *username);
static int handle_connect(char *message, u_int32_t size);
static int handle_join(char *message, int size);
static int handle_append(char *message, int size);
static int handle_like(int line_number);
static int handle_unlike(int line_number);
static int handle_history();
static int handle_membership_status();

static int parse(char *message, int size, int num_groups, char **groups);
static int handle_server_update();
static int handle_anti_entropy();
static int handle_membership_change();

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

	E_attach_fd(Mbox, READ_FD, Read_message, 0, NULL, HIGH_PRIORITY);

	E_handle_events();

	return (0);
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
			handle_membership_message(num_groups, &target_groups, &memb_info);

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
	if (argc != 2) {
		printf("Usage: ./server [server_id 1-5]");
		exit(0);
	}
	current_session.server_id = atoi(argv[1]);

}

static void Bye() {
	To_exit = 1;

	printf("\nBye.\n");

	SP_disconnect(Mbox);

	exit(0);
}

static int parse(char *message, int size, int num_groups, char **groups) {
	char type;
	memcpy(&type, message, 1);
	switch (type) {
	case TYPE_CONNECT:
		handle_connect(message, size);
		break;
	case TYPE_JOIN:
		handle_join(message, size);
		break;
	case TYPE_APPEND:
		handle_append(message, size);
		break;
	case TYPE_HISTORY:
		handle_history(message, size);
		break;
	case TYPE_LIKE:
		handle_like(message, size);
		break;
	case TYPE_UNLIKE:
		handle_unlike(message, size);
		break;
	case TYPE_MEMBERSHIP_STATUS:
		handle_membership_status();
		break;
	case TYPE_ANTY_ENTROPY:
		handle_anti_entropy(message, size);
		break;
	case TYPE_SERVER_UPDATE:
		handle_server_update(message, size);
		break;
	default:
		break;
	}
	return 0;
}

static int initialize() {
	int ret;
	create_log_files(current_session.server_id, 5, DEBUG_MODE);
	create_chatroom_from_logs();
	int i,j;
	for(i=0; i < MAX_CHATROOMS; i++)
	{
		for(j=0;j < MAX_PARTICIPANTS; j++)
		{
			current_session.chatrooms[i].participants = (char*) malloc(20);
		}
		current_session.chatrooms[i].numOf_messages = 0;
	}
	current_session.connected_clients = 0;
	log_info("Joining servers group");
	ret = SP_join(Mbox, "chat_servers");
	if (ret < 0)
		SP_error(ret);
	return 0;
}

//////////////////////////   User Event Handlers ////////////////////////////////////////////////////

static int handle_disconnect(char *username) {

	return 0;
}

static int handle_connect(char *message, u_int32_t size) {
	u_int32_t username_size;
	int ret;
	char username[20];
	char group_name[30];
	memcpy(&username_size, message +1, 4);
	memcpy(username, message + 5, username_size);
	sprintf(group_name, "%s_%d", username, current_session.server_id);
	ret = SP_join(Mbox, group_name);
	if (ret < 0)
		SP_error(ret);
	current_session.connected_clients++;	// TODO: not if the client is already connected
	return 0;
}

static int find_chatroom_index(char *chatroom)
{

}

static int handle_join(char *message, int size) {
	u_int32_t username_length, chatroom_length;
	char username[20], chatroom[20];
	memcpy(&username_length, message +1, 4);
	memcpy(username, message + 5, username_length);
	memcpy(&chatroom_length, message + 5 + username_length, 4);
	memcpy(chatroom, message + 9 + username_length, chatroom_length);
	int chatroom_index = find_chatroom_index(chatroom);


	return 0;
}

static int handle_append(char *message, int size) {

	return 0;
}

static int handle_like(int line_number) {

	return 0;
}

static int handle_unlike(int line_number) {

	return 0;
}

static int handle_history() {

	return 0;
}
static int handle_membership_status() {

	return 0;
}

//////////////////////////   Server Event Handlers ////////////////////////////////////////////////////

static int handle_server_update() {

	return 0;
}

static int handle_anti_entropy() {

	return 0;
}

static int handle_membership_change() {

	return 0;
}
