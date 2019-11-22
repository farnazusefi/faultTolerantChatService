#include <stdio.h>
#include <stdlib.h>
#include "fileService.h"

int main()
{
    u_int32_t me = 3;
    create_log_files(me, 5);

    create_chatroom_file(me, "hopnets");

    logEvent e, e2;
    e.lamportCounter = 234234;
    e.eventType = 'a';
    sprintf(e.payload, "%s", "salam");
    // sprintf(e.ad, "%s", "erfan,farnaz");

    addEventToLogFile(2, e);

    parseLineInLogFile("234234~a~salam khoobi? ~ q", &e2);
    printf("l counter = %d, payload = %s, info = %s, type = %c\n", e2.lamportCounter, e2.payload, e2.additionalInfo, e2.eventType);
    Message m, m2;
    sprintf(m.userName, "%s", "erfan");
    m.serverID = 5;
    sprintf(m.message, "%s", "how are you");
    m.lamportCounter = 14;
    sprintf(m.additionalInfo, "%s", "");
    log_info("1111111");
    addMessageToChatroomFile(me, "hopnets", m);
    log_info("222222");
    parseLineInMessagesFile("5~14~erfan~how are you~mamad,reza,ali", &m2);
    log_info("counter = %d, message = %s, info = %s, serverID = %d\n", m2.lamportCounter, m2.message, m2.additionalInfo, m2.serverID);

    

    return 0;
}