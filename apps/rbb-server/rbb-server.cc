// See LICENSE.Berkeley for license details.

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "remote_bitbang.h"

extern "C" {
  void init_platform(const char *ipaddr, int port);
  void finish_platform();
}

/////////// remote_bitbang_t

remote_bitbang_t::remote_bitbang_t(uint16_t port) :
  socket_fd(0),
  client_fd(0),
  recv_start(0),
  recv_end(0),
  err(0)
{
  socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd == -1) {
    fprintf(stderr, "remote_bitbang failed to make socket: %s (%d)\n",
            strerror(errno), errno);
    abort();
  }

  fcntl(socket_fd, F_SETFL, O_NONBLOCK);
  int reuseaddr = 1;
  if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr,
                 sizeof(int)) == -1) {
    fprintf(stderr, "remote_bitbang failed setsockopt: %s (%d)\n",
            strerror(errno), errno);
    abort();
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (::bind(socket_fd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
    fprintf(stderr, "remote_bitbang failed to bind socket: %s (%d)\n",
            strerror(errno), errno);
    abort();
  }

  if (listen(socket_fd, 1) == -1) {
    fprintf(stderr, "remote_bitbang failed to listen on socket: %s (%d)\n",
            strerror(errno), errno);
    abort();
  }

  socklen_t addrlen = sizeof(addr);
  if (getsockname(socket_fd, (struct sockaddr *) &addr, &addrlen) == -1) {
    fprintf(stderr, "remote_bitbang getsockname failed: %s (%d)\n",
            strerror(errno), errno);
    abort();
  }

  tck = 1;
  tms = 1;
  tdi = 1;
  trstn = 1;
  quit = 0;

  fprintf(stderr, "This emulator compiled with JTAG Remote Bitbang client. To enable, use +jtag_rbb_enable=1.\n");
  fprintf(stderr, "Listening on port %d\n",
         ntohs(addr.sin_port));
}

void remote_bitbang_t::accept()
{

  fprintf(stderr,"Attempting to accept client socket\n");
  int again = 1;
  while (again != 0) {
    client_fd = ::accept(socket_fd, NULL, NULL);
    if (client_fd == -1) {
      if (errno == EAGAIN) {
        // No client waiting to connect right now.
      } else {
        fprintf(stderr, "failed to accept on socket: %s (%d)\n", strerror(errno),
                errno);
        again = 0;
        abort();
      }
    } else {
      fcntl(client_fd, F_SETFL, O_NONBLOCK);
      fprintf(stderr, "Accepted successfully.\n");
      again = 0;
    }
  }
}

#define LEN_IDX 0
#define TMS_IDX 1
#define TDI_IDX 2
#define TDO_IDX 3
#define CTRL_IDX 4
extern volatile uint32_t *jtag_base;

void remote_bitbang_t::tick()
{
  if (client_fd > 0) {
    //tdo = jtag_base[TDO_IDX] & 0x1;
    execute_command();
  }
  else {
    this->accept();
  }

  static unsigned char last_tck = 1;
  if (!last_tck && tck) {
    jtag_base[TMS_IDX] = tms;
    jtag_base[TDI_IDX] = tdi;
    jtag_base[LEN_IDX] = 1;
    // send cmd
    jtag_base[CTRL_IDX] = 1;
    while (jtag_base[CTRL_IDX] & 0x1) {; }

	fprintf(stderr, "shift tms = %d, tdi = %d\n", tms, tdi);

	tdo = jtag_base[TDO_IDX] & 0x1;
  }

  last_tck = tck;

  //* jtag_trstn = trstn;
}

void remote_bitbang_t::reset(){
  //trstn = 0;
}

void remote_bitbang_t::set_pins(char _tck, char _tms, char _tdi){
  tck = _tck;
  tms = _tms;
  tdi = _tdi;
}

void remote_bitbang_t::execute_command()
{
  char command;
  int again = 1;
  while (again) {
    ssize_t num_read = read(client_fd, &command, sizeof(command));
    if (num_read == -1) {
      if (errno == EAGAIN) {
        // We'll try again the next call.
        //fprintf(stderr, "Received no command. Will try again on the next call\n");
      } else {
        fprintf(stderr, "remote_bitbang failed to read on socket: %s (%d)\n",
                strerror(errno), errno);
        again = 0;
        abort();
      }
    } else if (num_read == 0) {
      fprintf(stderr, "No Command Received.\n");
      again = 1;
    } else {
      again = 0;
    }
  }

  fprintf(stderr, "Received a command %c\n", command);

  int dosend = 0;

  char tosend = '?';

  switch (command) {
  case 'B': /* fprintf(stderr, "*BLINK*\n"); */ break;
  case 'b': /* fprintf(stderr, "_______\n"); */ break;
  case 'r': reset(); break; // This is wrong. 'r' has other bits that indicated TRST and SRST.
  case '0': set_pins(0, 0, 0); break;
  case '1': set_pins(0, 0, 1); break;
  case '2': set_pins(0, 1, 0); break;
  case '3': set_pins(0, 1, 1); break;
  case '4': set_pins(1, 0, 0); break;
  case '5': set_pins(1, 0, 1); break;
  case '6': set_pins(1, 1, 0); break;
  case '7': set_pins(1, 1, 1); break;
  case 'R': dosend = 1; tosend = tdo ? '1' : '0'; break;
  case 'Q': quit = 1; break;
  default:
    fprintf(stderr, "remote_bitbang got unsupported command '%c'\n",
            command);
  }

  if (dosend){
    fprintf(stderr, "sending tdo = '%c'\n", tosend);
    while (1) {
      ssize_t bytes = write(client_fd, &tosend, sizeof(tosend));
      if (bytes == -1) {
        fprintf(stderr, "failed to write to socket: %s (%d)\n", strerror(errno), errno);
        abort();
      }
      if (bytes > 0) {
        break;
      }
    }
  }

  if (quit) {
    // The remote disconnected.
    fprintf(stderr, "Remote end disconnected\n");
    close(client_fd);
    client_fd = 0;
  }
}

int main(void) {
  init_platform(NULL, 0);

  remote_bitbang_t *jtag = new remote_bitbang_t(8080);
  while (1) jtag->tick();

  finish_platform();
  return 0;
}
