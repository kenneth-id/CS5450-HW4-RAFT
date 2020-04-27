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

        // Persistent state
        quint16 current_term = 0;
        qint16 voted_for = -1;
        QVector<QPair<QString, quint16>> log;
        QStringList chat_history;

        // Volatile state
        quint16 commit_index = 0;
        quint16 last_applied = 0;
        server_state state = follower;

        // Volatile state on leaders
        QMap<quint16, quint16> next_index;
        QMap<quint16, quint16> match_index;

        // Network functions
        qint16 send_datagram(datagram data, quint16 port);

        // Utilitiy functions
        QString get_string_from_datagram(datagram data);
};
