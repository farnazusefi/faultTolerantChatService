#include "sp.h"

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define int32u unsigned int

#define MAX_MESSLEN     102400
#define MAX_VSSETS      10
#define MAX_MEMBERS     100

///////////////////////// Data Structures   //////////////////////////////////////////////////////


typedef struct Session_t {
	u_int32_t logged_in;
	u_int32_t connected_server;
	u_int32_t is_joined;

	u_int32_t username;
	u_int32_t chatroom;

} Session ;

///////////////////////// Global Variables //////////////////////////////////////////////////////

Session current_session;
static	char	User[80];
static  char    Spread_name[80];

static  char    Private_group[MAX_GROUP_NAME];
static  mailbox Mbox;
static	int	Num_sent;

static  int     To_exit = 0;

//////////////////////////   Declarations    ////////////////////////////////////////////////////


static	void	Print_menu();
static	void	User_command();
static	void	Read_message();
static	void	Usage( int argc, char *argv[] );
static  void    Print_help();
static  void	Bye();

static int handle_login(char* username);
static int handle_connect(int server_id);
static int join(char* chatroom);
static int handle_append(char* message, int size);
static int handle_like(int line_number);
static int handle_unlike(int line_number);
static int handle_history();
static int handle_membership_status();

static int parse(char *message, int size, int num_groups, char** groups);
static int handle_membership_message(int num_groups, char** groups, struct membership_info *mem_info);
static int handle_update_response();
static int handle_membership_status_response();

//////////////////////////   Core Functions  ////////////////////////////////////////////////////


int main( int argc, char *argv[] )
{
	int     ret;
    int     mver, miver, pver;
    sp_time test_timeout;

    test_timeout.sec = 5;
    test_timeout.usec = 0;

	Usage( argc, argv );
    if (!SP_version( &mver, &miver, &pver))
    {
	    printf("main: Illegal variables passed to SP_version()\n");
	    Bye();
	}
	printf("Spread library version is %d.%d.%d\n", mver, miver, pver);

	ret = SP_connect_timeout( Spread_name, User, 0, 1, &Mbox, Private_group, test_timeout );
	if( ret != ACCEPT_SESSION ) 
	{
		SP_error( ret );
		Bye();
	 }
	 printf("User: connected to %s with private group %s\n", Spread_name, Private_group );

	 E_init();

	 E_attach_fd( 0, READ_FD, User_command, 0, NULL, LOW_PRIORITY );

	 E_attach_fd( Mbox, READ_FD, Read_message, 0, NULL, HIGH_PRIORITY );

	 Print_menu();

	 printf("\nUser> ");
	 fflush(stdout);

	 Num_sent = 0;

	 E_handle_events();

	 return( 0 );
}

static void	User_command()
{
	char	command[130];  //
	char	mess[MAX_MESSLEN];  //
	char	argument[80]; //
	unsigned int	mess_len; //
	int	ret; //
	int	i; //
	int server_id, line_id;

	for( i=0; i < sizeof(command); i++ ) command[i] = 0;
	if( fgets( command, 130, stdin ) == NULL ) 
            Bye();

	switch( command[0] )
	{
		case 'u':	// login with username
			ret = sscanf( &command[2], "%s", argument );
			if( ret < 1 )
			{
				printf(" invalid username \n");
				break;
			}
			handle_login(argument);
			break;

		case 'c':	// connect to server [1-5]
			ret = sscanf( &command[2], "%s", argument );
			if( ret < 1 )
			{
				printf(" invalid server id \n");
				break;
			}
			if(current_session.logged_in)
			{
				server_id = atoi(argument);
				handle_connect(server_id);
			}
			else{
				printf(" not logged in yet! \n");
			}
			break;

		case 'j':	// join a chatroom
			ret = sscanf( &command[2], "%s", argument );
			if( ret < 1 ) 
			{
				printf(" invalid chatroom \n");
				break;
			}
			if(current_session.logged_in)
			{
				if(current_session.connected_server)
					join(argument);
				else
					printf(" not connected to any server! \n");
			}
			else{
				printf(" not logged in yet! \n");
			}
			break;

		case 'a':	// append to chatroom
			if(!current_session.logged_in){
				printf(" not logged in yet! \n");
				break;
			}
			if(!current_session.connected_server){
				printf(" not connected to any server \n");
				break;
			}
			if(!current_session.is_joined){
				printf(" not joined any chatroom yet! \n");
				break;
			}
			printf("enter message: ");
			if (fgets(mess, 200, stdin) == NULL)
				Bye();
			mess_len = strlen( mess );
			handle_append(mess, mess_len);
			break;

		case 'l':	// like a message
			ret = sscanf( &command[2], "%s", argument );
			if( ret < 1 )
			{
				printf(" invalid chatroom \n");
				break;
			}
			if(!current_session.logged_in){
				printf(" not logged in yet! \n");
				break;
			}
			if(!current_session.connected_server){
				printf(" not connected to any server \n");
				break;
			}
			if(!current_session.is_joined){
				printf(" not joined any chatroom yet! \n");
				break;
			}
			line_id = atoi(argument);
			handle_like(line_id);
			break;

		case 'r':		// unlike a message
			ret = sscanf( &command[2], "%s", argument );
			if( ret < 1 )
			{
				printf(" invalid chatroom \n");
				break;
			}
			if(!current_session.logged_in){
				printf(" not logged in yet! \n");
				break;
			}
			if(!current_session.connected_server){
				printf(" not connected to any server \n");
				break;
			}
			if(!current_session.is_joined){
				printf(" not joined any chatroom yet! \n");
				break;
			}
			line_id = atoi(argument);
			handle_unlike(line_id);
			break;

		case 'h':		// chatroom history
			if(!current_session.logged_in){
				printf(" not logged in yet! \n");
				break;
			}
			if(!current_session.connected_server){
				printf(" not connected to any server \n");
				break;
			}
			if(!current_session.is_joined){
				printf(" not joined any chatroom yet! \n");
				break;
			}
			handle_history();
			break;

		case 'v':
			if(!current_session.logged_in){
				printf(" not logged in yet! \n");
				break;
			}
			if(!current_session.connected_server){
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

static void	Print_menu()
{
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

static void	Read_message()
{

	static char	 mess[MAX_MESSLEN];
	char	 sender[MAX_GROUP_NAME];
	char	 target_groups[MAX_MEMBERS][MAX_GROUP_NAME];
	membership_info  memb_info;
	vs_set_info      vssets[MAX_VSSETS];
	unsigned int     my_vsset_index;
	int      num_vs_sets;
	char     members[MAX_MEMBERS][MAX_GROUP_NAME];
	int		 num_groups;
	int		 service_type;
	int16	 mess_type;
	int		 endian_mismatch;
	int		 i,j;
	int		 ret;

	service_type = 0;

	ret = SP_receive( Mbox, &service_type, sender, 100, &num_groups, target_groups, 
		&mess_type, &endian_mismatch, sizeof(mess), mess );
	if( ret < 0 ) 
	{
		if ( (ret == GROUPS_TOO_SHORT) || (ret == BUFFER_TOO_SHORT) ) {
				service_type = DROP_RECV;
				printf("\n========Buffers or Groups too Short=======\n");
				ret = SP_receive( Mbox, &service_type, sender, MAX_MEMBERS, &num_groups, target_groups,
								  &mess_type, &endian_mismatch, sizeof(mess), mess );
		}
	}
	if (ret < 0 )
	{
		if( !To_exit )
		{
			SP_error( ret );
			printf("\n============================\n");
			printf("\nBye.\n");
		}
		exit( 0 );
	}
	if( Is_regular_mess( service_type ) )
	{
		parse(mess, ret, num_groups, &target_groups);

	}else if( Is_membership_mess( service_type ) )
    {
		ret = SP_get_memb_info( mess, service_type, &memb_info );
		if (ret < 0) {
			printf("BUG: membership message does not have valid body\n");
			SP_error( ret );
			exit( 1 );
		}
		if ( Is_reg_memb_mess( service_type ) )
		{
			handle_membership_message(num_groups, &target_groups, &memb_info);

		}else if( Is_transition_mess(   service_type ) ) {
			printf("received TRANSITIONAL membership for group %s\n", sender );
		}else if( Is_caused_leave_mess( service_type ) ){
			printf("received membership message that left group %s\n", sender );
		}else printf("received incorrecty membership message of type 0x%x\n", service_type );
	} else if ( Is_reject_mess( service_type ) )
	{
		printf("REJECTED message from %s, of servicetype 0x%x messtype %d, (endian %d) to %d groups \n(%d bytes): %s\n",
			sender, service_type, mess_type, endian_mismatch, num_groups, ret, mess );
	}else printf("received message of unknown message type 0x%x with ret %d\n", service_type, ret);


	printf("\n");
	printf("User> ");
	fflush(stdout);
}

static void	Usage(int argc, char *argv[])
{
	sprintf( User, "user" );
	sprintf( Spread_name, "4803");
	while( --argc > 0 )
	{
		argv++;

		if( !strncmp( *argv, "-u", 2 ) )
		{
			if (argc < 2) Print_help();
			strcpy( User, argv[1] );
			argc--; argv++;
		}else if( !strncmp( *argv, "-r", 2 ) )
		{
			strcpy( User, "" );
		}else if( !strncmp( *argv, "-s", 2 ) ){
                        if (argc < 2) Print_help();
			strcpy( Spread_name, argv[1] ); 
			argc--; argv++;
		}else{
            Print_help();
        }
	 }
}
static void Print_help()
{
    printf( "Usage: spuser\n%s\n%s\n%s\n",
            "\t[-u <user name>]  : unique (in this machine) user name",
            "\t[-s <address>]    : either port or port@machine",
            "\t[-r ]    : use random user name");
    exit( 0 );
}
static void Bye()
{
	To_exit = 1;

	printf("\nBye.\n");

	SP_disconnect( Mbox );

	exit( 0 );
}

//////////////////////////   User Event Handlers ////////////////////////////////////////////////////

static int handle_login(char* username)
{

	return 0;
}

static int handle_connect(int server_id)
{

	return 0;
}

static int join(char* chatroom)
{

	return 0;
}

static int handle_append(char* message, int size)
{

	return 0;
}

static int handle_like(int line_number)
{

	return 0;
}

static int handle_unlike(int line_number)
{

	return 0;
}

static int handle_history()
{

	return 0;
}
static int handle_membership_status()
{

	return 0;
}

//////////////////////////   Server Event Handlers ////////////////////////////////////////////////////

static int parse(char *message, int size, int num_groups, char** groups)
{

	return 0;
}

static int handle_membership_message(int num_groups, char** groups, struct membership_info *mem_info)
{

	return 0;
}

static int handle_update_response()
{

	return 0;
}

static int handle_membership_status_response()
{

	return 0;
}
