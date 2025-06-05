#pragma once

#ifndef _socket_helper_H_
#define _socket_helper_H_

#include <sys/types.h> 
#include <sys/socket.h> 
#include <arpa/inet.h> 
#include <netinet/in.h> 
#include <stdlib.h>
#include <utility>
#include <string>
#include <unistd.h>

struct socket_config
{
  int domain = AF_INET;   // IPv4
  int type = SOCK_STREAM; // TCP
  int protocol = 0;       // Default protocol
  int port;
};


class socket_helper
{
private:
  int sock_fd;
  int port;
  struct sockaddr_in server_addr, client_addr;
public:
  ~socket_helper();
  socket_helper(/* args */);
  int init();
};

socket_helper::socket_helper(/* args */)
{
}

socket_helper::~socket_helper()
{
  close(sock_fd);
}


#endif // _socket_helper_H_