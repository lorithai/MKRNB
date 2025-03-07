/*
  This file is part of the MKR NB library.
  Copyright (c) 2018 Arduino SA. All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "Modem.h"

#include "utility/NBSocketBuffer.h"

#include "NBClient.h"

enum {
  CLIENT_STATE_IDLE,
  CLIENT_STATE_CREATE_SOCKET,
  CLIENT_STATE_WAIT_CREATE_SOCKET_RESPONSE,
  CLIENT_STATE_ENABLE_SSL,
  CLIENT_STATE_WAIT_ENABLE_SSL_RESPONSE,
  CLIENT_STATE_MANAGE_SSL_PROFILE,
  CLIENT_STATE_WAIT_MANAGE_SSL_PROFILE_RESPONSE,
  CLIENT_STATE_CONNECT,
  CLIENT_STATE_WAIT_CONNECT_RESPONSE,
  CLIENT_STATE_CLOSE_SOCKET,
  CLIENT_STATE_WAIT_CLOSE_SOCKET,
  CLIENT_STATE_RETRIEVE_ERROR
};

NBClient::NBClient(bool synch) :
  NBClient(-1, synch)
{
}

NBClient::NBClient(int socket, bool synch) :
  _synch(synch),
  _socket(socket),
  _connected(false),
  _state(CLIENT_STATE_IDLE),
  _ip((uint32_t)0),
  _host(NULL),
  _port(0),
  _ssl(false),
  _writeSync(true)
{
  MODEM.addUrcHandler(this);
}

NBClient::~NBClient()
{
  MODEM.removeUrcHandler(this);
}

int NBClient::ready()
{
  int ready = MODEM.ready();

  if (ready == 0) {
    return 0;
  }

  switch (_state) {
    case CLIENT_STATE_IDLE:
    default: {
      break;
    }

    case CLIENT_STATE_CREATE_SOCKET: {
      MODEM.setResponseDataStorage(&_response);
      MODEM.send("AT+USOCR=6");

      _state = CLIENT_STATE_WAIT_CREATE_SOCKET_RESPONSE;
      ready = 0;
      break;
    }

    case CLIENT_STATE_WAIT_CREATE_SOCKET_RESPONSE: {
      if (ready > 1 || !_response.startsWith("+USOCR: ")) {
        _state = CLIENT_STATE_IDLE;
      } else {
        _socket = _response.charAt(_response.length() - 1) - '0';

        if (_ssl) {
          _state = CLIENT_STATE_ENABLE_SSL;
        } else {
          _state = CLIENT_STATE_CONNECT;
        }

        ready = 0;
      }
      break;
    }

    case CLIENT_STATE_ENABLE_SSL: {
      MODEM.sendf("AT+USOSEC=%d,1,0", _socket);

      _state = CLIENT_STATE_WAIT_ENABLE_SSL_RESPONSE;
      ready = 0;
      break;
    }

    case CLIENT_STATE_WAIT_ENABLE_SSL_RESPONSE: {
      if (ready > 1) {
        _state = CLIENT_STATE_CLOSE_SOCKET;
      } else {
        _state = CLIENT_STATE_MANAGE_SSL_PROFILE;
      }

      ready = 0;
      break;
    }

    case CLIENT_STATE_MANAGE_SSL_PROFILE: {
      MODEM.send("AT+USECPRF=0,0,1");

      _state = CLIENT_STATE_WAIT_MANAGE_SSL_PROFILE_RESPONSE;
      ready = 0;
      break;
    }

    case CLIENT_STATE_WAIT_MANAGE_SSL_PROFILE_RESPONSE: {
      if (ready > 1) {
        _state = CLIENT_STATE_CLOSE_SOCKET;
      } else {
        _state = CLIENT_STATE_CONNECT;
      }
      ready = 0;
      break;
    }

    case CLIENT_STATE_CONNECT: {
      if (_host != NULL) {
        MODEM.sendf("AT+USOCO=%d,\"%s\",%d", _socket, _host, _port);
      } else {
        MODEM.sendf("AT+USOCO=%d,\"%d.%d.%d.%d\",%d", _socket, _ip[0], _ip[1], _ip[2], _ip[3], _port);
      }

      _state = CLIENT_STATE_WAIT_CONNECT_RESPONSE;
      ready = 0;
      break;
    }

    case CLIENT_STATE_WAIT_CONNECT_RESPONSE: {
      if (ready > 1) {
        _state = CLIENT_STATE_CLOSE_SOCKET;

        ready = 0;
      } else {
        _connected = true;
        _state = CLIENT_STATE_IDLE;
      }
      break;
    }

    case CLIENT_STATE_CLOSE_SOCKET: {

      MODEM.sendf("AT+USOCL=%d", _socket);

      _state = CLIENT_STATE_WAIT_CLOSE_SOCKET;
      ready = 0;
      break;
    }

    case CLIENT_STATE_WAIT_CLOSE_SOCKET: {
      _state = CLIENT_STATE_RETRIEVE_ERROR;
      _socket = -1;
      break;
    }

    case CLIENT_STATE_RETRIEVE_ERROR: {
      MODEM.send("AT+USOER");
      _state = CLIENT_STATE_IDLE;
      break;
    }
  }

  return ready;
}

int NBClient::connect(IPAddress ip, uint16_t port)
{
  _ip = ip;
  _host = NULL;
  _port = port;
  _ssl = false;

  return connect();
}

int NBClient::connectSSL(IPAddress ip, uint16_t port)
{
  _ip = ip;
  _host = NULL;
  _port = port;
  _ssl = true;

  return connect();
}

int NBClient::connect(const char *host, uint16_t port)
{
  _ip = (uint32_t)0;
  _host = host;
  _port = port;
  _ssl = false;

  return connect();
}

int NBClient::connectSSL(const char *host, uint16_t port)
{
  _ip = (uint32_t)0;
  _host = host;
  _port = port;
  _ssl = true;

  return connect();
}

int NBClient::connect()
{
  if (_socket != -1) {
    stop();
  }

  if (_synch) {
    while (ready() == 0);
  } else if (ready() == 0) {
    return 0;
  }

  _state = CLIENT_STATE_CREATE_SOCKET;

  if (_synch) {
    while (ready() == 0) {
      delay(100);
    }

    if (_socket == -1) {
      return 0;
    }
  }

  return 1;
}

void NBClient::beginWrite(bool sync)
{
  _writeSync = sync;
}

size_t NBClient::write(uint8_t c)
{
  return write(&c, 1);
}

size_t NBClient::write(const uint8_t *buf)
{
  return write(buf, strlen((const char*)buf));
}

size_t NBClient::write(const uint8_t* buf, size_t size)
{
  if (_writeSync) {
    while (ready() == 0);
  } else if (ready() == 0) {
    return 0;
  }

  if (_socket == -1) {
    return 0;
  }

  size_t written = 0;
  String command;

  command.reserve(19 + (size > 256 ? 256 : size) * 2);

  while (size) {
    size_t chunkSize = size;

    if (chunkSize > 256) {
      chunkSize = 256;
    }

    command.reserve(19 + chunkSize * 2);

    command = "AT+USOWR=";
    command += _socket;
    command += ",";
    command += chunkSize;
    command += ",\"";

    for (size_t i = 0; i < chunkSize; i++) {
      byte b = buf[i + written];

      byte n1 = (b >> 4) & 0x0f;
      byte n2 = (b & 0x0f);

      command += (char)(n1 > 9 ? 'A' + n1 - 10 : '0' + n1);
      command += (char)(n2 > 9 ? 'A' + n2 - 10 : '0' + n2);
    }

    command += "\"";

    MODEM.send(command);
    if (_writeSync) {
      String response;
      int status = 
        ForResponse(120000, &response);
      if ( status != 1) {
        if (status == 4 && response.indexOf("Operation not allowed") != -1 ) {
          stop();
          break;
        } else {
          break;
        }
      }
    }

    written += chunkSize;
    size -= chunkSize;
  }

  return written;
}

void NBClient::endWrite(bool /*sync*/)
{
  _writeSync = true;
}

uint8_t NBClient::connected()
{
  MODEM.poll();

  if (_socket == -1) {
    return 0;
  }

  // call available to update socket state
  if (NBSocketBuffer.available(_socket) < 0 || (_ssl && !_connected)) {
    stop();

    return 0;
  }

  return 1;
}

NBClient::operator bool()
{
  return (_socket != -1);
}

int NBClient::read(uint8_t *buf, size_t size)
{
  if (_socket == -1) {
    return 0;
  }

  if (size == 0) {
    return 0;
  }

  int avail = available();

  if (avail == 0) {
    return 0;
  }

  return NBSocketBuffer.read(_socket, buf, size);
}

int NBClient::read()
{
  byte b;

  if (read(&b, 1) == 1) {
    return b;
  }

  return -1;
}

int NBClient::available()
{
  if (_synch) {
    while (ready() == 0);
  } else if (ready() == 0) {
    return 0;
  }

  if (_socket == -1) {
    return 0;
  }

  int avail = NBSocketBuffer.available(_socket);

  if (avail < 0) {
    stop();

    return 0;
  }

  return avail;
}

int NBClient::peek()
{
  if (available() > 0) {
    return NBSocketBuffer.peek(_socket);
  }

  return -1;
}

void NBClient::flush()
{
}

void NBClient::stop()
{
  _state = CLIENT_STATE_IDLE;
  if (_socket < 0) {
    return;
  }

  MODEM.sendf("AT+USOCL=%d", _socket);
  MODEM.waitForResponse(120000);

  NBSocketBuffer.close(_socket);

  _socket = -1;
  _connected = false;
}

void NBClient::handleUrc(const String& urc)
{
  if (urc.startsWith("+UUSORD: ")) {
    int socket = urc.charAt(9) - '0';
    if (socket == _socket) {
      if (urc.endsWith(",4294967295")) {
        _connected = false;
      }
    }
  }
}
