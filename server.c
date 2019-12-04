#include "sp.h"
#define _GNU_SOURCE
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>

#include "log.h"
#include "fileService.h"
#include "list.h"

#include "include/HashSet/src/hash_set.h"
#include "include/c_hashmap/hashmap.h"

#define MAX_MESSLEN 102400
#define MAX_VSSETS 10
#define MAX_MEMBERS 100
#define MAX_PARTICIPANTS 100
#define MAX_CHATROOMS 100
#define RECREATE_FILES_IN_STARTUP 1
#define NUM_SERVERS 5
#define MAX_HISTORY_MESSAGES 100

///////////////////////// Data Structures   //////////////////////////////////////////////////////

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

typedef struct Chatroom_t
{
	char name[20];
	u_int32_t numOf_messages;
	Message messages[25];
	hash_set_st participants[5];
	u_int32_t num_of_participants[5];
	u_int32_t message_start_pointer;
} Chatroom;

typedef struct Session_t
{
	u_int32_t server_id;			   // my ID
	Chatroom chatrooms[MAX_CHATROOMS]; // chatroom data list
	int connected_clients;			   // number of clients currently connected to me
	map_t clients;					   // connected clients and their chatroom ID
	int num_of_chatrooms;			   // to keep track of in-memory data structures
	u_int32_t membership[5];		   // Membership status of each server
	u_int32_t lamport_counters[5][5];  // Stores the last received lamport counter from each server according to each server's view
	u_int32_t lamport_counter;
	enum State state;
	Node *unprocessed_update_start;
	u_int32_t unprocessed_updates_count;
	u_int32_t processed_lamport_counters[5];
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

//static int handle_disconnect(char *username);
static int handle_connect(char *message, u_int32_t size);
static int handle_join(char *message, int size);
static int handle_append(char *message, int msg_size);
static int handle_like_unlike(char *message, char event_type);
static int handle_history();
static int handle_membership_status(char *message, int msg_size);

static int handle_unprocessed_updates();
static int check_primary_conditions();
static int send_anti_entropy_to_server(u_int32_t server_id);
static int create_new_chatroom(char *chatroom, int no_create_file);
static int find_chatroom_index(char *chatroom);
static int parse(char *message, int size, int num_groups);
static int handle_server_update();
static int handle_participant_update(char *message, int msg_size);
static int handle_anti_entropy();
static int handle_membership_change();
static void update_chatroom_data(int chatroom_index, char *chatroom, char *username, u_int32_t payload_length, char *payload, logEvent e, u_int32_t serverID, int dump);

////////////////////////// Utility ////////////////////////////////////////

u_int32_t chksum(const void *str)
{
	char *s = (char *)str;
	int len = strlen(s);
	int i;
	uint32_t c = 0;

	for (i = 0; i < len; ++i)
	{
		c = (c >> 1) + ((c & 1) << (32 - 1));
		c += s[i];
	}
	return (c);
}

int hash_set_remove(hash_set_st *old_hashset, u_int32_t chatroom_index, u_int32_t server_index, char *username)
{
	hash_set_it *it;
	int j, count = 0;
	char *value;
	hash_set_st *temp = hash_set_init(chksum);
	it = it_init(old_hashset);
	u_int32_t length = current_session.chatrooms[chatroom_index].num_of_participants[server_index];
	for (j = 0; j < length; j++)
	{
		value = (char *)it_value(it);
		if (strcmp(value, username))
		{
			hash_set_insert(temp, value, strlen(value));
			count++;
		}
		it_next(it);
	}
	hash_set_clear(old_hashset);
	count = 0;
	it = it_init(old_hashset);
	for (j = 0; j < count; j++)
	{
		value = (char *)it_value(it);
		hash_set_insert(old_hashset, value, strlen(value));
		count++;
		it_next(it);
	}
	current_session.chatrooms[chatroom_index].num_of_participants[server_index] = count;
	return count;
}

//////////////////////////   Core Functions  ////////////////////////////////////////////////////

int main(int argc, char *argv[])
{
	int ret;
	int mver, miver, pver;
	sp_time test_timeout;

	test_timeout.sec = 5;
	test_timeout.usec = 0;

	Usage(argc, argv);
	if (!SP_version(&mver, &miver, &pver))
	{
		log_fatal("main: Illegal variables passed to SP_version()\n");
		Bye();
	}
	log_info("Spread library version is %d.%d.%d\n", mver, miver, pver);

	ret = SP_connect_timeout(Spread_name, User, 0, 1, &Mbox, Private_group, test_timeout);
	if (ret != ACCEPT_SESSION)
	{
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

static void Read_message()
{

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
	if (ret < 0)
	{
		if ((ret == GROUPS_TOO_SHORT) || (ret == BUFFER_TOO_SHORT))
		{
			service_type = DROP_RECV;
			printf("\n========Buffers or Groups too Short=======\n");
			ret = SP_receive(Mbox, &service_type, sender, MAX_MEMBERS, &num_groups, target_groups, &mess_type, &endian_mismatch, sizeof(mess), mess);
		}
	}
	if (ret < 0)
	{
		if (!To_exit)
		{
			SP_error(ret);
			printf("\n============================\n");
			printf("\nBye.\n");
		}
		exit(0);
	}
	if (Is_regular_mess(service_type))
	{
		log_debug("Received regular message.");
		parse(mess, ret, num_groups);
	}
	else if (Is_membership_mess(service_type))
	{
		log_debug("Received membership message.");
		ret = SP_get_memb_info(mess, service_type, &memb_info);
		if (ret < 0)
		{
			log_fatal("BUG: membership message does not have valid body\n");
			SP_error(ret);
			exit(1);
		}
		if (Is_reg_memb_mess(service_type))
		{
			int join = 0, i;
            u_int32_t server_id;
	        char garbage[20];
			if (Is_caused_join_mess(service_type)){
				join = 1;
                if (!strncmp(sender, "chat_servers", 12))
                {
                    for(i=0;i < num_groups; i++)
                    {
                        sscanf(&target_groups[i][0] + 1, "%d%s", &server_id, garbage);
                        current_session.membership[server_id-1] = 1;
                        log_debug("%s is in that group", &target_groups[i][0]);
                    }

                }
            }
            
			handle_membership_change(target_groups, num_groups, join, memb_info.changed_member, sender);
		}
		else if (Is_transition_mess(service_type))
		{
			log_info("received TRANSITIONAL membership for group %s\n", sender);
		}
		else if (Is_caused_leave_mess(service_type))
		{
			log_info("received membership message that left group %s\n", sender);
		}
		else
			log_info("received incorrecty membership message of type 0x%x\n", service_type);
	}
	else if (Is_reject_mess(service_type))
	{
		log_info("REJECTED message from %s, of servicetype 0x%x messtype %d, (endian %d) to %d groups \n(%d bytes): %s\n", sender, service_type, mess_type,
				 endian_mismatch, num_groups, ret, mess);
	}
	else
		log_error("received message of unknown message type 0x%x with ret %d\n", service_type, ret);
}

static void Usage(int argc, char *argv[])
{
	sprintf(Spread_name, "10330");
	if (argc != 2 && argc != 3)
	{
		printf("Usage: ./server [server_id 1-5] [log_level]\n");
		exit(0);
	}
	if (argc == 3)
	{
		log_level = atoi(argv[2]);
		log_set_level(log_level);
	}
	sprintf(User, "%s", argv[1]);
	current_session.server_id = atoi(argv[1]);
}

static void Bye()
{
	To_exit = 1;

	log_info("\nBye.\n");

	SP_disconnect(Mbox);

	exit(0);
}

static int parse(char *message, int size, int num_groups)
{
	char type;
	memcpy(&type, message, 1);
	log_debug("Type is %c", type);
	switch (type)
	{
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
		handle_membership_status(message, size);
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
    case TYPE_MEMBERSHIP_STATUS_RESPONSE:
        break;
	default:
		log_error("invalid message type %c", type);
		break;
	}
	return 0;
}

static const char *get_filename_ext(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if(!dot || dot == filename) return "";
    return dot + 1;
}

static void dump_message_from_file(u_int32_t chatroom_index, Message m)
{
    u_int32_t msg_pointer;
	if (current_session.chatrooms[chatroom_index].numOf_messages < 25)
	{
		msg_pointer = current_session.chatrooms[chatroom_index].numOf_messages;
		log_debug("chatroom %s has %d message(s)", current_session.chatrooms[chatroom_index].name, msg_pointer);
	}
	else
	{
		msg_pointer = current_session.chatrooms[chatroom_index].message_start_pointer;
	}
	memcpy(current_session.chatrooms[chatroom_index].messages[msg_pointer].message, m.message, strlen(m.message));
	memcpy(current_session.chatrooms[chatroom_index].messages[msg_pointer].userName, m.userName, strlen(m.userName));
	current_session.chatrooms[chatroom_index].messages[msg_pointer].userName[strlen(m.userName)] = 0;

	current_session.chatrooms[chatroom_index].messages[msg_pointer].numOfLikes = 0;
	current_session.chatrooms[chatroom_index].messages[msg_pointer].lamportCounter = m.lamportCounter;
	current_session.chatrooms[chatroom_index].messages[msg_pointer].serverID = m.serverID;
	current_session.chatrooms[chatroom_index].numOf_messages++;
	if (current_session.chatrooms[chatroom_index].message_start_pointer == 25)
	{
		current_session.chatrooms[chatroom_index].message_start_pointer = 0;
	}
}

static void rebuild_lts_data(u_int32_t index)
{
	int i;
	for(i = 0;i < current_session.chatrooms[index].numOf_messages; i++)
	{
    	current_session.lamport_counters[current_session.server_id-1][current_session.chatrooms[index].messages[i].serverID-1] = current_session.chatrooms[index].messages[i].lamportCounter;
	}
}

static void create_chatroom_from_logs()
{
	DIR *directory;
	struct dirent* file;
	FILE *cf;
    char * line = NULL;
    size_t len = 0;
    ssize_t read;
	char chatroom_name[20];
    Message m;
    u_int32_t index, server_id;
	directory = opendir(".");
    if (directory == NULL) {
        log_error("error opening base directory");
        exit(2);
    }

    while ((file=readdir(directory)) != NULL) {
        log_debug("filename: %s\n", file->d_name);
		if(strcmp("chatroom", get_filename_ext(file->d_name)) == 0)
		{
			sscanf(file->d_name, "%d_%[^.].chatroom", &server_id, chatroom_name);
            if(server_id != current_session.server_id)
                continue;
            index = find_chatroom_index(chatroom_name);
            if (index == -1)
		        index = create_new_chatroom(chatroom_name, 1);
			log_debug("chatroom file found %s for room %s, idx = %d", file->d_name, chatroom_name, index);
            cf = fopen(file->d_name, "r");
            if ( cf != NULL )
            {
                while ((read = getline(&line, &len, cf)) != -1) {
                    parseLineInMessagesFile(line, &m);
                    log_debug("LTS = %d, %d", m.serverID, m.lamportCounter);
                    dump_message_from_file(index, m);
                }
            }
            rebuild_lts_data(index);
		}
	}
	closedir(directory);
}

static void update_chatroom_data_based_on_log_files()
{
	// read the log files
	// if line has more reent data than last LTS of chatroom, update it
}

static int initialize()
{
	int ret, i, j;
	int fds[5];
	char server_group_name[10];
	log_info("Server Initializing");
	create_log_files(current_session.server_id, 5, RECREATE_FILES_IN_STARTUP, fds);

	create_chatroom_from_logs();
	update_chatroom_data_based_on_log_files();

	current_session.connected_clients = 0;
	current_session.num_of_chatrooms = 0;
	current_session.unprocessed_updates_count = 0;
	current_session.state = STATE_PRIMARY;
	current_session.unprocessed_update_start = NULL;

	for (i = 0; i < 5; i++)
	{
		current_session.membership[i] = 0;
		current_session.processed_lamport_counters[i] = 0;
		for (j = 0; j < 5; j++)
		{
			current_session.lamport_counters[i][j] = 0;
		}
	}
	current_session.clients = hashmap_new();
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
	char *serversGroup = "chat_servers";
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
	for (i = 0; i < 5; i++)
	{
		it = it_init(&current_session.chatrooms[index].participants[i]);
		for (j = 0; j < current_session.chatrooms[index].num_of_participants[i]; j++)
		{
			username = (char *)it_value(it);
			hash_set_insert(agg, username, strlen(username));
			count++;
			it_next(it);
		}
	}
	log_debug("Aggregated participants for chatroom index %d - total participants = %d", index, count);
	return count;
}

static int send_chatroom_update_to_clients(char *chatroom, int index)
{
	int j;
	char chatroomGroup[30];
	char message[1400];
	char *username;
	hash_set_st *participants = hash_set_init(chksum);
	hash_set_it *it;
	u_int32_t num_participants, username_size;
	int offset = 5;
	message[0] = TYPE_CLIENT_UPDATE;
	sprintf(chatroomGroup, "CHATROOM_%s_%d", chatroom, current_session.server_id);
	num_participants = aggregate_participants(participants, index);
	memcpy(message + 1, &num_participants, 4);
	it = it_init(participants);
	for (j = 0; j < num_participants; j++)
	{
		username = (char *)it_value(it);
		u_int32_t uname_size = strlen(username);
		memcpy(message + offset, &uname_size, 4);
		memcpy(message + offset + 4, username, uname_size);
		offset += (4 + uname_size);
		it_next(it);
	}

	memcpy(message + offset, &current_session.chatrooms[index].numOf_messages, 4);
	offset += 4;
	int i;
	for (i = 0; i < current_session.chatrooms[index].numOf_messages; i++)
	{
		u_int32_t message_size = strlen(current_session.chatrooms[index].messages[i].message);
		log_debug("message size is %d", message_size);
		memcpy(message + offset, &current_session.chatrooms[index].messages[i].serverID, 4);
		memcpy(message + offset + 4, &current_session.chatrooms[index].messages[i].lamportCounter, 4);
		//
		username_size = strlen(current_session.chatrooms[index].messages[i].userName);
		memcpy(message + offset + 8, &username_size, 4);
		memcpy(message + offset + 12, &current_session.chatrooms[index].messages[i].userName, username_size);
		current_session.chatrooms[index].messages[i].userName[username_size] = 0;
		//
		offset += (12 + username_size);
		memcpy(message + offset, &message_size, 4);
		memcpy(message + offset + 4, current_session.chatrooms[index].messages[i].message, message_size);
		log_debug("message is %s", current_session.chatrooms[index].messages[i].message);
		memcpy(message + offset + 4 + message_size, &current_session.chatrooms[index].messages[i].numOfLikes, 4);
		log_debug("num of likes is %s", current_session.chatrooms[index].messages[i].numOfLikes);
		offset += (8 + message_size);
	}
	log_debug("sending client update for chatroom %s with %d participants and %d messages", chatroom, num_participants, current_session.chatrooms[index].numOf_messages);
	SP_multicast(Mbox, AGREED_MESS, chatroomGroup, 2, offset + 5, message);
	return 0;
}

static int handle_connect(char *message, u_int32_t size)
{
	u_int32_t username_size;
	int ret;
	char username[20];
	char group_name[30];
	memcpy(&username_size, message + 1, 4);
	memcpy(username, message + 5, username_size);
	username[username_size] = 0;
	sprintf(group_name, "%s_%d", username, current_session.server_id);
	log_info("Handling client connection %s by joining %s", username, group_name);
	ret = SP_join(Mbox, group_name);
	if (ret < 0)
		SP_error(ret);
	current_session.connected_clients++; // TODO: not if the client is already connected
	//chatroom_id = (int32_t *) malloc(sizeof(int32_t));
	//*chatroom_id = -1;
	//ret = hashmap_put(current_session.clients, username, chatroom_id);
	//if(ret!=MAP_OK)
	//	log_error("Error in adding client to my map");
	return 0;
}

static int find_chatroom_index(char *chatroom)
{
	int i;
	for (i = 0; i < current_session.num_of_chatrooms; i++)
		if (!strcmp(current_session.chatrooms[i].name, chatroom))
		{
			log_debug("chatroom index for %s is %d", chatroom, i);
			return i;
		}
	log_debug("chatroom index for %s not found", chatroom);
	return -1;
}

static int create_new_chatroom(char *chatroom, int no_create_file)
{
	int i;
	int index = current_session.num_of_chatrooms;
	log_info("Creating data structures for new chatroom %s", chatroom);
	current_session.num_of_chatrooms++;
	strcpy(current_session.chatrooms[index].name, chatroom);
	current_session.chatrooms[index].numOf_messages = 0;
	current_session.chatrooms[index].message_start_pointer = 0;
	for (i = 0; i < 5; i++)
	{
		current_session.chatrooms[index].participants[i] = *hash_set_init(chksum);
		current_session.chatrooms[index].num_of_participants[i] = 0;
	}
	if(!no_create_file)
		create_chatroom_file(current_session.server_id, chatroom, RECREATE_FILES_IN_STARTUP);
	return index;
}

static int send_participant_change_to_servers(char *chatroom, char *username, int index)
{
	char payload[1300];
	u_int32_t offset, nop;
	u_int32_t chatroom_length = strlen(chatroom);
	hash_set_it *it;
	memcpy(payload, &chatroom_length, 4);
	memcpy(payload + 4, chatroom, chatroom_length);
	offset = 4 + chatroom_length;
	int i, j;
	for (i = 0; i < 5; i++)
	{
		nop = current_session.chatrooms[index].num_of_participants[i];
		log_debug("Server %d #participants %d", i + 1, nop);
		memcpy(payload + offset, &nop, 4);
		offset += 4;
		it = it_init(&current_session.chatrooms[index].participants[i]);
		for (j = 0; j < nop; j++)
		{
			username = (char *)it_value(it);
			u_int32_t uname_size = strlen(username);
			memcpy(payload + offset, &uname_size, 4);
			memcpy(payload + offset + 4, username, uname_size);
			offset += (4 + uname_size);
			it_next(it);
		}
	}
	send_to_servers(TYPE_PARTICIPANT_UPDATE, payload, offset + 4 + chatroom_length);
	return 0;
}

static int handle_join(char *message, int size)
{
	u_int32_t username_length, chatroom_length;
	char chatroom[20];
	char *username;
	int32_t chatroom_index, ret;
	int32_t *old_idx = (int32_t *)malloc(sizeof(int32_t));
	username = (char *)calloc(20, 1);
	memcpy(&username_length, message + 1, 4);
	memcpy(username, message + 5, username_length);
	memcpy(&chatroom_length, message + 5 + username_length, 4);
	memcpy(chatroom, message + 9 + username_length, chatroom_length);
	chatroom[chatroom_length] = 0;
	username[username_length] = 0;
	log_debug("Handling client join request username = %s, chatroom length = %d, chatroom = %s, old chatroom = %d", username, chatroom_length, chatroom, *old_idx);
	ret = hashmap_get(current_session.clients, username, (void **)(&old_idx));
	if (ret == MAP_OK)
	{

		log_debug("client was previously in chatroom index %d", *old_idx);
		hash_set_remove(&current_session.chatrooms[*old_idx].participants[current_session.server_id - 1], *old_idx, current_session.server_id - 1, username);
		send_participant_change_to_servers(current_session.chatrooms[*old_idx].name, username, *old_idx);
		send_chatroom_update_to_clients(current_session.chatrooms[*old_idx].name, *old_idx);
	}
	else
	{
		free(old_idx);
		old_idx = (int32_t *)malloc(sizeof(int32_t));
	}
	chatroom_index = find_chatroom_index(chatroom);
	if (chatroom_index == -1)
		chatroom_index = create_new_chatroom(chatroom, 0);

	hash_set_insert(&current_session.chatrooms[chatroom_index].participants[current_session.server_id - 1], username, strlen(username));
	current_session.chatrooms[chatroom_index].num_of_participants[current_session.server_id - 1]++;
	log_warn("1111");
	*old_idx = chatroom_index;
	log_warn("22222");
	ret = hashmap_put(current_session.clients, username, old_idx);

	if (ret != MAP_OK)
		log_error("Error in putting client to my map");

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

static int handle_append(char *message, int msg_size)
{
	u_int32_t username_length, chatroom_length, payload_length, offset;
	u_int32_t size = 0;
	char username[20], chatroom[20], payload[80];
	int chatroom_index;
	logEvent e;
	memset(&e, 0, sizeof(e));
	memcpy(&username_length, message + 1, 4);
	memcpy(username, message + 5, username_length);
	memcpy(&chatroom_length, message + 5 + username_length, 4);
	memcpy(chatroom, message + 9 + username_length, chatroom_length);
	chatroom[chatroom_length] = 0;
	username[username_length] = 0;
	log_debug("handling append message from %s in chatroom %s", username, chatroom);
	if(current_session.state == STATE_RECONCILING)
	{
		log_warn("in the midst of reconciling. appending to a temporary list to process later. list length before append is %d", current_session.unprocessed_updates_count);
		push(current_session.unprocessed_update_start, message, msg_size);
		current_session.unprocessed_updates_count++;
		return 0;
	}

	chatroom_index = find_chatroom_index(chatroom);
	if (chatroom_index == -1)
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
	current_session.lamport_counters[current_session.server_id - 1][current_session.server_id - 1] = current_session.lamport_counter;
	char line[100];
	sprintf(line, "%s~%s", username, payload);
	memcpy(e.payload, line, payload_length + username_length + 1);
	memcpy(e.chatroom, chatroom, chatroom_length);
	log_debug("event log payload for append is %s", line);
	size += (13 + strlen(e.payload));
	char buffer[size];
	createLogLine(current_session.server_id, e, buffer);
	addEventToLogFile(current_session.server_id, buffer);
	send_log_update_to_servers(current_session.server_id, strlen(buffer), buffer);

	update_chatroom_data(chatroom_index, chatroom, username, payload_length, payload, e, current_session.server_id, 0);

	return 0;
}

static void update_chatroom_data(int chatroom_index, char *chatroom, char *username, u_int32_t payload_length, char *payload, logEvent e, u_int32_t serverID, int dump)
{
	u_int32_t msg_pointer;
	if (current_session.chatrooms[chatroom_index].numOf_messages < 25)
	{
		msg_pointer = current_session.chatrooms[chatroom_index].numOf_messages;
		log_debug("chatroom %s has %d message(s)", chatroom, msg_pointer);
	}
	else
	{
		msg_pointer = current_session.chatrooms[chatroom_index].message_start_pointer;
		if(!dump)
		{
			Message m = current_session.chatrooms[chatroom_index].messages[current_session.chatrooms[chatroom_index].message_start_pointer];
			log_debug("moving message #%d, %d to file", m.serverID, m.lamportCounter);
			addMessageToChatroomFile(current_session.server_id, chatroom, m);
		}

	}
	memcpy(current_session.chatrooms[chatroom_index].messages[msg_pointer].message, payload, payload_length);
	memcpy(current_session.chatrooms[chatroom_index].messages[msg_pointer].userName, username, strlen(username));
	current_session.chatrooms[chatroom_index].messages[msg_pointer].userName[strlen(username)] = 0;

	current_session.chatrooms[chatroom_index].messages[msg_pointer].numOfLikes = 0;
	current_session.chatrooms[chatroom_index].messages[msg_pointer].lamportCounter = e.lamportCounter;
	current_session.chatrooms[chatroom_index].messages[msg_pointer].serverID = serverID;
	current_session.chatrooms[chatroom_index].numOf_messages++;
	if (current_session.chatrooms[chatroom_index].message_start_pointer == 25)
	{
		current_session.chatrooms[chatroom_index].message_start_pointer = 0;
	}
	if(!dump)
		send_chatroom_update_to_clients(chatroom, chatroom_index);
}

static int handle_like_unlike(char *message, char event_type)
{

	u_int32_t username_length, chatroom_length, offset;
	u_int32_t size = 0;
	logEvent e;
	int chatroom_index;
	char username[20], chatroom[20];
	u_int32_t pid, counter;
	char line[100];
	memcpy(&username_length, message + 1, 4);
	memcpy(username, message + 5, username_length);
	memcpy(&chatroom_length, message + 5 + username_length, 4);
	memcpy(chatroom, message + 9 + username_length, chatroom_length);
	username[username_length] = 0;
	chatroom[chatroom_length] = 0;
	if(current_session.state == STATE_RECONCILING)
	{
		log_warn("in the midst of reconciling. appending to a temporary list to process later. list length before append is %d", current_session.unprocessed_updates_count);
		push(current_session.unprocessed_update_start, message, strlen(message));
		current_session.unprocessed_updates_count++;
		return 0;
	}
	chatroom_index = find_chatroom_index(chatroom);
	if (chatroom_index == -1)
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
	current_session.lamport_counters[current_session.server_id - 1][current_session.server_id - 1] = current_session.lamport_counter;
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

static int send_history_response(char *username, char *chatroom)
{
	int i;	
	char clientGroup[30];
	u_int32_t index = find_chatroom_index(chatroom);
	char response[1400];
	u_int32_t num_of_messages, offset = 5;
	u_int32_t message_size, username_size;
	Message messages[MAX_HISTORY_MESSAGES];

	retrieve_chatroom_history(current_session.server_id, chatroom, &num_of_messages, messages);
    // TODO: append_memory_messages(current_session.server_id, chatroom, &num_of_messages, messages)
	response[0] = TYPE_HISTORY_RESPONSE;
    memcpy(response + 1, &num_of_messages, 4);
	sprintf(clientGroup, "%s_%d", username, current_session.server_id);

	for (i = 0; i < num_of_messages; i++)
	{
		message_size = strlen(messages[i].message);
		log_debug("message size is %d", message_size);
		memcpy(response + offset, &messages[i].serverID, 4);
		memcpy(response + offset + 4, &messages[i].lamportCounter, 4);
		//
		username_size = strlen(messages[i].userName);
		memcpy(response + offset + 8, &username_size, 4);
		memcpy(response + offset + 12, &messages[i].userName, username_size);
		//
		offset += (12 + username_size);
		memcpy(response + offset, &message_size, 4);
		memcpy(response + offset + 4, current_session.chatrooms[index].messages[i].message, message_size);
		log_debug("message is %s", messages[i].message);
		memcpy(response + offset + 4 + message_size, &messages[i].numOfLikes, 4);
		log_debug("num of likes is %s", messages[i].numOfLikes);
		offset += (8 + message_size);
	}
	log_debug("sending history response to group %s with %d messages ", clientGroup, num_of_messages);
	SP_multicast(Mbox, AGREED_MESS, clientGroup, 2, offset + 5, response);
    return 0;    
}

static int handle_history(char *message, u_int32_t size)
{
    u_int32_t username_length, chatroom_length;
	u_int32_t offset = 5;
	char username[20], chatroom[20];

	memcpy(&username_length, message + 1, 4);
	memcpy(username, message + 5, username_length);
	username[username_length] = 0;
	offset += username_length;
	memcpy(&chatroom_length, message + offset, 4);
	memcpy(chatroom, message + offset + 4, chatroom_length);
	chatroom[chatroom_length] = 0;
	log_debug("handling history message from %s for chatroom %s", username, chatroom);
	
	send_history_response(username, chatroom);
	return 0;
}

static int handle_membership_status(char *message, int msg_size)
{
	int i;
	char clientGroup[30];
	char response[NUM_SERVERS*4+5];
    u_int32_t username_length;	
	char username[20];
    int offset = 5;
    u_int32_t num_servers;

	memcpy(&username_length, message + 1, 4);
	memcpy(username, message + 5, username_length);
	username[username_length] = 0;
	log_debug("handling membership status message from %s", username);
	num_servers = NUM_SERVERS;
	response[0] = TYPE_MEMBERSHIP_STATUS_RESPONSE;
    memcpy(response + 1, &num_servers, 4);
	sprintf(clientGroup, "%s_%d", username, current_session.server_id);
	for (i = 0; i < NUM_SERVERS; i++)
	{
		memcpy(response + offset, &current_session.membership[i], 4);
		offset += 4;
	}
	log_debug("sending membership status response: %d %d %d %d %d ", current_session.membership[0],current_session.membership[1],current_session.membership[2],current_session.membership[3],current_session.membership[4]);
	SP_multicast(Mbox, AGREED_MESS, clientGroup, 2, offset + 5, response);	
	return 0;
}

//////////////////////////   Server Event Handlers ////////////////////////////////////////////////////

static int log_remaining()
{
	int i;
	for(i = 0;i< NUM_SERVERS;i++)
	{
		if(current_session.processed_lamport_counters[i] < current_session.lamport_counters[current_session.server_id - 1][i])
			return 1;
	}
	return 0;
}

static int process_log_event(logEvent e, u_int32_t server_id)
{
	u_int32_t chatroom_index;
	char username[20], message_text[80];
	chatroom_index = find_chatroom_index(e.chatroom);
    if(chatroom_index == -1){
        chatroom_index = create_new_chatroom(e.chatroom, 0);
    }
	switch (e.eventType)
	{
	case TYPE_APPEND:
		sscanf(e.payload, "%[^\n\t~]~%[^\n\t]", username, message_text);
		log_debug("parsing append data {%s} from message. username = %s, message text = %s, chatroom = %s", e.payload, username, message_text, e.chatroom);
		update_chatroom_data(chatroom_index, e.chatroom, username, strlen(message_text), message_text, e, server_id, 0);
		break;
	case TYPE_LIKE:
		break;
	case TYPE_UNLIKE:
		break;
	default:
		log_error("Invalid event type %c", e.eventType);
		break;
	}
	current_session.processed_lamport_counters[server_id - 1] = e.lamportCounter;
	return 0;
}

static int process_log_files()
{
	logEvent e[NUM_SERVERS];
	u_int32_t data_available[NUM_SERVERS];
	u_int32_t min_lc = -1, min_server_id = 0;
	int i;
    log_debug("processing log files");
	while(log_remaining())
	{
		retrieve_line_from_logs(e, data_available, NUM_SERVERS, current_session.processed_lamport_counters);
        log_debug("retrieving log lines from files %d %d %d %d %d", data_available[0],data_available[1],data_available[2],data_available[3],data_available[4]);
        min_lc = -1;
        min_server_id = 0;
		for(i = 0;i<NUM_SERVERS;i++)
		{
			if(data_available[i])
			{
				if(e[i].lamportCounter < min_lc)
				{
					min_lc = e[i].lamportCounter;
					min_server_id = i+1;
				}
			}
		}
        log_debug("minimum log line is for server %d with lc %d", min_server_id, e[min_server_id-1].lamportCounter); 
		process_log_event(e[min_server_id - 1], min_server_id);
	}
    return 0;
}

static int handle_server_update(char *messsage, int size)
{
	u_int32_t sender_id, server_id, log_length, chatroom_index;
	logEvent e;
	char username[20], message_text[80];
	memcpy(&sender_id, messsage + 1, 4);
	memcpy(&server_id, messsage + 5, 4);
	memcpy(&log_length, messsage + 9, 4);
	if (server_id == current_session.server_id)
		return 0;
	char line[log_length];
	memcpy(&line, messsage + 13, log_length);
	log_debug("handling server update (of server %d) from server %d. update line is: %s", server_id, sender_id, line);
	parseLineInLogFile(line, &e);
	if(e.lamportCounter <= current_session.lamport_counters[current_session.server_id - 1][server_id - 1]){
		log_warn("received duplicate data. ignoring");
		return 0;
	}
	addEventToLogFile(server_id, line);
	if(e.lamportCounter > current_session.lamport_counter)
		current_session.lamport_counter = e.lamportCounter;
	// current_session.lamport_counters[current_session.server_id - 1][current_session.server_id - 1] = e.lamportCounter;
	current_session.lamport_counters[current_session.server_id - 1][server_id - 1] = e.lamportCounter;

	if(current_session.state == STATE_RECONCILING){
		if(check_primary_conditions()){
            log_info("returning to primary state");
			current_session.state = STATE_PRIMARY;
			process_log_files();	// TODO
			handle_unprocessed_updates();
		}
		return 0;
	}
	process_log_event(e, server_id);
	return 0;
}

static int resend_data(u_int32_t server_id, u_int32_t lamport_counter)
{
	int i;
	char username[20];
	u_int32_t size = (13 + 1300);
	char buffer[size];
	logEvent logs[100]; // TODO: maybe more?
	u_int32_t length = 0;
	get_logs_newer_than(server_id, lamport_counter, &length, logs);
	log_debug("getting newer data than LTS %d,%d from log file, totalling %d", server_id, lamport_counter, length);
	for(i = 0; i < length; i++)
	{
		log_debug("resending log %d, %d of type", server_id, logs[i].lamportCounter, logs[i].eventType);
		createLogLine(current_session.server_id, logs[i], buffer);
		send_log_update_to_servers(current_session.server_id, strlen(buffer), buffer);
	}
	for(i = 0; i < current_session.num_of_chatrooms; i++){
		send_participant_change_to_servers(current_session.chatrooms[i].name, username, i);
	}
	return 0;
}

static int i_am_responsible_to_resend_data(u_int32_t server_id)
{
	if(server_id == current_session.server_id)
	{
		int i;
		u_int32_t min_lc = current_session.lamport_counters[current_session.server_id - 1][current_session.server_id - 1];
        log_debug("min lc for myself is %d", min_lc);
		for(i = NUM_SERVERS-1; i >= 0; i--)
			if (current_session.membership[i] && current_session.lamport_counters[i][server_id - 1] < min_lc){
				min_lc = current_session.lamport_counters[i][server_id - 1];
                log_debug("min lc changed to %d in row %d", min_lc, i);
            }
		if(min_lc < current_session.lamport_counters[current_session.server_id - 1][current_session.server_id - 1]){
            log_debug("I am responsible for missing data from myself. attempting to send from lc %d...", min_lc);
			resend_data(current_session.server_id, min_lc);
        }
		return 1;	// my own data
	}
	else if (current_session.membership[server_id - 1])
    {
        return 0;   // because the responsible server is already in the membership
    }
    else
	{
		int i;
		u_int32_t max_lc = 0;
		u_int32_t min_lc = -1;
		u_int32_t max_server = 0;
		for(i = NUM_SERVERS-1; i >= 0; i--){
            log_debug("server %d membership = %d", i+1, current_session.membership[i]);
			if(current_session.membership[i]){
				if (current_session.lamport_counters[i][server_id - 1] >= max_lc){
					max_lc = current_session.lamport_counters[i][server_id - 1];
					max_server = i+1;
                    log_debug("changing max lc to %d for server %d", max_lc, max_server);
				}
				if(current_session.lamport_counters[i][server_id - 1] < min_lc){
					min_lc = current_session.lamport_counters[i][server_id - 1];
                    log_debug("min lc changed to %d in row %d", min_lc, i);
                }
			}
		}
		if(min_lc < max_lc && max_server == current_session.server_id){
            log_debug("I am responsible for missing data from %d. attempting to send from lc %d...",server_id, min_lc);
			resend_data(server_id, min_lc);
        }
		return 1;	// my own data
	}
	return 0;
}

static int check_primary_conditions()
{
	int i, j;
	for(i = 0; i < NUM_SERVERS; i++)
	{
		if(!current_session.membership[i] || i == current_session.server_id - 1)
			continue;
		for(j = 0;j < NUM_SERVERS;j++)
		{
			if(current_session.lamport_counters[i][j] != current_session.lamport_counters[current_session.server_id - 1][j]){
                log_debug("check primary condisions failed. last recived matrix i=%d j=%d lc=%d , my value=%d", i, j, current_session.lamport_counters[i][j], current_session.lamport_counters[current_session.server_id - 1][j]);
				return 0;
            }
		}
	}
    log_debug("Check primary conditions successful");
	return 1;
}

static int handle_unprocessed_updates()
{
	if(current_session.unprocessed_updates_count){
		char mess[MAX_MESSLEN];
		int len;
		while (current_session.unprocessed_update_start != NULL)
		{
			pop(&current_session.unprocessed_update_start, mess, &len);
			current_session.unprocessed_updates_count--;
			parse(mess, len, 0);
		}
		
	}
}

static int handle_anti_entropy(char *messsage, int size)
{
	u_int32_t sender_id, lamport_ctr;
	int i, j, offset = 5, outdated = 0, updated = 0;
	memcpy(&sender_id, messsage + 1, 4);
	if (sender_id == current_session.server_id)
		return 0;
	log_debug("Parsing Anti-entropy message from %d", sender_id);
	for (i = 0; i < NUM_SERVERS; i++)	// ROW
	{
		for (j = 0; j < NUM_SERVERS; j++)	// COLUMN
		{
			memcpy(&lamport_ctr, messsage + offset, 4);
			offset += 4;
			if (i == current_session.server_id - 1)
			{
				// Don't update data about myself. I know me better.
				// I should resend my entropy matrix to servers
				if(lamport_ctr < current_session.lamport_counters[i][j])
					outdated = 1;
			}
			else
			{
				if (i == sender_id - 1){
					if(lamport_ctr > current_session.lamport_counters[i][j]){
						current_session.lamport_counters[i][j] = lamport_ctr;
						updated = 1;
					}
				}					// Trust everything it said about itself.
				else if(lamport_ctr > current_session.lamport_counters[i][j]) // only update these rows if its data is more recent about others	
				{
					current_session.lamport_counters[i][j] = lamport_ctr;
					updated = 1;
				}
				
			}
		}
	}

	// But, if the server is behind, and I'm responsible, resend the data.
	for (i = 0; i < NUM_SERVERS; i++)
	    i_am_responsible_to_resend_data(i+1);

	if(outdated){
		log_debug("resending Anti-entropy to all");
		send_anti_entropy_to_server(0);
	}
    log_debug("updated = %d (matrix updated)", updated);
	if(check_primary_conditions()){
        log_info("returning to PRIMARY state");
		current_session.state = STATE_PRIMARY;
		handle_unprocessed_updates();
	}

	return 0;
}

static int send_anti_entropy_to_server(u_int32_t server_id)
{
	u_int32_t size = NUM_SERVERS * NUM_SERVERS * 4;
	char payload[size];
	int i, j, offset = 0;
	for (i = 0; i < NUM_SERVERS; i++)
	{
		for (j = 0; j < NUM_SERVERS; j++)
		{
			memcpy(payload + offset, &current_session.lamport_counters[i][j], 4);
			offset += 4;
		}
	}
	log_debug("sending Anti entropy to servers");
	send_to_servers(TYPE_ANTY_ENTROPY, payload, size);
	return 0;
}

static void handle_server_join(u_int32_t server_id)
{
    log_debug("handling server join %d", server_id);
	//current_session.membership[server_id - 1] = 1;
	current_session.state = STATE_RECONCILING;
	send_anti_entropy_to_server(server_id);
}

static void handle_server_leave(u_int32_t server_id)
{
	int i;
	for (i = 0; i < current_session.num_of_chatrooms; i++)
	{
		hash_set_clear(&current_session.chatrooms[i].participants[server_id - 1]);
		current_session.chatrooms[i].num_of_participants[i] = 0;
		send_chatroom_update_to_clients(current_session.chatrooms[i].name, i);
	}
	current_session.membership[server_id - 1] = 0;
}

static int handle_membership_change(char **target_groups, int num_groups, int is_joined, char *target_member, char *target_group)
{
	u_int32_t server_id;
	char client[20];
	char garbage[20];
	int ret;
	int32_t *idx = (int32_t *)malloc(sizeof(int32_t));
	if (!strncmp(target_group, "chat_servers", 12))
	{
		if (is_joined)
		{
			sscanf(target_member + 1, "%d%s", &server_id, garbage);
			log_warn("Server %s with id %d  joined the membership with %d members ", target_member, server_id, num_groups);
			//if(server_id != current_session.server_id)
            current_session.membership[server_id - 1] = 1;
            if(num_groups > 1)
			    handle_server_join(server_id);
		}
		else
		{
			sscanf(target_member + 1, "%d%s", &server_id, garbage);
			log_warn("Server %s with id %d left the membership with %d members", target_member, server_id, num_groups);
			// remove chat participants of this server from my lists
			handle_server_leave(server_id);
		}
	}
	else if (!strncmp(target_group, "server", 6))
		log_debug("It is me joining my group!");
	else
	{
		log_debug("client membership event: target group %s, target_member %s, joined = %d", target_group, target_member, is_joined);
		sscanf(target_group, "%[^_]_%d", client, &server_id);
		client[strlen(client)] = 0;
		log_debug("client is: %s with size: %d", client, strlen(client));
		if (!is_joined)
		{
			log_debug("map length is %d", hashmap_length(current_session.clients));
			ret = hashmap_get(current_session.clients, client, (void **)(&idx));
			if (ret == MAP_OK)
			{
				log_info("My client %s left", client);
				hashmap_remove(current_session.clients, client);
				hash_set_remove(&current_session.chatrooms[*idx].participants[current_session.server_id - 1], *idx, current_session.server_id - 1, client);
				send_participant_change_to_servers(current_session.chatrooms[*idx].name, client, *idx);
				send_chatroom_update_to_clients(current_session.chatrooms[*idx].name, *idx);
			}
		}
	}
	return 0;
}

static int handle_participant_update(char *message, int msg_size)
{
	u_int32_t server_id, num_of_participants;
	u_int32_t chatroom_length, offset, uname_length;

	int chatroom_index, i, p, flag = 1;
	char username[20], chatroom[20];
	memcpy(&server_id, message + 1, 4);
	memcpy(&chatroom_length, message + 5, 4);
	memcpy(&chatroom, message + 9, chatroom_length);
	chatroom[chatroom_length] = 0;
	if (server_id == current_session.server_id)
		return 0;
	chatroom_index = find_chatroom_index(chatroom);
	if (chatroom_index == -1)
	{
		// I don't have the chatroom. Let's create it:
		chatroom_index = create_new_chatroom(chatroom, 0);
	}
	offset = 9 + chatroom_length;
	for (i = 0; i < 5; i++)
	{
		if (i != server_id - 1 && current_session.membership[i])
			flag = 0;
		else
		{
			flag = 1;
			hash_set_clear(&current_session.chatrooms[chatroom_index].participants[i]);
			current_session.chatrooms[chatroom_index].num_of_participants[i] = 0;
		}
		memcpy(&num_of_participants, message + offset, 4);
		log_debug("num of participants %d is %d", i + 1, num_of_participants);
		offset += 4;
		for (p = 0; p < num_of_participants; p++)
		{
			memcpy(&uname_length, message + offset, 4);
			memcpy(username, message + offset + 4, uname_length);
			username[uname_length] = 0;
			log_debug("parsing user name %s for server %d list of p, will be added? %d", username, i + 1, flag);
			offset += (4 + uname_length);
			if (flag)
			{
				hash_set_insert(&current_session.chatrooms[chatroom_index].participants[i], username, strlen(username));
				current_session.chatrooms[chatroom_index].num_of_participants[i]++;
			}
		}
	}
	send_chatroom_update_to_clients(chatroom, chatroom_index);
	return 0;
}
