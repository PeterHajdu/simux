/*
 * Copyright (c) 2024, Peter Ferenc Hajdu
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <fcntl.h>
#include <err.h>
#include <unistd.h>
#include <stdlib.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>

typedef struct _fds_t {
  int server_socket;
  int command_line_fd;
} fds_t;

void usage(void);
int connect_to_server(int, char*[]);
int start_communication_thread(int);
void *communication_thread_function(void *);

int main(int argc, char **argv) {
  if (pledge("inet stdio cpath wpath rpath tty", NULL) == -1)
    err(6, "pledge");

  const int server_socket = connect_to_server(argc, argv);
  const int command_socket = start_communication_thread(server_socket);
  const char* history_file = "simux.history";

  using_history();
  read_history(history_file);

  for (;;) {
    char* command = readline("simux> ");
    if (NULL == command) {
      return 0;
    }
    add_history(command);
    write_history(history_file);
    if (-1 == write(command_socket, &command, sizeof(char*))) {
      err(1, "Unable to send command to comm thread.");
    }
  }

  close(server_socket);
  return 0;
}

void usage(void) {
  printf(
      "usage: simux [options] <host> <port>\n"
      "\noptions:\n"
      "\t-h --help\tPrint out this message.\n");
  exit(1);
}

int connect_to_server(int argc, char** argv) {
  if (argc < 3)
    usage();
  if (0 == strncmp(argv[1], "-h", 2) || 0 == strncmp(argv[1], "--help", 6))
    usage();

  const char *server_hostname = argv[1];
  unsigned short server_port = strtol(argv[2], NULL, 10);

  const struct hostent *hostent = gethostbyname(server_hostname);
  if (NULL == hostent) {
    err(1, "gethostbyname(\"%s\")\n", server_hostname);
  }

  const in_addr_t in_addr = inet_addr(inet_ntoa(*(struct in_addr*)*(hostent->h_addr_list)));
  if (INADDR_NONE == in_addr) {
    err(1, "inet_addr(\"%s\")\n", *(hostent->h_addr_list));
  }

  const int server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (-1 == server_socket) {
    err(1, "Unable to open socket.");
  }
  struct sockaddr_in server;
  server.sin_family = AF_INET;
  server.sin_port = htons(server_port);
  server.sin_addr.s_addr = in_addr;
  if (0 != connect(server_socket, (struct sockaddr *)&server, sizeof server)) {
    err(1, "Unable to connect to server.");
  }
  printf("Connected to: %s:%d\n", server_hostname, server_port);
  return server_socket;
}

int start_communication_thread(int server_socket) {
  int command_stream[2];
  if (-1 == pipe(command_stream)) {
    err(1, "Unable to open pipe");
  }

  pthread_t thr;
  fds_t* fds = malloc(sizeof(fds_t));
  fds->server_socket = server_socket;
  fds->command_line_fd = command_stream[0];

  if (0!=pthread_create(&thr, NULL, communication_thread_function, fds)) {
    err(1, "Error creating a thread.");
  }

  return command_stream[1];
}

void *communication_thread_function(void *arg) {
  char buffer[2048];
  fds_t *data = (fds_t *)arg;
  const int command_fd = data->command_line_fd;
  const int server_fd = data->server_socket;
  fd_set read_fds;

  const int max_fd = (command_fd > server_fd) ? command_fd : server_fd;
  int output_log = open("output.log",O_CREAT|O_WRONLY|O_APPEND);
  if (-1 == output_log) {
    err(1, "Unable to open output log.");
  }

  for (;;) {
    FD_ZERO(&read_fds);
    FD_SET(command_fd, &read_fds);
    FD_SET(server_fd, &read_fds);
    int select_return = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
    if (-1 == select_return) {
      if (EINTR == errno) {
        continue;
      }
      err(1,"select failed");
    }

    if (FD_ISSET(command_fd, &read_fds)) {
      char* command;
      if (-1 == read(command_fd, &command, sizeof(char*))) {
        err(1, "Unable to read from command fd. %d", command_fd);
      }
      const int full_length_with_line_termination = strlen(command)+2;
      char* command_with_lineending = malloc(full_length_with_line_termination);
      strlcpy(command_with_lineending, command, full_length_with_line_termination);
      strlcat(command_with_lineending, "\n", full_length_with_line_termination);
      send(data->server_socket, command_with_lineending, strlen(command_with_lineending), 0);
      free(command);
      free(command_with_lineending);
    }

    if (FD_ISSET(server_fd, &read_fds)) {
      int recv_result = recv(server_fd, buffer, sizeof(buffer), 0);
      if (recv_result < 0) {
        err(1, "Error receiving from server.");
      }
      if (-1 == write(output_log, buffer, recv_result)) {
        err(1, "Unable to write output log. %d", output_log);
      }
    }
  }
}

