/*
 * Copyright (C) Tildeslash Ltd. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 *
 * You must obey the GNU Affero General Public License in all respects
 * for all of the code used other than OpenSSL.
 */

#include "config.h"

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "protocol.h"

// libmonit
#include "exceptions/IOException.h"
#include "exceptions/ProtocolException.h"

/**
 *  Send PING and check for PONG.
 *
 *  @file
 */
void check_fail2ban(Socket_T socket) {
        ASSERT(socket);

        const unsigned char ping[] = {
                0x80, 0x04, 0x95, 0x0b, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x5d, 0x94, 0x8c, 0x04, 0x70,
                0x69, 0x6e, 0x67, 0x94, 0x61, 0x2e, 0x3c, 0x46,
                0x32, 0x42, 0x5f, 0x45, 0x4e, 0x44, 0x5f, 0x43,
                0x4f, 0x4d, 0x4d, 0x41, 0x4e, 0x44, 0x3e, 0x00
        };
        const unsigned char pong[] = {
                0x80, 0x04, 0x95, 0x0c, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x4b, 0x00, 0x8c, 0x04, 0x70,
                0x6f, 0x6e, 0x67, 0x94, 0x86, 0x94, 0x2e, 0x3c,
                0x46, 0x32, 0x42, 0x5f, 0x45, 0x4e, 0x44, 0x5f,
                0x43, 0x4f, 0x4d, 0x4d, 0x41, 0x4e, 0x44, 0x3e
        };

        // Send PING
        if (Socket_write(socket, (void *)ping, sizeof(ping)) < 0) {
                THROW(IOException, "FAIL2BAN: PING command error -- %s", STRERROR);
        }

        // Read and check PONG
        unsigned char response[40];
        if (Socket_read(socket, response, sizeof(response)) != sizeof(pong)) {
                THROW(IOException, "FAIL2BAN: PONG read error -- %s", STRERROR);
        }
        if (memcmp(response, pong, sizeof(pong))) {
                THROW(ProtocolException, "FAIL2BAN: PONG error");
        }
}

