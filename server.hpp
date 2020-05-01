#pragma once

#include <QtCore>
#include <QtNetwork>
#include "datagram.hpp"

#define UDP_ROOT 20000

enum server_state {
    leader = 0,
    candidate,
    follower
};

struct message
{
    QString msg_string;
    qint16 msg_id;
};

class Server : public QObject{
    Q_OBJECT

    public:
        Server();
        ~Server();
        
    private slots:
        void reset_election_handler();
        void new_tcp_connection_handler();
        void heartbeat_handler();
        void read_incoming_stream();
        void read_incoming_datagram();

    private:
        // Networking objects
        QUdpSocket *udp_socket;
        QTcpServer *tcp_server;
        QTcpSocket *tcp_socket;

        QTimer *reset_election_timer;
        QTimer *heartbeat_timer;

        uint16_t max_udp_port;
        uint16_t my_udp_port;
        uint16_t num_servers;
        uint16_t majority;

        // Persistent state
        uint16_t current_term = 0;
        qint16 voted_for = -1;
        QVector<QPair<message, uint16_t>> log; //QPair is chat_string first then term
        QStringList chat_history;
        QMap <int, QString> applied_msg_ids;

        // Volatile state
        uint16_t commit_index = 0;
        uint16_t last_applied = 0;
        server_state state = follower;
        int cur_leader = -1;
        uint16_t num_votes_for_me =0; //Used for 
        QVector <message> forward_buffer;
        QVector <int> message_ids_to_ack;

        // Volatile state on leaders
        QMap <uint16_t, uint16_t> next_index;
        QMap <uint16_t, uint16_t> match_index;
        QMap <int, uint16_t> replication_count;

        // Network functions
        qint64 send_datagram(datagram data, uint16_t port);

        // Utility functions
        QString get_string_from_datagram(datagram data);
        void broadcast_requestVote();
        int get_bounded_random_number(int min, int max);
        void maybe_apply();
        void maybe_forward_message();
        void maybe_ack_message();
        void debug_datagram(datagram data);

        // RPC handling functions
        void requestVote_RPC_handler(datagram rpc);
        void requestVoteACK_RPC_handler(datagram rpc);
        void appendEntries_RPC_handler(datagram rpc);
        void appendEntriesACK_RPC_handler(datagram rpc);
        void forwardedMsg_handler(datagram rpc);

        // RPC handling utility functions
        qint64 send_requestVote_RPC_response(bool success, uint16_t port);
        qint64 send_appendEntries_RPC_response(int text_id, QString text_string, bool success, uint16_t port);
        void maybe_step_down(uint16_t remote_term);
        void advance_term(uint16_t remote_term);

        // State change updates utility function 
        void become_follower();
        void become_candidate();
        void become_leader();
};
