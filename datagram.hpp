#pragma once

#include <stdint.h>

#define MAX_NUM_SERVERS 5
#define MAX_MSG_LEN 200

/*----- Message Types -----*/
enum datagram_type {
    appendEntries=0,
    requestVote,
    appendEntriesACK,
    requestVoteACK,
    forwardedMsg
};

struct datagram {
    datagram_type type;
    
    uint16_t id;
    uint16_t term;
    uint16_t log_index;
    uint16_t log_term;
    uint16_t leader_commit;

    int text_data_id;
    uint16_t text_data_len;
    char text_data[MAX_MSG_LEN]; //MAX_MSG_LEN is defined to be 200

    uint16_t term_ack;
    bool success_ack;
};
