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

#ifndef MONIT_EVENT_H
#define MONIT_EVENT_H

#include "monit.h"


//FIXME: could be good to support also event state clasification ... currently we have helper bitmap to differentiate error/change
//FIXME: keep backward compatibility for M/Monit ... new event types must map to old events and old type reported along new one in XML
//FIXME: extend alert filters for new event types
/*


typedef struct Events_T {
        uint8_t action                     : 2;
        uint8_t checksum                   : 2;
        uint8_t children                   : 2;
        uint8_t content                    : 2;
        uint8_t connection                 : 2;
        uint8_t cpu                        : 2;
        uint8_t cpuSystem                  : 2;
        uint8_t cpuTotal                   : 2;
        uint8_t cpuUser                    : 2;
        uint8_t cpuWait                    : 2;
        uint8_t data                       : 2;
        uint8_t exec                       : 2;
        uint8_t exist                      : 2;
        uint8_t gid                        : 2;
        uint8_t heartbeat                  : 2;
        uint8_t instance                   : 2;
        uint8_t invalid                    : 2;
        uint8_t filesystemFlags            : 2;
        uint8_t filesystemInodes           : 2;
        uint8_t filesystemBytesRead        : 2;
        uint8_t filesystemBytesWrite       : 2;
        uint8_t filesystemOperationsRead   : 2;
        uint8_t filesystemOperationsWrite  : 2;
        uint8_t filesystemServiceTime      : 2;
        uint8_t filesystemSpace            : 2;
        uint8_t linkBytesIn                : 2;
        uint8_t linkBytesOut               : 2;
        uint8_t linkError                  : 2;
        uint8_t linkPacketsIn              : 2;
        uint8_t linkPacketsOut             : 2;
        uint8_t linkSaturation             : 2;
        uint8_t linkSpeed                  : 2;
        uint8_t linkStatus                 : 2;
        uint8_t loadAverage1m              : 2;
        uint8_t loadAverage5m              : 2;
        uint8_t loadAverage15m             : 2;
        uint8_t memory                     : 2;
        uint8_t memoryTotal                : 2;
        uint8_t nonExist                   : 2;
        uint8_t permission                 : 2;
        uint8_t ping                       : 2;
//        uint8_t processDiskBytesRead       : 2; // use filesystemBytesRead in process content too ... ditto for bellow
//        uint8_t processDiskBytesWrite      : 2;
//        uint8_t processDiskOperationsRead  : 2;
//        uint8_t processDiskOperationsWrite : 2;
        uint8_t processPid                 : 2;
        uint8_t processPpid                : 2;
        uint8_t size                       : 2;
        uint8_t status                     : 2;
        uint8_t swap                       : 2;
        uint8_t threads                    : 2;
        uint8_t timeout                    : 2;
        uint8_t timestampAccess            : 2;
        uint8_t timestampChange            : 2;
        uint8_t timestampModify            : 2;
        uint8_t uid                        : 2;
        uint8_t uptime                     : 2;
} Events_T;


typedef struct EventData_T {
        boolean_t failed;
        boolean_t changed;
} *EventData_T;
//FIXME: too complicated and fragile?
union {
        struct {
        } directory;
        struct {
        } fifo;
        struct {
        } file;
        struct {
                struct EventData_T space;
                struct EventData_T inode;
                struct EventData_T readBytes;
                struct EventData_T writeBytes;
        } filesystem;
        struct {
        } host;
        struct {
        } net;
        struct {
                struct EventData_T cpu;
                struct EventData_T cpuTotal;
                struct EventData_T memory;
                struct EventData_T memoryTotal;
                struct EventData_T uptime;
        } process;
        struct {
        } program;
        struct {
                struct EventData_T cpuUser;
                struct EventData_T cpuSystem;
                struct EventData_T cpuWait;
                struct EventData_T memory;
                struct EventData_T uptime;
        } system;
} event;
*/
//FIXME: use enum for new Event types, but just normal sequence, not mask ... so we can easily do for example eventCpu??? The Event_post will then internally set the above structure
//FIXME: keep this for backward compatibility and add new types (covered by sequential enum)?
//FIXME: add new function that will map new events to old events? ... the Event_post can post the new event and just the XML code will map the event to the old one
//FIXME: rename?:
typedef enum {
        Event_Null       = 0x0,
        Event_Checksum   = 0x1,
        Event_Resource   = 0x2, //FIXME: split to more specific events (cpu, totalcpu, mem, totalmem, loadaverage, space, inode, ...)
        Event_Timeout    = 0x4,
        Event_Timestamp  = 0x8, //FIXME: split to more specific events (atime, mtime, ctime)
        Event_Size       = 0x10,
        Event_Connection = 0x20,
        Event_Permission = 0x40,
        Event_Uid        = 0x80,
        Event_Gid        = 0x100,
        Event_NonExist   = 0x200,
        Event_Invalid    = 0x400,
        Event_Data       = 0x800,
        Event_Exec       = 0x1000,
        Event_FsFlag     = 0x2000,
        Event_Icmp       = 0x4000,
        Event_Content    = 0x8000,
        Event_Instance   = 0x10000,
        Event_Action     = 0x20000,
        Event_Pid        = 0x40000,
        Event_PPid       = 0x80000,
        Event_Heartbeat  = 0x100000,
        Event_Status     = 0x200000,
        Event_Uptime     = 0x400000,
        Event_Link       = 0x800000, //FIXME: split to more specific events (link status, link errors)
        Event_Speed      = 0x1000000,
        Event_Saturation = 0x2000000,
        Event_ByteIn     = 0x4000000,
        Event_ByteOut    = 0x8000000,
        Event_PacketIn   = 0x10000000,
        Event_PacketOut  = 0x20000000,
        Event_Exist      = 0x40000000,
        Event_All        = 0x7FFFFFFF
} Event_Type;


#define IS_EVENT_SET(value, mask) ((value & mask) != 0)

typedef struct myeventtable {
        int id;
        char *description_failed;
        char *description_succeeded;
        char *description_changed;
        char *description_changednot;
} EventTable_T;

extern EventTable_T Event_Table[];


/**
 * This class implements the <b>event</b> processing machinery used by
 * monit. In monit an event is an object containing a Service_T
 * reference indicating the object where the event orginated, an id
 * specifying the event type, a value representing up or down state
 * and an optional message describing why the event was fired.
 *
 * Clients may use the function Event_post() to post events to the
 * event handler for processing.
 *
 * @file
 */


/**
 * Post a new Event
 * @param service The Service the event belongs to
 * @param id The event identification
 * @param state The event state
 * @param action Description of the event action
 * @param s Optional message describing the event
 */
void Event_post(Service_T service, long id, State_Type state, EventAction_T action, char *s, ...) __attribute__((format (printf, 5, 6)));


/**
 * Get a textual description of actual event type. For instance if the
 * event type is possitive Event_Timestamp, the textual description is
 * "Timestamp error". Likewise if the event type is negative Event_Checksum
 * the textual description is "Checksum recovery" and so on.
 * @param E An event object
 * @return A string describing the event type in clear text. If the
 * event type is not found NULL is returned.
 */
const char *Event_get_description(Event_T E);


/**
 * Get an event action id.
 * @param E An event object
 * @return An action id
 */
Action_Type Event_get_action(Event_T E);


/**
 * Get a textual description of actual event action. For instance if the
 * event type is possitive Event_NonExist, the textual description of
 * failed state related action is "restart". Likewise if the event type is
 * negative Event_Checksum the textual description of recovery related action
 * is "alert" and so on.
 * @param E An event object
 * @return A string describing the event type in clear text. If the
 * event type is not found NULL is returned.
 */
const char *Event_get_action_description(Event_T E);


/**
 * Reprocess the partialy handled event queue
 */
void Event_queue_process();


#endif
