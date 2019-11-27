#include "sp.h"

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "fileService.h"
#include "include/HashSet/src/hash_set.h"

#define MAX_MESSLEN     102400
#define MAX_VSSETS      10
#define MAX_MEMBERS     100
#define MAX_PARTICIPANTS 100
#define MAX_CHATROOMS	100
#define RECREATE_FILES_IN_STARTUP	1

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

typedef struct Chatroom_t {
	char name[20];
	u_int32_t numOf_messages;
	Message messages[25];
	hash_set_st participants[5];
	u_int32_t num_of_participants[5];
	u_int32_t message_start_pointer;
} Chatroom;

typedef struct Session_t {
	u_int32_t server_id;
	Chatroom chatrooms[MAX_CHATROOMS];
	int connected_clients;
	int num_of_chatrooms;
	u_int32_t lamport_counter;
	u_int32_t membership[5];
} Session;

///////////////////////// Global Variables //////////////////////////////////////////////////////

Session current_session;
static char User[80];
static char Spread_name[80];

static char Private_group[MAX_GROUP_NAME];
static mailbox Mbox;

static int To_exit = 0;
static int log_level;

//////////////////////////   Declarations    ////////////////////////////////////////////////////

static void Read_message();
static void Usage(int argc, char *argv[]);
static void Bye();

static int initialize();

static int handle_disconnect(char *username);
static int handle_connect(char *message, u_int32_t size);
static int handle_join(char *message, int size);
static int handle_append(char *message, int msg_size);
static int handle_like_unlike(char *message, char event_type);
static int handle_history();
static int handle_membership_status();

static int parse(char *message, int size, int num_groups);
static int handle_server_update();
static int handle_participant_update(char *message, int msg_size);
static int handle_anti_entropy();
static int handle_membership_change();
static void update_chatroom_data(int chatroom_index, char *chatroom, u_int32_t payload_length, char *payload);

////////////////////////// Utility ////////////////////////////////////////

u_int32_t chksum(const void *str) 
{
  char *s = (char *)str;
  int len = strlen(s);
  int i;
  uint32_t c = 0;

  for (i = 0; i < len; ++i) {
    c = (c >> 1) + ((c & 1) << (32-1));
    c += s[i];
  }
  return (c);
}

//////////////////////////   Core Functions  ////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
	int ret;
	int mver, miver, pver;
	sp_time test_timeout;

	test_timeout.sec = 5;
	test_timeout.usec = 0;

	Usage(argc, argv);
	if (!SP_version(&mver, &miver, &pver)) {
		log_fatal("main: Illegal variables passed to SP_version()\n");
		Bye();
	}
	log_info("Spread library version is %d.%d.%d\n", mver, miver, pver);

	ret = SP_connect_timeout(Spread_name, User, 0, 1, &Mbox, Private_group, test_timeout);
	if (ret != ACCEPT_SESSION) {
		SP_error(ret);
		Bye();
	}
	log_info("User: connected to %s with private group %s\n", Spread_name, Private_group);

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
	int num_groups;
	int service_type;
	int16 mess_type;
	int endian_mismatch;
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
		log_debug("Received regular message.");
		parse(mess, ret, num_groups);

	} else if (Is_membership_mess(service_type)) {
		log_debug("Received membership message.");
		ret = SP_get_memb_info(mess, service_type, &memb_info);
		if (ret < 0) {
			log_fatal("BUG: membership message does not have valid body\n");
			SP_error(ret);
			exit(1);
		}
		if (Is_reg_memb_mess(service_type)) {
			handle_membership_change();

		} else if (Is_transition_mess(service_type)) {
			log_info("received TRANSITIONAL membership for group %s\n", sender);
		} else if (Is_caused_leave_mess(service_type)) {
			log_info("received membership message that left group %s\n", sender);
		} else
			log_info("received incorrecty membership message of type 0x%x\n", service_type);
	} else if (Is_reject_mess(service_type)) {
		log_info("REJECTED message from %s, of servicetype 0x%x messtype %d, (endian %d) to %d groups \n(%d bytes): %s\n", sender, service_type, mess_type,
				endian_mismatch, num_groups, ret, mess);
	} else
		log_error("received message of unknown message type 0x%x with ret %d\n", service_type, ret);

}

static void Usage(int argc, char *argv[]) {
	sprintf(User, "user");
	sprintf(Spread_name, "10330");
	if (argc != 2 && argc != 3) {
		printf("Usage: ./server [server_id 1-5] [log_level]\n");
		exit(0);
	}
	if(argc == 3)
	{
		log_level = atoi(argv[2]);
		log_set_level(log_level);
	}
	current_session.server_id = atoi(argv[1]);
}

static void Bye() {
	To_exit = 1;

	log_info("\nBye.\n");

	SP_disconnect(Mbox);

	exit(0);
}

static int parse(char *message, int size, int num_groups) {
	char type;
	memcpy(&type, message, 1);
    log_debug("Type is %c", type);
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
		handle_like_unlike(message, TYPE_LIKE);
		break;
	case TYPE_UNLIKE:
		handle_like_unlike(message, TYPE_UNLIKE);
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
	case TYPE_PARTICIPANT_UPDATE:
		handle_participant_update(message, size);
		break;
	default:
	log_error("invalid message type %c", type);
		break;
	}
	return 0;
}

static void create_chatroom_from_logs()
{

}

static int initialize() {
	int ret, i;
	int fds[5];
	char server_group_name[10];
	log_info("Server Initializing");
	create_log_files(current_session.server_id, 5, RECREATE_FILES_IN_STARTUP, fds);
	// for(i = 0; i < 5; i++)
	// 	E_attach_fd(fds[i], READ_FD, log_update_callback, 0, NULL, LOW_PRIORITY);

	create_chatroom_from_logs();
	
	current_session.connected_clients = 0;
	current_session.num_of_chatrooms = 0;
	current_session.lamport_counter = 0;
	for(i = 0; i < 5; i++)
		current_session.membership[i] = 1;

	log_info("Joining servers group");
	ret = SP_join(Mbox, "chat_servers");
	if (ret < 0)
		SP_error(ret);
	sprintf(server_group_name, "server%d", current_session.server_id);
	log_info("joining our public group = %s", server_group_name);
	ret = SP_join(Mbox, server_group_name);
	if (ret < 0)
		SP_error(ret);

	return 0;
}

//////////////////////////   User Event Handlers ////////////////////////////////////////////////////

static int send_to_servers(char type, char *payload, u_int32_t size)
{
	char* serversGroup = "chat_servers";
	char message[size + 5];
	message[0] = type;
	log_debug("sending message type %c to servers", type);
	memcpy(message + 1, &current_session.server_id, 4);
	memcpy(message + 5, payload, size);
	SP_multicast(Mbox, AGREED_MESS, serversGroup, 2, size + 5, message);
	return 0;
}

static int aggregate_participants(hash_set_st *agg, int index)
{
	int i, j, count = 0;
	hash_set_it *it;
	char *username;
	for(i = 0;i<5;i++)
	{
		it = it_init(&current_session.chatrooms[index].participants[i]);
		for(j = 0; j < current_session.chatrooms[index].num_of_participants[i];j++)
		{
			username = (char *)it_value(it);
			hash_set_insert(agg, username, strlen(username));
			count ++;
		}
	}
	log_debug("Aggregated participants for chatroom index %d - total participants = %d", index, count);
	return count;
}

static int send_chatroom_update_to_clients(char* chatroom, int index)
{
	int j;
	char chatroomGroup[30];
	char message[1400];
	char *username;
  	hash_set_st *participants = hash_set_init(chksum);
	hash_set_it *it;
	u_int32_t num_participants;
	int offset = 5;
	message[0] = TYPE_CLIENT_UPDATE;
	sprintf(chatroomGroup, "CHATROOM_%s_%d", chatroom, current_session.server_id);
	num_participants = 	aggregate_participants(participants, index);
	memcpy(message + 1, &num_participants, 4);
	it = it_init(participants);
	for(j = 0; j < num_participants;j++)
	{
		username = (char *)it_value(it);
		u_int32_t uname_size = strlen(username);
		memcpy(message + offset, &uname_size, 4);
		memcpy(message + offset + 4, username, uname_size);
		offset += (4 + uname_size);
	}

	memcpy(message + offset, &current_session.chatrooms[index].numOf_messages, 4);
	offset += 4;
	int i;
	for(i = 0;i < current_session.chatrooms[index].numOf_messages; i++)
	{
		u_int32_t message_size = strlen(current_session.chatrooms[index].messages[i].message);
		memcpy(message + offset, &message_size, 4);
		memcpy(message + offset + 4, current_session.chatrooms[index].messages[i].message, message_size);
		memcpy(message + offset + 4 + message_size, &current_session.chatrooms[index].messages[i].numOfLikes, 4);
		offset += (8 + message_size);
	}
	log_debug("sending client update for chatroom %s with %d participants and %d messages", chatroom, num_participants, current_session.chatrooms[index].numOf_messages);
	SP_multicast(Mbox, AGREED_MESS, chatroomGroup, 2, offset + 5, message);
	return 0;
}

static int handle_disconnect(char *username) {
	log_warn("handling disconnect by doing nothing");
	// TODO: ?
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
	log_info("Handling client connection %s by joining %s", username, group_name);
	ret = SP_join(Mbox, group_name);
	if (ret < 0)
		SP_error(ret);
	current_session.connected_clients++;	// TODO: not if the client is already connected
	return 0;
}

static int find_chatroom_index(char *chatroom)
{
	int i;
	for(i = 0; i < current_session.num_of_chatrooms; i++)
		if(! strcmp(current_session.chatrooms[i].name, chatroom))
		{
			log_debug("chatroom index for %s is %d", chatroom, i);
			return i;
		}
	log_debug("chatroom index for %s not found", chatroom);
	return -1;
}

static int create_new_chatroom(char *chatroom)
{
	int i;
	int index = current_session.num_of_chatrooms;
	log_info("Creating data structures for new chatroom %s", chatroom);
	current_session.num_of_chatrooms++;
	strcpy(current_session.chatrooms[index].name, chatroom);
	current_session.chatrooms[index].numOf_messages = 0;
	current_session.chatrooms[index].message_start_pointer = 0;
	for(i = 0; i < 5; i++){
		current_session.chatrooms[index].participants[i] = *hash_set_init(chksum);
		current_session.chatrooms[index].num_of_participants[i] = 0;
	}
	create_chatroom_file(current_session.server_id, chatroom, RECREATE_FILES_IN_STARTUP);
	return index;
}

static int send_participant_change_to_servers(char *chatroom, char *username, int index)
{
	char payload[1300];
	u_int32_t offset, nop, uname_size;
	u_int32_t chatroom_length = strlen(chatroom);
	hash_set_it *it;
	memcpy(payload, &chatroom_length, 4);
	memcpy(payload + 4, chatroom, chatroom_length);
	offset = 4 + chatroom_length;
	int i, j;
	for(i = 0; i < 5; i++)
	{
		nop = current_session.chatrooms[index].num_of_participants[i];
		log_debug("Server %d #participants %d", i+1, nop);
		memcpy(payload + offset, &nop, 4);
		offset += 4;
		it = it_init(&current_session.chatrooms[index].participants[i]);
		for(j = 0; j < nop;j++)
		{
			username = (char *)it_value(it);
			u_int32_t uname_size = strlen(username);
			memcpy(payload + offset, &uname_size, 4);
			memcpy(payload + offset + 4, username, uname_size);
			offset += (4 + uname_size);
		}
	}
	send_to_servers(TYPE_PARTICIPANT_UPDATE, payload, offset + 4 + chatroom_length);
	return 0;
}

static int handle_join(char *message, int size) {
	u_int32_t username_length, chatroom_length;
	char username[20], chatroom[20];
	int chatroom_index;
	memcpy(&username_length, message +1, 4);
	memcpy(username, message + 5, username_length);
	memcpy(&chatroom_length, message + 5 + username_length, 4);
	memcpy(chatroom, message + 9 + username_length, chatroom_length);
    chatroom[chatroom_length] = 0;
    username[username_length] = 0;
	log_debug("Handling client join request username = %s, chatroom length = %d, chatroom = %s", username, chatroom_length, chatroom);
	chatroom_index = find_chatroom_index(chatroom);
	if(chatroom_index == -1)
		chatroom_index = create_new_chatroom(chatroom);
	hash_set_insert(&current_session.chatrooms[chatroom_index].participants[current_session.server_id-1], username, strlen(username));
	current_session.chatrooms[chatroom_index].num_of_participants[current_session.server_id-1]++;
	send_participant_change_to_servers(chatroom, username, chatroom_index);
	send_chatroom_update_to_clients(chatroom, chatroom_index);
	return 0;
}

void createLogLine(u_int32_t server_id, logEvent e, char *line)
{
    sprintf(line, "%u~%s~%c~%s\n", e.lamportCounter, e.chatroom, e.eventType, e.payload);
	log_debug("log line is %s", line);
}

static int send_log_update_to_servers(u_int32_t server_id, u_int32_t line_length, char *line)
{
	u_int32_t size = 0;
    size += (8 + line_length);
	char payload[size];
	memcpy(payload, &server_id, 4);
	memcpy(payload + 4, &line_length, 4);
	memcpy(payload + 8, line, line_length);
	log_debug("sending log line to servers %s", line);
	send_to_servers(TYPE_SERVER_UPDATE, payload, size);
	return 0;
}

static int handle_append(char *message, int msg_size) {
	u_int32_t username_length, chatroom_length, payload_length, offset;
	u_int32_t size = 0;
	char username[20], chatroom[20], payload[80];
	int chatroom_index;
	logEvent e;
    memset(&e, 0, sizeof(e));
	memcpy(&username_length, message +1, 4);
	memcpy(username, message + 5, username_length);
	memcpy(&chatroom_length, message + 5 + username_length, 4);
	memcpy(chatroom, message + 9 + username_length, chatroom_length);
    chatroom[chatroom_length] = 0;
    username[username_length] = 0;
	log_debug("handling append message from %s in chatroom %s", username, chatroom);

	chatroom_index = find_chatroom_index(chatroom);
	if(chatroom_index == -1)
	{
		log_fatal("chatroom not found. This should not happen, right???????");
		return 0;
	}
	offset = username_length + chatroom_length + 9;
	memcpy(&payload_length, message + offset, 4);
	memcpy(payload, message + offset + 4, payload_length);
    payload[payload_length] = 0;
	e.eventType = TYPE_APPEND;
	e.lamportCounter = ++current_session.lamport_counter;
	char line[100];
	sprintf(line, "%s~%s", username, payload);
	memcpy(e.payload, line, payload_length + username_length + 1);
	memcpy(e.chatroom, chatroom, chatroom_length);
	log_debug("event log payload for append is %s",line);
    size += (13 + strlen(e.payload));
    char buffer[size];
	createLogLine(current_session.server_id, e, buffer);
	addEventToLogFile(current_session.server_id, buffer);
	send_log_update_to_servers(current_session.server_id, strlen(buffer), buffer);

	update_chatroom_data(chatroom_index, chatroom, payload_length, payload);
	
	return 0;
}

static void update_chatroom_data(int chatroom_index, char *chatroom, u_int32_t payload_length, char *payload){
	if(current_session.chatrooms[chatroom_index].numOf_messages < 25)
	{
		log_debug("chatroom %s has %d message(s)", chatroom, current_session.chatrooms[chatroom_index].numOf_messages);
		memcpy(current_session.chatrooms[chatroom_index].messages[current_session.chatrooms[chatroom_index].numOf_messages++].message, payload, payload_length);
	}
	else
	{
		Message m = current_session.chatrooms[chatroom_index].messages[current_session.chatrooms[chatroom_index].message_start_pointer];
		log_debug("moving message #%d, %d to file", m.serverID, m.lamportCounter);
		addMessageToChatroomFile(current_session.server_id, chatroom, m);
		memcpy(current_session.chatrooms[chatroom_index].messages[current_session.chatrooms[chatroom_index].message_start_pointer].message, payload, payload_length);
		current_session.chatrooms[chatroom_index].message_start_pointer++;
		if(current_session.chatrooms[chatroom_index].message_start_pointer == 25)
			current_session.chatrooms[chatroom_index].message_start_pointer = 0;
	}
	send_chatroom_update_to_clients(chatroom, chatroom_index);
}
static int handle_like_unlike(char *message, char event_type) {

	u_int32_t username_length, chatroom_length, offset;
	u_int32_t size = 0;
	logEvent e;
	int chatroom_index;
	char username[20], chatroom[20];
	u_int32_t pid, counter;
	char line[100];
	memcpy(&username_length, message +1, 4);
	memcpy(username, message + 5, username_length);
	memcpy(&chatroom_length, message + 5 + username_length, 4);
	memcpy(chatroom, message + 9 + username_length, chatroom_length);
	chatroom_index = find_chatroom_index(chatroom);
	if(chatroom_index == -1)
	{
		log_fatal("chatroom not found. This should not happen, right???????");
		return 0;
	}
	offset = username_length + chatroom_length + 9;
	memcpy(&pid, message + offset, 4);
	memcpy(&counter, message + offset + 4, 4);
	log_debug("handling like/unlike for message #%d, %d from %s", pid, counter, username);
	e.eventType = event_type;
	e.lamportCounter = ++current_session.lamport_counter;
	
	sprintf(line, "%s~%d~%d", username, pid, counter);
	log_debug("like/unlike log payload is: %s", line);
	memcpy(e.payload, line, strlen(line));
	memcpy(e.chatroom, chatroom, chatroom_length);
    size += (13 + strlen(e.payload));
    char buffer[size];
	createLogLine(current_session.server_id, e, buffer);
	addEventToLogFile(current_session.server_id, buffer);
	send_log_update_to_servers(current_session.server_id, strlen(buffer), buffer);

	send_chatroom_update_to_clients(chatroom, chatroom_index);
	return 0;
}

static int handle_history() {
	// TODO: ...
	return 0;
}
static int handle_membership_status() {
	// TODO: ...
	return 0;
}

//////////////////////////   Server Event Handlers ////////////////////////////////////////////////////

static int handle_server_update(char *messsage, int size) {
	u_int32_t sender_id, server_id, log_length, chatroom_index;
	logEvent e;
	char username[20], message_text[80];
	memcpy(&sender_id, messsage + 1, 4);
	memcpy(&server_id, messsage + 5, 4);
	memcpy(&log_length, messsage + 9, 4);
	if(server_id == current_session.server_id)
		return 0;
	char line[log_length];
	memcpy(&line, messsage + 13, log_length);
	log_debug("handling server update (of server %d) from server %d. update line is: %s", server_id, sender_id, line);
	addEventToLogFile(server_id, line);
	parseLineInLogFile(line, &e);
	chatroom_index = find_chatroom_index(e.chatroom);
	switch(e.eventType)
	{
		case TYPE_APPEND:
			sscanf(e.payload, "%[^\n\t~]~%[^\n\t]", username, message_text);
			log_debug("parsing append data {%s} from message. username = %s, message text = %s, chatroom = %s", e.payload, username, message_text, e.chatroom);
			update_chatroom_data(chatroom_index, e.chatroom, strlen(message_text), message_text);
			break;
		case TYPE_LIKE:
			break;
		case TYPE_UNLIKE:
			break;
		default:
			log_error("Invalid event type %c", e.eventType);
			break;
	}
	return 0;
}

static int handle_anti_entropy() {
	
	return 0;
}

static int handle_membership_change() {

	return 0;
}

static int handle_participant_update(char *message, int msg_size) {
	u_int32_t server_id, num_of_participants;
	u_int32_t chatroom_length, offset, uname_length;
	
	int chatroom_index, i, p, flag = 1;
	char username[20], chatroom[20];
	memcpy(&server_id, message + 1, 4);
	memcpy(&chatroom_length, message + 5, 4);
	memcpy(&chatroom, message + 9, chatroom_length);
    if(server_id == current_session.server_id)
		return 0;
	chatroom_index = find_chatroom_index(chatroom);
	offset = 9 + chatroom_length;
	for (i=0;i<5;i++)
	{
		if(i != server_id && current_session.membership[i])
			flag = 0;
		else{
			flag = 1;
			hash_set_clear(&current_session.chatrooms[chatroom_index].participants[i]);
			current_session.chatrooms[chatroom_index].num_of_participants[i] = 0;
		}
		memcpy(&num_of_participants, message + offset, 4);
		offset += 4;
		for(p = 0;p < num_of_participants; p++)
		{
			memcpy(&uname_length, message + offset, 4);
			memcpy(username, message + offset + 4, uname_length);
			offset += (4 + uname_length);
			if(flag){
				hash_set_insert(&current_session.chatrooms[chatroom_index].participants[i], username, strlen(username));
				current_session.chatrooms[chatroom_index].num_of_participants[i]++;
			}
		}
	}
    return 0;
}
