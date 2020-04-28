#include "server.hpp"

Server::Server(){
    srand(time(NULL));
    QString n = QCoreApplication::arguments()[2];
    QString port = QCoreApplication::arguments()[3];
    max_udp_port = UDP_ROOT + n.toInt() - 1;
    num_servers = n.toInt();

    tcp_server = new QTcpServer(this);
    tcp_server->listen(QHostAddress("127.0.0.1"), port.toUInt());
    qDebug() << "INFO: TCP Server listening to port " << port;
    connect(tcp_server, &QTcpServer::newConnection, this, &Server::new_tcp_connection_handler);

    udp_socket = new QUdpSocket(this);
    for(int i = UDP_ROOT; i<=max_udp_port; ++i){
        if (udp_socket->bind(i)) {
            qDebug() << "INFO: bound to UDP port " << i;
            my_udp_port = i;
            break;
        }
    }
    connect(udp_socket, &QUdpSocket::readyRead, this, &Server::read_incoming_datagram);

    reset_election_timer = new QTimer(this);
    connect(reset_election_timer, &QTimer::timeout, this, &Server::reset_election_handler);
    reset_election_timer->start(QRandomGenerator::global()->bounded(150,350));

    heartbeat_timer = new QTimer(this);
    connect(heartbeat_timer, &QTimer::timeout, this, &Server::heartbeat_handler);
}

Server::~Server(){
    delete(udp_socket);
    delete(tcp_server);
    delete(tcp_socket);
    delete(heartbeat_timer);
    delete(reset_election_timer);
}

bool Server::broadcast_requestVote(){
    uint16_t lastLogIndex;
    uint16_t lastLogTerm;

    if(log.length() == 0){
        lastLogIndex = 0;
        lastLogTerm = 0;
    }
    else{
        lastLogIndex = log.length();
        lastLogTerm = log.last().second;
    }

    datagram to_broadcast = {
        .type = requestVote,
        .id = my_udp_port,
        .term = current_term,
        .log_index = lastLogIndex,
        .log_term = lastLogTerm,
    };

    for(int i = UDP_ROOT; i <= max_udp_port; ++i){
        if (i != my_udp_port){
            send_datagram(to_broadcast, i);
        }
    }
}

void Server::reset_election_handler(){
    state = candidate;
    current_term++;
    voted_for = my_udp_port;
    reset_election_timer->start(QRandomGenerator::global()->bounded(150,350));
    broadcast_requestVote();

    // TODO: Handle in read incoming datagram
    // If votes received from majority of servers: become leader
    // If AppendEntries RPC received from new leader: convert to follower
    // If election timeout elapses: start new election
}

void Server::new_tcp_connection_handler(){
    tcp_socket = tcp_server->nextPendingConnection();
    connect(tcp_socket, &QAbstractSocket::disconnected,tcp_socket, &QObject::deleteLater);
    connect(tcp_socket, &QTcpSocket::readyRead, this, &Server::read_incoming_stream);
}


void Server::read_incoming_stream(){
    QByteArray input;
    input = tcp_socket->readAll();
    qDebug() << "Server with UDP port" << my_udp_port <<"received from proxy: " << input;
    QString raw_msg = QString(input);

    if (raw_msg.contains("msg") == true){
        qDebug() << "Received a new message from proxy";

        QString text_msg = raw_msg.split(" ")[2];

        // TODO: what to do if leader? what if follower? what if candidate?
    }

    else if (raw_msg.contains("get") == true){
        qDebug() << "Received request for chatlog from proxy";
        // TODO: what to do if leader? what if follower? what if candidate?
        QString api_formatted;
        if(chat_history.isEmpty()){
            api_formatted = "chatLog\n";
        }
        else{
            QString comma_separated_log = chat_history.join(",");
            api_formatted = "chatLog " + comma_separated_log + "\n";

        }
        tcp_socket->write(api_formatted.toUtf8());
    }

    else if (raw_msg.contains("crash") == true){
        qDebug() << "Received crash order from proxy";
        QCoreApplication::quit();
    }

    else{
        qDebug() << "Received unknown API from proxy";
    }
}

void Server::read_incoming_datagram(){
    datagram incoming_datagram;
    qint64 datagram_size = udp_socket->pendingDatagramSize();
    qint64 err = udp_socket->readDatagram((char*)&incoming_datagram, datagram_size);

    if(err != -1){
        switch (incoming_datagram.type){
        case appendEntries:
            break;
        case requestVote:
            break;
        case appendEntriesACK:
            break;
        case requestVoteACK:
            break;
        default:
            qDebug() << "ERROR: Incoming datagram type invalid";
            break;
        }
    }
    else{
        qDebug() << "Failed to read datagram";
    }
}

qint16 Server::send_datagram(datagram data, quint16 port){
    qint64 size_datagram = sizeof(data);
    char *datagram = (char *)calloc(1, size_datagram);
    memcpy(datagram, &data, size_datagram);

    qint64 err = udp_socket->writeDatagram(datagram, size_datagram, QHostAddress("127.0.0.1"), port);
    free(datagram);

    return err;
}

QString Server::get_string_from_datagram(datagram data){

}
