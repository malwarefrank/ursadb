#pragma once

#include <zmq.hpp>

#include <string>

//  Receive 0MQ string from socket and convert into string
inline std::string s_recv(zmq::socket_t &socket) {
    zmq::message_t message;
    socket.recv(&message);
    return std::string(static_cast<char *>(message.data()), message.size());
}

//  Convert string to 0MQ string and send to socket
inline bool s_send(zmq::socket_t &socket, const std::string &string, int flags = 0) {
    zmq::message_t message(string.size());
    memcpy(message.data(), string.data(), string.size());
    return socket.send(message, flags);
}

template<typename T>
inline T s_recv_val(zmq::socket_t &socket) {
    zmq::message_t message;

    if (!socket.recv(&message)) {
        throw std::runtime_error("socket recv failed");
    }

    return *(static_cast<T*>(message.data()));
}

template<typename T>
inline bool s_send_val(zmq::socket_t &socket, const T &value, int flags = 0) {
    zmq::message_t msg(sizeof(T));
    memcpy(msg.data(), &value, sizeof(T));
    return socket.send(msg, flags);
}
