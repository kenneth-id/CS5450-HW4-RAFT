#include "server.hpp"

Server::Server(){
    srand(time(NULL));
    QString n = QCoreApplication::arguments()[2];
    QString port = QCoreApplication::arguments()[3];
    max_udp_port = UDP_ROOT + n.toInt() - 1;
    num_servers = n.toInt();
    majority = qCeil(num_servers/2);
    message empty_message = {.msg_string = "", .msg_id =-1};
    QPair <message, quint16> dummy_pair(empty_message, 0);
    log.push_back(dummy_pair);

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
    reset_election_timer->start(get_bounded_random_number(150,300));

    heartbeat_timer = new QTimer(this);
    connect(heartbeat_timer, &QTimer::timeout, this, &Server::heartbeat_handler);
    heartbeat_timer->start(25);
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

    lastLogIndex = log.length();
    lastLogTerm = log.last().second;

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

void Server::heartbeat_handler(){
    if(state == leader){
        for(int i = UDP_ROOT; i <= max_udp_port; ++i){
            if (i != my_udp_port){
                quint16 n_index = next_index.value(i);

                uint16_t prevLogIndex;
                uint16_t prevLogTerm;
                QByteArray byte_array;
                uint16_t byte_size;
                char *byte_data;

                prevLogIndex = n_index - 1;
                prevLogTerm = log.value(prevLogIndex).second;
                // WARNING: check indexing
                byte_array = log.value(next_index.value(i) -1).first.msg_string.toLocal8Bit();
                byte_size = byte_array.size();
                byte_data = byte_array.data();
                
                datagram to_send = {
                    .type = appendEntries,
                    .id = my_udp_port,
                    .term = current_term,
                    .log_index = prevLogIndex,
                    .log_term = prevLogTerm,
                    .leader_commit = commit_index,
                    .text_data_id = log.value(next_index.value(i)-1).first.msg_id,
                    .text_data_len = byte_size,
                };
                memcpy(&to_send.text_data, byte_data, byte_size);

                send_datagram(to_send, i);
            }
        }
    }
}

void Server::reset_election_handler(){
    if(state != leader){
        become_candidate();
    }
    else{
        reset_election_timer->start(get_bounded_random_number(150,300));
    }
    // TODO: Handle in read incoming datagram
    // If AppendEntries RPC received from new leader: convert to follower
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
            requestVote_RPC_handler(incoming_datagram);
            break;
        case appendEntriesACK:
            break;
        case requestVoteACK:
            requestVoteACK_RPC_handler(incoming_datagram);
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

void Server::appendEntries_RPC_handler(datagram rpc){

    uint16_t leader_term = rpc.term;
    uint8_t leader_id = rpc.id;
    uint16_t leader_last_log_index = rpc.log_index;
    uint16_t leader_last_log_term = rpc.log_term;
    uint16_t leader_commit_index = rpc.commit_index;

    maybe_step_down(leader_term);

    if(leader_term < current_Term){
        send_appendEntries_RPC_response(false, leader_id);
        break;
    }
    else{
        // Leader is valid
        cur_leader = leader_id
        reset_election_timer->start(QRandomGenerator::global()->bounded(150,350));
    }

    // Handle out of index calls
    if(log.length() < leader_last_log_index){
        send_appendEntries_RPC_response(false, leader_id);
        break;
    }
    if(log[leader_last_log_index].second != leader_term){
        send_appendEntries_RPC_response(false, leader_id);
        break;
    }
    // We agree on the previous log term; truncate and append
    //Truncate log
    log = log.resize(leader_last_log_index)
    //Add to log
    incoming_string = QString::fromLocal8Bit(datagram.text_data, datagram.text_data_len).trimmed();
    log.append(incoming_string, leader_term)
    
    if(leader_commit_index > commit_index){
        commit_index = qMin(leader_commit_index, log.length());
    }
    send_appendEntries_RPC_response(true, leader_id);

}
void Server::requestVote_RPC_handler(datagram rpc){
    uint16_t candidate_term = rpc.term;
    uint8_t candidate_id = rpc.id;
    uint16_t candidate_last_log_index = rpc.log_index;
    uint16_t candidate_last_log_term = rpc.log_term;

    maybe_step_down(candidate_term);

    if(candidate_term < current_term){
        send_requestVote_RPC_response(false, candidate_id);
    }
    else{
        if((voted_for == -1 || voted_for == candidate_id) && candidate_last_log_index >= log.length() ){
            send_requestVote_RPC_response(true, candidate_id);
            voted_for = candidate_term;
            // Warning: May need to reset election timer here.
        }
        else{
            send_requestVote_RPC_response(false, candidate_id);
        }
    }
}

void Server::requestVoteACK_RPC_handler(datagram rpc){
    uint16_t remote_term = rpc.term;
    bool vote_granted = rpc.success_ack;

    maybe_step_down(remote_term);

    if(state == candidate && current_term == remote_term && vote_granted){
        num_votes_for_me++;

        if(num_votes_for_me > majority){
            become_leader();
        }
    }
}

qint16 Server::send_requestVote_RPC_response(bool success, quint16 port){
    datagram to_send = {
        .term_ack = current_term,
        .success_ack = success
    };

    return send_datagram(to_send, port);
}

qint16 Server::send_appendEntries_RPC_response(bool success, quint16 port){
    datagram to_send = {
        .term_ack = current_term,
        .success_ack = success
    };

    return send_datagram(to_send, port);
}

qint16 Server::send_datagram(datagram data, quint16 port){
    qint64 size_datagram = sizeof(data);
    char *datagram = (char *)calloc(1, size_datagram);
    memcpy(datagram, &data, size_datagram);

    qint64 err = udp_socket->writeDatagram(datagram, size_datagram, QHostAddress("127.0.0.1"), port);
    free(datagram);

    return err;
}

int Server::get_bounded_random_number(int min, int max){
    return min + (std::rand() % (max-min+1));
}

void Server::maybe_step_down(quint16 remote_term){
    if(remote_term > current_term){
        advance_term(remote_term);
        become_follower();
    }
}

void Server::advance_term(quint16 term){
    current_term = term;
    voted_for = -1;
}

void Server::become_follower(){
    state = follower;
    next_index.clear();
    match_index.clear();
    cur_leader = -1;
    num_votes_for_me = 0;
    reset_election_timer->start(get_bounded_random_number(150,300));
}

void Server::become_candidate(){
    state = candidate;
    advance_term(++current_term);
    voted_for = my_udp_port;
    num_votes_for_me++;
    cur_leader = -1;
    reset_election_timer->start(get_bounded_random_number(150,300));
    broadcast_requestVote();
}

void Server::become_leader(){
    state = leader;
    cur_leader = my_udp_port;
    num_votes_for_me = 0;
    for(int i = UDP_ROOT; i <= max_udp_port; ++i){
        if (i != my_udp_port){
            next_index.insert(i, log.length()); //WARNING: check indexing
            match_index.insert(i, 0);
        }
    }
    heartbeat_timer->start(0);
}
