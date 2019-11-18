#include "sp.h"

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"


#define int32u unsigned int

#define MAX_MESSLEN     102400
#define MAX_VSSETS      10
#define MAX_MEMBERS     100

///////////////////////// Data Structures   //////////////////////////////////////////////////////

enum MessageType {
	TYPE_APPEND = 'a',
	TYPE_JOIN = 'j',
	TYPE_LIKE = 'l',
	TYPE_UNLIKE = 'r',
	TYPE_HISTORY = 'h',
	TYPE_MEMBERSHIP_STATUS = 'v',
	TYPE_CLIENT_UPDATE = 'c',
	TYPE_MEMBERSHIP_STATUS_RESPONSE = 'm',
	TYPE_SERVER_UPDATE = 'u',
	TYPE_ANTY_ENTROPY = 'e'
}

typedef struct Session_t {
	u_int32_t server_id;

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

static int initialize();

static int handle_disconnect(char* username);
static int handle_connect(int server_id);
static int handle_join(char* chatroom);
static int handle_append(char* message, int size);
static int handle_like(int line_number);
static int handle_unlike(int line_number);
static int handle_history();
static int handle_membership_status();

static int parse(char *message, int size, int num_groups, char** groups);
static int handle_server_update();
static int handle_anti_entropy();
static int handle_membership_change();


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

	 E_attach_fd( Mbox, READ_FD, Read_message, 0, NULL, HIGH_PRIORITY );

	 start_server();

	 E_handle_events();

	 return( 0 );
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
	sprintf( Spread_name, "225.1.3.30:10330");
	if(argc != 2){
		printf("Usage: ./server [server_id 1-5]");
		exit(0);
	}
	current_session.server_id = atoi(argv[1]);
	
}

static void Bye()
{
	To_exit = 1;

	printf("\nBye.\n");

	SP_disconnect( Mbox );

	exit( 0 );
}

static int parse(char *message, int size, int num_groups, char** groups)
{

	return 0;
}

static int initialize()
{

	return 0;
}

//////////////////////////   User Event Handlers ////////////////////////////////////////////////////

static int handle_disconnect(char* username)
{

	return 0;
}

static int handle_connect(int server_id)
{

	return 0;
}

static int handle_join(char* chatroom)
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


static int handle_server_update()
{

	return 0;
}

static int handle_anti_entropy()
{

	return 0;
}

static int handle_membership_change()
{

	return 0;
}
