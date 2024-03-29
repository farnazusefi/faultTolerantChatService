////////////////// Chat client source code v1.0 //////////////////////////////////////////////////////////
//
//	The chat client is responsible to issue basic chat operations (append, like, unlike, join, leave)
//  to chat servers.
//	This project was defined as the final course project for Distributed Systems in JHU
//	Authors: Erfan Sharafzadeh (e.sharafzadeh@jhu.edu)
//			 Farnaz Yousefi (f.yousefi@jhu.edu)
//  											12/06/2019
////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "chat_include.h"

///////////////////////// Data Structures   //////////////////////////////////////////////////////

typedef struct Session_t {
	u_int32_t logged_in;			// if the client has logged in
	u_int32_t connected_server;		// the id of the current server
	u_int32_t is_connected;			// if the client is connectd to a server
	u_int32_t is_joined;			// if the user has joined a room

	char username[20];				// client's username
	char chatroom[20];				// client's chatroom name

	Message messages[25];			// messages receiveed from the server
	u_int32_t numOfMessages;		// number of valid messages in my session

	char listOfParticipants[MAX_PARTICIPANTS][20];	// list of chatroom participants
	u_int32_t numOfParticipants;	// number of valid participants of the chatroom

} Session;

///////////////////////// Global Variables //////////////////////////////////////////////////////

Session current_session;

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

static int parse(char *message, int size, int num_groups);
static int handle_membership_message(char *sender, int num_groups, membership_info *mem_info, int service_type);
static int handle_update_response(char *message, int size, int num_groups);
static int handle_history_response(char *message, int size);
static int handle_membership_status_response(char *message, int size, int num_groups);

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

	E_handle_events();

	return (0);
}

// parse user command and trigger the action handler
static void User_command() {
	char command[130];
	char mess[MAX_MESSLEN];
	char argument[80];
	unsigned int mess_len;
	int ret, i, server_id, line_id;

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
			if (current_session.is_connected)
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
		if (!current_session.is_connected) {
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
		ret = sscanf(&command[2], "%d", &line_id);
		if (ret < 1) {
			printf(" invalid line number \n");
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
		handle_like(line_id);
		break;

	case TYPE_UNLIKE:		// unlike a message
		ret = sscanf(&command[2], "%d", &line_id);
		if (ret < 1) {
			printf(" invalid line number \n");
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

// print menu on {Enter} pressed
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
	printf("\tl <line number> -- like a message\n");
	printf("\tr <line number> -- un-like a message\n");
	printf("\n");
	printf("\th -- print chatroom history \n");
	printf("\tv -- view server membership status\n");
	printf("\n");
	printf("\tq -- quit\n");
	fflush(stdout);
}

// Read the message from server
// try to parse the regular messages and handle the mebership changes
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
			log_error("\n========Buffers or Groups too Short=======\n");
			ret = SP_receive(Mbox, &service_type, sender, MAX_MEMBERS, &num_groups, target_groups, &mess_type, &endian_mismatch, sizeof(mess), mess);
		}
	}
	if (ret < 0) {
		if (!To_exit) {
			SP_error(ret);
			log_fatal("\n============================\n");
			log_fatal("\nBye.\n");
		}
		exit(0);
	}
	if (Is_regular_mess(service_type)) {
		log_debug("Regular message received");
		parse(mess, ret, num_groups);

	} else if (Is_membership_mess(service_type)) {
		log_debug("Membership change received");
		ret = SP_get_memb_info(mess, service_type, &memb_info);
		if (ret < 0) {
			log_fatal("BUG: membership message does not have valid body\n");
			SP_error(ret);
			exit(1);
		}
		if (Is_reg_memb_mess(service_type)) {
			handle_membership_message(sender, num_groups, &memb_info, service_type);

		} else if (Is_transition_mess(service_type)) {
			log_info("received TRANSITIONAL membership for group %s\n", sender);
		} else if (Is_caused_leave_mess(service_type)) {
			log_info("received membership message that left group %s\n", sender);
		} else
			log_error("received incorrecty membership message of type 0x%x\n", service_type);
	} else if (Is_reject_mess(service_type)) {
		log_error("REJECTED message from %s, of servicetype 0x%x messtype %d, (endian %d) to %d groups \n(%d bytes): %s\n", sender, service_type, mess_type,
				endian_mismatch, num_groups, ret, mess);
	} else
		log_error("received message of unknown message type 0x%x with ret %d\n", service_type, ret);

	printf("\n");
	printf("User> ");
	fflush(stdout);
}

// parsend command line arguments
static void Usage(int argc, char *argv[]) {
	char debug_level_str[10];
	sprintf(User, "user");
	sprintf(Spread_name, "10330");
	log_set_level(LOG_INFO);
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
		} else if (!strncmp(*argv, "-d", 2)) {
			if (argc < 2)
				Print_help();
			strcpy(debug_level_str, argv[1]);
			log_level = atoi(debug_level_str);
			log_set_level(log_level);
			argc--;
			argv++;
		} else {
			Print_help();
		}
	}
}

// print usage and command line arguments
static void Print_help() {
	printf("Usage: spuser\n%s\n%s\n%s\n%s\n", "\t[-u <user name>]  : unique (in this machine) user name", "\t[-s <address>]    : either port or port@machine",
			"\t[-r ]    : use random user name", "\t[-d <log_level> ]    : choose between [0-7] default is 2");
	exit(0);
}

// exit the application
static void Bye() {
	To_exit = 1;

	log_info("\nBye.\n");

	SP_disconnect(Mbox);

	exit(0);
}


// initialize the client session.
// everything is set to zero
static int initialize() {
	log_info("Initializing data structures.");
	current_session.logged_in = 0;
	current_session.is_joined = 0;
	current_session.connected_server = 0;
	current_session.is_connected = 0;
	current_session.numOfMessages = 0;
	log_debug("list of participants initialized");
	return 0;
}

//////////////////////////   User Event Handlers ////////////////////////////////////////////////////

// send a generic message of type <type> to server
// the username and type are appended to every message we send
static int sendToServer(char type, char *payload, u_int32_t size) {
	char serverPrivateGroup[80], message[size + 26];
	int ret;
	u_int32_t username_length = (u_int32_t) strlen(current_session.username);
	log_debug("sending to server type = %c, username length is %d", type, username_length);
	message[0] = type;
	memcpy(message + 1, &username_length, 4);
	memcpy(message + 5, current_session.username, username_length);
	memcpy(message + 5 + username_length, payload, size);
	sprintf(serverPrivateGroup, "server%d", current_session.connected_server);
	// print_hex(message, size + username_length + 5);
	ret = SP_multicast(Mbox, AGREED_MESS, serverPrivateGroup, 2, size + username_length + 5, message);
	log_debug("multicast returned with %d", ret);
	return 0;
}

// connect to a server
static int sendConnectionRequestToServer() {
	log_debug("sending connection request to server");
	sendToServer(TYPE_CONNECT, "", 0);
	return 0;

}

// end join request. we append the chatroom name as payload
static int sendJoinRequestToServer(char *chatroom) {
	int length = strlen(chatroom);
	char payload[length + 4];
	log_debug("sending join request to server for chatroom = %s", chatroom);
	memcpy(payload, &length, 4);
	memcpy(payload + 4, chatroom, length);
	sendToServer(TYPE_JOIN, payload, length + 4);
	return 0;

}

// request to append <message> to chatroom <chatroom>
static int sendAppendRequestToServer(char *chatroom, char *message) {
	int c_length = strlen(chatroom);
	int m_length = strlen(message);
	char payload[c_length + m_length + 8];
	log_debug("sending append request to server for chatroom = %s (%d), message = %s (%d)", chatroom, c_length, message, m_length);
	memcpy(payload, &c_length, 4);
	memcpy(payload + 4, chatroom, c_length);
	memcpy(payload + 4 + c_length, &m_length, 4);
	memcpy(payload + 8 + c_length, message, m_length);
	sendToServer(TYPE_APPEND, payload, c_length + m_length + 8);
	return 0;

}

// like or unlike a message in <chatroom> with <pid> and <counter> 
// type is  either TYPE_LIKE or TYPE_UNLIKE 
static int sendLikeUnlikeRequestToServer(u_int32_t pid, u_int32_t counter, char *chatroom, char type) {
	u_int32_t c_length = strlen(chatroom);
	char payload[c_length + 12];
	log_debug("sending %c request to server for chatroom = %s (length=%d), message LTS = %d,%d", type, chatroom, c_length, pid, counter);
	memcpy(payload, &c_length, 4);
	memcpy(payload + 4, chatroom, c_length);
	memcpy(payload + 4 + c_length, &pid, 4);
	memcpy(payload + 8 + c_length, &counter, 4);
	// print_hex(payload, 12 + c_length);
	sendToServer(type, payload, c_length + 12);
	return 0;
}

// request history of the <chatroom>
static int sendHistoryRequestToServer(char *chatroom) {
	u_int32_t length = strlen(chatroom);
	char payload[length + 4];
	log_debug("sending history request to server for chatroom = %s", chatroom);
	memcpy(payload, &length, 4);
	memcpy(payload + 4, chatroom, length);
	sendToServer(TYPE_HISTORY, payload, length + 4);
	return 0;
}

// request current server membership status (v)
static int sendMembershipRequestToServer() {
	log_debug("sending membership status request to server ");
	sendToServer(TYPE_MEMBERSHIP_STATUS, "", 0);
	return 0;
}

// convert line number of the chat messages to LTS.
// we use this for likes/unlikes
static void lineNumberToLTS(int line_number, u_int32_t *pid, u_int32_t *counter) {
	*pid = current_session.messages[line_number - 1].serverID;
	*counter = current_session.messages[line_number - 1].lamportCounter;
	log_debug("line number %d changed to pid %d and counter %d", line_number, *pid, *counter);
}

// login with <username>
static int handle_login(char *username) {
	memcpy(current_session.username, username, strlen(username));
	current_session.logged_in = 1;
	log_info("Welcome %s!", username);
	return 0;
}

// timed out in connecting to server. Display the connection failure to the user
static void server_connect_timeout(int code, void *data)
{
	int ret;
	char server_group_name[80];
	log_warn("Server %d Connection Failed. Try again with another server", current_session.connected_server);
	sprintf(server_group_name, "%s_%d", current_session.username, current_session.connected_server);
	ret = SP_leave(Mbox, server_group_name);
	if (ret < 0)
		SP_error(ret);
	current_session.connected_server = 0;
}

// connect to a server
// if already connected, first disconnect from the old server
static int handle_connect(int server_id) {
	int ret;
	sp_time timeout;
	char server_group_name[80];
	if (server_id != current_session.connected_server || !current_session.is_connected) {
		if (current_session.is_connected) {
			log_info("disconnecting from %d", current_session.connected_server);
			sprintf(server_group_name, "%s_%d", current_session.username, current_session.connected_server);
			ret = SP_leave(Mbox, server_group_name);
            current_session.is_joined = 0;
			if (ret < 0)
				SP_error(ret);
		}
		current_session.connected_server = server_id;
		sprintf(server_group_name, "%s_%d", current_session.username, current_session.connected_server);
		log_info("requesting to join %d, group name = %s", server_id, server_group_name);
		ret = SP_join(Mbox, server_group_name);
		if (ret < 0)
			SP_error(ret);

		sendConnectionRequestToServer();
		timeout.sec = 3;
    	E_queue( server_connect_timeout, 0, NULL, timeout );

		return 0;
	}
	log_warn("Already connected to server %d", server_id);
	return 0;
}


// join a chatroom
// if already joined, leave the previous chatroom first
static int join(char *chatroom) {
	char chatroom_group_name[80];
	int ret;
    if(current_session.is_joined)
    {
        sprintf(chatroom_group_name, "CHATROOM_%s_%d", current_session.chatroom, current_session.connected_server);
        log_debug("leaving Spread group %s", chatroom_group_name);
        ret = SP_leave(Mbox, chatroom_group_name);
        if (ret < 0)
            SP_error(ret);
    }
	sprintf(chatroom_group_name, "CHATROOM_%s_%d", chatroom, current_session.connected_server);
	log_info("joining Spread group %s", chatroom_group_name);
	ret = SP_join(Mbox, chatroom_group_name);
	if (ret < 0)
		SP_error(ret);
	sendJoinRequestToServer(chatroom);
	memcpy(current_session.chatroom, chatroom, strlen(chatroom));
    current_session.is_joined = 1;
	return 0;
}


static int handle_append(char *message, int size) {
	sendAppendRequestToServer(current_session.chatroom, message);
	return 0;
}

static int handle_like(int line_number) {
	u_int32_t pid, counter;
	lineNumberToLTS(line_number, &pid, &counter);
	sendLikeUnlikeRequestToServer(pid, counter, current_session.chatroom, TYPE_LIKE);
	return 0;
}

static int handle_unlike(int line_number) {
	u_int32_t pid, counter;
	lineNumberToLTS(line_number, &pid, &counter);
	sendLikeUnlikeRequestToServer(pid, counter, current_session.chatroom, TYPE_UNLIKE);
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

static int parse(char *message, int size, int num_groups) {
	char type = message[0];
	switch (type) {
	case TYPE_CLIENT_UPDATE:
		handle_update_response(message, size, num_groups);
		break;
	case TYPE_MEMBERSHIP_STATUS_RESPONSE:
		handle_membership_status_response(message, size, num_groups);
		break;
	case TYPE_HISTORY_RESPONSE:
		handle_history_response(message, size);
		break;
	default:
		log_error("Invalid message type received from server %d", type);
		break;
	}
	return 0;
}

static void display_disconnection_to_user()
{
	log_warn("-----------------------\n");
	log_warn("You are disconnected from the server. Try connecting again.\n");
}

// a membership change has occured. There are two cases:
// - server is disconnected
// - server has responded to connection request by joining to your mutual group
static int handle_membership_message(char *sender, int num_groups, membership_info *mem_info, int service_type)
{
	char username[20], garbage[20], chatroom_group_name[80];
	int serverID;
	log_debug("Handling membership change %s", mem_info->changed_member);
	sscanf(sender, "%[^_]_%d", username, &serverID);
	int ret = strcmp(username, current_session.username);
    log_debug("comparing group name %s with username ret = %d, service type = %d", username, ret, service_type);
	if (ret == 0)
	{
		// this is the group between me and the server
		if(Is_caused_leave_mess( service_type ) || Is_caused_disconnect_mess(service_type))
		{
			// server is disconnected
            log_debug("caused leave or disconnect");
			if(current_session.is_joined)
			{
				sprintf(chatroom_group_name, "CHATROOM_%s_%d", current_session.chatroom, current_session.connected_server);
				log_debug("leaving Spread group %s", chatroom_group_name);
				ret = SP_leave(Mbox, chatroom_group_name);
				if (ret < 0)
					SP_error(ret);
			}
			current_session.connected_server = 0;
			current_session.is_connected = 0;
			current_session.is_joined = 0;
			display_disconnection_to_user();
		}
		if(Is_caused_join_mess(service_type))
		{
			sscanf(mem_info->changed_member + 1, "%d%s", &serverID, garbage);
			if(serverID == current_session.connected_server && !current_session.is_connected)
			{
                E_dequeue( server_connect_timeout, 0, NULL );
				log_info("Successfully connected to server %d", current_session.connected_server);
				current_session.is_connected = 1;
			}
		}
	}
	return 0;
}

// iterate over messages and print them alonside participants
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
	for (i = 0; i < current_session.numOfMessages; i++) {
		printf("%d. %s: %s \t likes: %d\n", i+1, current_session.messages[i].userName,
				current_session.messages[i].message, current_session.messages[i].numOfLikes);
	}
	printf("----------\n");
	fflush(stdout);
	//Print_menu();
}

// an update is received from server. parse and update chatroom messages and display them to the user
static int handle_update_response(char *message, int size, int num_groups) {
	u_int32_t username_size, num_messages, messageSize;
	int offset = 5;
	int i, pointer = 0;
	log_debug("Handling client update message");
	memcpy(&current_session.numOfParticipants, message + 1, 4);
	log_debug("Parsed number of participants %d", current_session.numOfParticipants);
	for (i = 0; i < current_session.numOfParticipants; i++) {
		memcpy(&username_size, message + offset, 4);
        log_debug("username size is %d", username_size);
		memcpy(&current_session.listOfParticipants[i], message + offset + 4, username_size);
        current_session.listOfParticipants[i][username_size] = 0;
        log_debug("added %s to list of participants", current_session.listOfParticipants[i]);
		offset += (4+username_size);
	}
	memcpy(&num_messages, message + offset, 4);
    offset +=4;
	log_debug("Parsed number of messages %d", num_messages);
	for (i = 0; i < num_messages; i++){
		memcpy(&current_session.messages[i].serverID, message + offset + pointer, 4);
        log_debug("server ID = %d", current_session.messages[i].serverID);
		memcpy(&current_session.messages[i].lamportCounter, message + offset + pointer + 4, 4);
        log_debug("lamport = %d", current_session.messages[i].lamportCounter);
      
        memcpy(&username_size, message + offset + pointer + 8, 4); 
        memcpy(current_session.messages[i].userName, message + offset + pointer + 12, username_size);
        current_session.messages[i].userName[username_size] = 0;
        log_debug("message uname is %s", current_session.messages[i].userName);
        pointer += (12 + username_size);
		memcpy(&messageSize, message + offset + pointer, 4);
        log_debug("m size = %d", messageSize);
		memcpy(&current_session.messages[i].message, message + offset + pointer +4, messageSize);
        current_session.messages[i].message[messageSize] = 0;
		memcpy(&current_session.messages[i].numOfLikes, message + offset + pointer + 4 + messageSize, 4);
		pointer += (8 + messageSize);
	}
	current_session.numOfMessages = num_messages;
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
        if(list[i])
    		printf("Server %d\n",i+1);
	}
	printf("----------\n");
	fflush(stdout);
	Print_menu();
}

static int handle_membership_status_response(char *message, int size, int num_groups) {

	u_int32_t numOfMembers;
	memcpy(&numOfMembers, message + 1, 4);
	u_int32_t membersList[numOfMembers];
	int i;
	log_debug("Handling membership status response");
	for (i = 0; i < numOfMembers; i++) {
		memcpy(&membersList[i], message + 5 + (4 * i), 4);
	}
	displayMembershipStatus(membersList, numOfMembers);
	return 0;
}

static void displayHistory(Message *messages, u_int32_t num_of_messages){
	printf("\n");
	printf("==========\n");
	printf("room: %s\n", current_session.chatroom);
	int i;

	printf("----------\n");
	for (i = 0; i < num_of_messages; i++) {
		printf("%d. %s: %s \t likes: %d\n", i+1, messages[i].userName,
				messages[i].message, messages[i].numOfLikes);
	}
	printf("----------\n");
	fflush(stdout);
	Print_menu();
}

static int handle_history_response(char *message, int size) {
	u_int32_t username_size, num_messages, messageSize;
	int offset = 5;
	int i, pointer = 0;
	log_debug("Handling client history response message");
	Message messages[MAX_HISTORY_MESSAGES];

	memcpy(&num_messages, message + 1, 4);
	log_debug("Parsed number of messages %d", num_messages);
	for (i = 0; i < num_messages; i++){
		memcpy(&messages[i].serverID, message + offset + pointer, 4);
        log_debug("server ID = %d", &messages[i].serverID);
		memcpy(&messages[i].lamportCounter, message + offset + pointer + 4, 4);
        log_debug("lamport = %d", &messages[i].lamportCounter);
      
        memcpy(&username_size, message + offset + pointer + 8, 4); 
        memcpy(messages[i].userName, message + offset + pointer + 12, username_size);
        messages[i].userName[username_size] = 0;
        log_debug("message uname is %s", messages[i].userName);
        pointer += (12 + username_size);
		memcpy(&messageSize, message + offset + pointer, 4);
        log_debug("m size = %d", messageSize);
		memcpy(&messages[i].message, message + offset + pointer +4, messageSize);
        messages[i].message[messageSize] = 0;
		memcpy(&messages[i].numOfLikes, message + offset + pointer + 4 + messageSize, 4);
		pointer += (8 + messageSize);
	}
	displayHistory(messages, num_messages);
	return 0;
}
