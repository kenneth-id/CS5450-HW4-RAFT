#include "server.hpp"

Server::Server(){
    srand(time(NULL));
    QString n = QCoreApplication::arguments()[2];
    QString port = QCoreApplication::arguments()[3];
    max_udp_port = UDP_ROOT + n.toInt() - 1;
    num_servers = n.toInt();
    majority = qFloor(num_servers/2) + 1;
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
    qDebug () << "Destructor invoked!";
    delete(udp_socket);
    delete(tcp_server);
    delete(tcp_socket);
    delete(heartbeat_timer);
    delete(reset_election_timer);
}

void Server::broadcast_requestVote(){
    uint16_t lastLogIndex;
    uint16_t lastLogTerm;

    lastLogIndex = log.length()-1;
    lastLogTerm = log.last().second;

    datagram to_broadcast = {
        .type = requestVote,
        .id = my_udp_port,
        .term = current_term,
        .log_index = lastLogIndex,
        .log_term = lastLogTerm,
    };

    // debug_datagram(to_broadcast);

    for(int i = UDP_ROOT; i <= max_udp_port; ++i){
        if (i != my_udp_port){
            send_datagram(to_broadcast, i);
        }
    }
}

void Server::heartbeat_handler(){
    if(state == leader){
        qDebug() << next_index;
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
                byte_array = log.value(next_index.value(i)).first.msg_string.toLocal8Bit();
                byte_size = byte_array.size();
                byte_data = byte_array.data();
                
                datagram to_send = {
                    .type = appendEntries,
                    .id = my_udp_port,
                    .term = current_term,
                    .log_index = prevLogIndex,
                    .log_term = prevLogTerm,
                    .leader_commit = commit_index,
                    .text_data_id = log.value(next_index.value(i)).first.msg_id,
                    .text_data_len = byte_size,
                };
                memcpy(&to_send.text_data, byte_data, byte_size);
                send_datagram(to_send, i);
                // qDebug() << my_udp_port << "Broadcast appendEntryRPC with content:";
                // debug_datagram(to_send);
            }
        }
        heartbeat_timer->start(25);
    }
    else if(state == follower){
        maybe_forward_message();
    }
    maybe_apply();
    maybe_ack_message();
}

void Server::reset_election_handler(){
    qDebug() << my_udp_port << "reset election handler triggered";
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
    qDebug() << "Server:" << my_udp_port <<"received from proxy: " << input;
    QString raw_msg = QString(input);

    if (raw_msg.contains("msg") == true){
        qDebug() << "Received a new message from proxy";

        int text_id = raw_msg.split(" ")[1].toInt();
        QString text_msg = raw_msg.split(" ")[2].trimmed();
        message new_msg = {.msg_string = text_msg, .msg_id = text_id};
        message_ids_to_ack.push_back(text_id);
        // TODO: what to do if leader? what if follower? what if candidate?
        if(state == leader){
            log.push_back(QPair<message, quint16>(new_msg, current_term));
            replication_count[text_id]++;
        }
        else{
            forward_buffer.push_back(new_msg);
        }
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
        qDebug() << api_formatted;
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
            qDebug() << my_udp_port <<" received appendEntries RPC";
            // debug_datagram(incoming_datagram);
            appendEntries_RPC_handler(incoming_datagram);
            break;
        case requestVote:
            qDebug() << my_udp_port <<" received requestVote RPC";
            // debug_datagram(incoming_datagram);
            requestVote_RPC_handler(incoming_datagram);
            break;
        case appendEntriesACK:
            qDebug() << my_udp_port <<" received appendEntriesACK RPC";
            // debug_datagram(incoming_datagram);
            reset_election_timer->start(get_bounded_random_number(150,300));
            appendEntriesACK_RPC_handler(incoming_datagram);
            break;
        case requestVoteACK:
            qDebug() << my_udp_port <<" received requestVoteACK RPC";
            // debug_datagram(incoming_datagram);
            requestVoteACK_RPC_handler(incoming_datagram);
            break;
        case forwardedMsg:
            qDebug() << my_udp_port <<" received forwardedMsg";
            forwardedMsg_handler(incoming_datagram);
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
    uint16_t leader_id = rpc.id;
    uint16_t leader_prev_log_index = rpc.log_index;
    uint16_t leader_prev_log_term = rpc.log_term;
    uint16_t leader_commit_index = rpc.leader_commit;
    int text_id = rpc.text_data_id;
    QString incoming_string = QString::fromLocal8Bit(rpc.text_data, rpc.text_data_len).trimmed();

    maybe_step_down(leader_term);

    if(leader_term < current_term){
        send_appendEntries_RPC_response(text_id, "", false, leader_id);
        qDebug() << "Sent appendEntries RPC response false_1";
        return;
    }
    else{
        // Leader is valid
        cur_leader = leader_id;
        reset_election_timer->start(get_bounded_random_number(150,300));
    }

    // Handle out of index calls
    if(log.length() -1 < leader_prev_log_index){ //WARNING: check the index
        send_appendEntries_RPC_response(text_id, "", false, leader_id);
        qDebug() << "Sent appendEntries RPC response false_2";
        return;
    }

    //  Reply false if log doesn’t contain an entry at prevLogIndex whose term matches prevLogTerm
    if(log[leader_prev_log_index].second != leader_prev_log_term){
        send_appendEntries_RPC_response(text_id, "", false, leader_id);
        qDebug() << "Sent appendEntries RPC response false_3";
        return;
    }
    // We agree on the previous log term; truncate and append
    //Truncate log
    // WARNING: Should'nt there be an if statement before truncation?

    if(incoming_string != ""){
        log.resize(leader_prev_log_index + 1); //WARNING: check the index
        //Add to log
        message incoming_msg = {.msg_string = incoming_string, .msg_id = rpc.text_data_id};
        log.push_back(QPair<message, quint16>(incoming_msg, leader_term));
    }

    if(leader_commit_index > commit_index){
        qDebug() << "Advancing follower commit index!";
        commit_index = qMin((int)leader_commit_index, log.length() -1);
    }
    send_appendEntries_RPC_response(text_id, incoming_string, true, leader_id);
}

void Server::appendEntriesACK_RPC_handler(datagram rpc){
    uint16_t remote_id = rpc.id;
    uint16_t remote_term = rpc.term_ack;
    bool success = rpc.success_ack;
    int text_id = rpc.text_data_id;
    QString incoming_string = QString::fromLocal8Bit(rpc.text_data, rpc.text_data_len).trimmed();

    maybe_step_down(remote_term);

    if(state == leader && remote_term == current_term){
        if(success){
            if(incoming_string != ""){
                ++next_index[remote_id];
                ++match_index[remote_id];
                ++replication_count[text_id];

                if(replication_count[text_id] >= majority){
                    // If there exists an N such that N > commitIndex, a majority
                    // of matchIndex[i] ≥ N, and log[N].term == currentTerm:
                    // set commitIndex = N
                    // WARNING: Is this the same? because we only have 1 new entry at a time.
                    commit_index = qMin(commit_index + 1, log.length() - 1);
                    maybe_apply();
                }
            }
            
        }
        else{
            next_index[remote_id] = qMax(next_index[remote_id] - 1, 1);
        }
    }
}

void Server::forwardedMsg_handler(datagram rpc){
    if(state == leader){
        QString remote_string = QString::fromLocal8Bit(rpc.text_data, rpc.text_data_len).trimmed();
        int text_id = rpc.text_data_id;
        message new_msg = {.msg_string = remote_string, .msg_id = text_id};
        log.push_back(QPair<message,int>(new_msg, current_term));
        replication_count[text_id]++;
    }
}

void Server::requestVote_RPC_handler(datagram rpc){
    uint16_t candidate_term = rpc.term;
    uint16_t candidate_id = rpc.id;
    uint16_t candidate_last_log_index = rpc.log_index;
    uint16_t candidate_last_log_term = rpc.log_term;

    maybe_step_down(candidate_term);

    if(candidate_term < current_term){
        send_requestVote_RPC_response(false, candidate_id);
        qDebug() << my_udp_port <<": reply failed_1 requestVote to "<< candidate_id;
    }
    else{
        // Warning: It is important to implement the “up-to-date log” check exactly as described in section 5.4.
        if((voted_for == -1 || voted_for == candidate_id) && candidate_last_log_index >= log.length() -1 ){
            voted_for = candidate_term;
            send_requestVote_RPC_response(true, candidate_id);
            qDebug() << my_udp_port <<": reply successful requestVote to "<< candidate_id;
            // Warning: May need to reset election timer here.
        }
        else{
            send_requestVote_RPC_response(false, candidate_id);
            qDebug() << my_udp_port <<": reply failed_2 requestVote to "<< candidate_id;
        }
    }
}

void Server::requestVoteACK_RPC_handler(datagram rpc){
    uint16_t remote_term = rpc.term_ack;
    bool vote_granted = rpc.success_ack;

    maybe_step_down(remote_term);

    // debug_datagram(rpc);

    if(state == candidate && current_term == remote_term && vote_granted){
        qDebug() << "Increasing number of votes for me";
        num_votes_for_me++;

        if(num_votes_for_me >= majority){
            become_leader();
        }
    }
}

qint64 Server::send_requestVote_RPC_response(bool success, quint16 port){
    datagram to_send = {
        .type = requestVoteACK,
        .term_ack = current_term,
        .success_ack = success
    };

    return send_datagram(to_send, port);
}

qint64 Server::send_appendEntries_RPC_response(int text_id, QString text_string, bool success, uint16_t port){
    QByteArray byte_array = text_string.toLocal8Bit();
    uint16_t byte_size = byte_array.size();
    char *byte_data = byte_array.data();

    datagram to_send = {
        .type = appendEntriesACK,
        .id = my_udp_port,
        .text_data_id = text_id,
        .text_data_len = byte_size,
        .term_ack = current_term,
        .success_ack = success,
    };
    memcpy(&to_send.text_data, byte_data, byte_size);

    return send_datagram(to_send, port);
}

qint64 Server::send_datagram(datagram data, quint16 port){
    qint64 size_datagram = sizeof(data);
    char *datagram = (char *)calloc(1, size_datagram);
    memcpy(datagram, &data, size_datagram);

    qint64 err = udp_socket->writeDatagram(datagram, size_datagram, QHostAddress("127.0.0.1"), port);
    free(datagram);

    return err;
}

void Server::maybe_apply(){
    if (commit_index > last_applied){
        QString text_string = log.value(last_applied+1).first.msg_string;
        int text_id = log.value(last_applied+1).first.msg_id;
        chat_history.push_back(text_string);
        applied_msg_ids.insert(text_id, text_string);
        ++last_applied;
        qDebug() << my_udp_port <<": applied "<< text_id <<" to chat_history";
    }
}

void Server::maybe_forward_message(){
    if(!forward_buffer.empty()){
        if(cur_leader != -1){
        message to_forward = forward_buffer.front();
        forward_buffer.pop_front();

        QByteArray byte_array;
        uint16_t byte_size;
        char *byte_data;

        byte_array = to_forward.msg_string.toLocal8Bit();
        byte_size = byte_array.size();
        byte_data = byte_array.data();
        
        datagram to_send = {
            .type = forwardedMsg,
            .id = my_udp_port,
            .term = current_term,
            .text_data_id = to_forward.msg_id,
            .text_data_len = byte_size,
        };
        memcpy(&to_send.text_data, byte_data, byte_size);

        send_datagram(to_send, cur_leader);
        }
    }
}

void Server::maybe_ack_message(){
    if(!message_ids_to_ack.empty()){
        int id_to_ack = message_ids_to_ack.front();
        if(applied_msg_ids.contains(id_to_ack)){
            QString api_formatted = "ack " + QString::number(id_to_ack) + " " + applied_msg_ids.value(id_to_ack) +"\n";
            tcp_socket->write(api_formatted.toUtf8());
            message_ids_to_ack.pop_front();
            qDebug() << "Sending ack of" << QString::number(id_to_ack) << "to proxy";
        }
    }
}

void Server::debug_datagram(datagram data){
    QString incoming_string = QString::fromLocal8Bit(data.text_data, data.text_data_len).trimmed();

    qDebug() << "type: " << data.type;
    qDebug() << "id: " << data.id;
    qDebug() << "term: " << data.term;
    qDebug() << "log_index: " << data.log_index;
    qDebug() << "log_term: " << data.log_term;
    qDebug() << "leader_commit: " << data.leader_commit;
    qDebug() << "text_data_id: " << data.text_data_id;
    qDebug() << "text_data_len: " << data.text_data_len;
    qDebug() << "text_data: " << incoming_string;
    qDebug() << "term_ack: " << data.term_ack;
    qDebug() << "success_ack: " << data.success_ack;
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
    qDebug() << my_udp_port <<" becomes follower!";
    state = follower;
    next_index.clear();
    match_index.clear();
    cur_leader = -1;
    num_votes_for_me = 0;
    reset_election_timer->start(get_bounded_random_number(150,300));
}

void Server::become_candidate(){
    qDebug() << my_udp_port <<" becomes candidate!";
    state = candidate;
    advance_term(++current_term);
    voted_for = my_udp_port;
    num_votes_for_me = 1;
    cur_leader = -1;
    reset_election_timer->start(get_bounded_random_number(150,300));
    broadcast_requestVote();
}

void Server::become_leader(){
    qDebug() << my_udp_port <<" becomes leader!";
    state = leader;
    cur_leader = my_udp_port;
    num_votes_for_me = 0;

    // Add everything from buffer into log
    while(!forward_buffer.empty()){
        message to_add = forward_buffer.front();
        log.push_back(QPair<message, uint16_t>(to_add, current_term));
        replication_count[to_add.msg_id]++;
        forward_buffer.pop_front();
    }

    for(int i = UDP_ROOT; i <= max_udp_port; ++i){
        if (i != my_udp_port){
            next_index.insert(i, log.length()); //WARNING: check indexing
            match_index.insert(i, 0);
        }
    }
    heartbeat_timer->start(0);
}
