#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include "log.h"
#include "fileService.h"

static char *get_filename_ext(const char *filename) {
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
		log_debug("chatroom %s has %d message(s)", chatroom, msg_pointer);
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

static void_rebuild_lts_data(u_int32_t index)
{
    current_session.chatroom[index].
}

static void create_chatroom_from_logs()
{
	DIR *directory;
	struct dirent* file;
	FILE *cf;
    char ch;
    char * line = NULL;
    size_t len = 0;
    ssize_t read;
	char chatroom_name[20];
    Message m;
    u_int32_t index;
	directory = opendir(".");
    if (directory == NULL) {
        log_error("error opening base directory");
        exit(2);
    }

    while ((file=readdir(directory)) != NULL) {
        log_debug("filename: %s\n", file->d_name);
		if(strcmp("chatroom", get_filename_ext(file->d_name)) == 0)
		{
			sscanf(file->d_name, "%[^.].chatroom", chatroom_name);
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


int main()
{
    create_chatroom_from_logs();
}