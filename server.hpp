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

        quint16 max_udp_port;
        quint16 my_udp_port;
        quint16 num_servers;
        quint16 majority;

        // Persistent state
        quint16 current_term = 0;
        qint16 voted_for = -1;
        QVector<QPair<message, quint16>> log; //QPair is chat_string first then term
        QStringList chat_history;

        // Volatile state
        quint16 commit_index = 0;
        quint16 last_applied = 0;
        server_state state = follower;
        qint16 cur_leader = -1;
        quint16 num_votes_for_me =0; //Used for 

        // Volatile state on leaders
        QMap<quint16, quint16> next_index;
        QMap<quint16, quint16> match_index;

        // Network functions
        qint16 send_datagram(datagram data, quint16 port);

        // Utility functions
        QString get_string_from_datagram(datagram data);
        bool broadcast_requestVote();
        int get_bounded_random_number(int min, int max);

        // RPC handling functions
        void requestVote_RPC_handler(datagram rpc);
        void requestVoteACK_RPC_handler(datagram rpc);
        void appendEntries_RPC_handler(datagram rpc);

        // RPC handling utility functions
        qint16 send_requestVote_RPC_response(bool success, quint16 port);
        qint16 send_appendEntries_RPC_response(bool success, quint16 port);
        void maybe_step_down(quint16 remote_term);
        void advance_term(quint16 remote_term);

        // State change updates utility function 
        void become_follower();
        void become_candidate();
        void become_leader();
};
