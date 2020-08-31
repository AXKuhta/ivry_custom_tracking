/*************************************************************************
*
* Copyright (C) 2016-2020 Mediator Software and/or its subsidiary(-ies).
* All rights reserved.
* Contact: Mediator Software (info@mediator-software.com)
*
* NOTICE:  All information contained herein is, and remains the property of
* Mediator Software and its suppliers, if any.
* The intellectual and technical concepts contained herein are proprietary
* to Mediator Software and its suppliers and may be covered by U.S. and
* Foreign Patents, patents in process, and are protected by trade secret or
* copyright law. Dissemination of this information or reproduction of this
* material is strictly forbidden unless prior written permission is obtained
* from Mediator Software.
*
* If you have questions regarding the use of this file, please contact
* Mediator Software (info@mediator-software.com).
*
***************************************************************************/

#include <winsock2.h>
#include <Ws2tcpip.h>
#include "IvryCustomTrackingApp.h"

// Socket prerequisites
// ==========================================
#pragma comment(lib, "Ws2_32.lib")

int WinsockInit(void) {
	WSADATA wsa_data;
	return WSAStartup(MAKEWORD(1, 1), &wsa_data);
}

int WinsockQuit(void) {
	return WSACleanup();
}

SOCKET listening_socket;
SOCKET connection;

// Taken from https://github.com/bmx-ng/pub.mod/blob/master/stdc.mod/stdc.c
int select_(int n_read, int* r_socks, int n_write, int* w_socks, int n_except, int* e_socks, int millis) {

	int i, n, r;
	struct timeval tv, * tvp;
	fd_set r_set, w_set, e_set;

	n = -1;

	FD_ZERO(&r_set);
	for (i = 0; i < n_read; ++i) {
		FD_SET(r_socks[i], &r_set);
		if (r_socks[i] > n) n = r_socks[i];
	}
	FD_ZERO(&w_set);
	for (i = 0; i < n_write; ++i) {
		FD_SET(w_socks[i], &w_set);
		if (w_socks[i] > n) n = w_socks[i];
	}
	FD_ZERO(&e_set);
	for (i = 0; i < n_except; ++i) {
		FD_SET(e_socks[i], &e_set);
		if (e_socks[i] > n) n = e_socks[i];
	}

	if (millis < 0) {
		tvp = 0;
	}
	else {
		tv.tv_sec = millis / 1000;
		tv.tv_usec = (millis % 1000) * 1000;
		tvp = &tv;
	}

	r = select(n + 1, &r_set, &w_set, &e_set, tvp);
	if (r < 0) return r;

	for (i = 0; i < n_read; ++i) {
		if (!FD_ISSET(r_socks[i], &r_set)) r_socks[i] = 0;
	}
	for (i = 0; i < n_write; ++i) {
		if (!FD_ISSET(w_socks[i], &w_set)) w_socks[i] = 0;
	}
	for (i = 0; i < n_except; ++i) {
		if (!FD_ISSET(e_socks[i], &e_set)) e_socks[i] = 0;
	}
	return r;
}


// Checks if the connection is present
int Connected() {
	if (!connection)
		return 0;

	if (connection == SOCKET_ERROR)
		return 0;

	// Storing the handle into a new variable is somehow important?
	// The socket seems to just die otherwise
	int handle = (int)connection;

	if (select_(1, &handle, 0, NULL, 0, NULL, 0) != 1)
		return 1;

	// Connection failure if this code is reached
	// Make sure the socket is closed
	if (connection) {
		closesocket(connection);
		connection = 0;
	}
}

// Unreliable -- do not use
int ReadAvail() {
	int status;
	u_long n = 0;
	
	status = ioctlsocket(connection, FIONREAD, &n);

	// Returns non-zero on error
	if (status != 0)
		return 0;

	return (int)n;
}

// Writes a line to client
int WriteLine(char* line) {
	send(connection, line, strlen(line), 0);

	return 1;
}
// ==========================================

// Global variables that will override the position
double XPosOverride = 0;
double YPosOverride = 1;
double ZPosOverride = 0;

// Command buffer
// ==========================================
#define CMD_BUFFER_LEN 256

char* cmd_buffer;
int cmd_buffer_offset = 0;

void reset_cmd_buffer() {
	memset(cmd_buffer, 0, CMD_BUFFER_LEN);
	cmd_buffer_offset = 0;
}

// Pulls the available amount of bytes into the command buffer and tries to detect a newline
// Returns cmd_buffer if a newline-terminated string was formed
// Returns NULL otherwise
char* getcmd() {
	int bytes_recv = 0;

	bytes_recv = recv(connection, cmd_buffer + cmd_buffer_offset, (CMD_BUFFER_LEN - cmd_buffer_offset), 0);
	cmd_buffer_offset += bytes_recv;
		
	if (bytes_recv == 0) {
		// Zero bytes read from a blocking recv() is a sign of lost connection
		return NULL;
	}

	if (cmd_buffer[cmd_buffer_offset - 1] == '\n') {
		return cmd_buffer;
	}

	return NULL;
}
// ==========================================

IvryCustomTrackingApp::IvryCustomTrackingApp()
	: m_hQuitEvent(INVALID_HANDLE_VALUE)
	, m_bUseDeviceOrientation(true)
{
	// Start position 1m off the ground at origin
	m_afPosition[0] = m_afPosition[2] = 0;
	m_afPosition[1] = 1;
}

IvryCustomTrackingApp::~IvryCustomTrackingApp()
{
	if (m_hQuitEvent != INVALID_HANDLE_VALUE)
	{
		// Make sure event is signalled before deleting
		::SetEvent(m_hQuitEvent);
		::CloseHandle(m_hQuitEvent);
	}
}

/** Run tracker **/
DWORD IvryCustomTrackingApp::Run()
{
	DWORD result = ERROR_SUCCESS;

	// Init our socket subsystem
	WinsockInit();

	// Create a TCP socket
	listening_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	// Bind it (messy)
	struct addrinfo addr;
	struct addrinfo *addr_result;
	
	memset(&addr, 0, sizeof(addr));

	addr.ai_family = AF_INET;
	addr.ai_socktype = SOCK_STREAM;
	addr.ai_protocol = IPPROTO_TCP;
	addr.ai_flags = AI_PASSIVE;

	getaddrinfo(NULL, "8021", &addr, &addr_result);

	int status = bind(listening_socket, addr_result->ai_addr, (int)addr_result->ai_addrlen);

	if (status == SOCKET_ERROR) {
		LogMessage("Failed to bind to port 8021\n");
	}

	// Start listening; max connection backlog of 0
	listen(listening_socket, 0);

	// Initialize the command buffer
	cmd_buffer = (char*)malloc(CMD_BUFFER_LEN);

	float x, y, z;
	char* cmd;

	// Open connection to driver
	if (Open())
	{
		// Create 'exiting' event
		m_hQuitEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
		if (m_hQuitEvent != INVALID_HANDLE_VALUE)
		{
			// NOTE: in an external tracking process disabling device orientation and
			// enabling external tracking would normally be done once the external 
			// tracking had actually begun, to avoid no tracking being active

			// Disable device orientation
			EnableDeviceOrientation(false);

			// Enable external tracking
			TrackingEnabled(true);

			while (1) {
				// Wait 10 ms for quit event on each iteration
				if (::WaitForSingleObject(m_hQuitEvent, 10) != WAIT_TIMEOUT)
					break;

				// If there's no connection, try to establish it
				if (!Connected()) {
					connection = accept(listening_socket, NULL, NULL);

					WriteLine("Hello! Connection was reestablished\r\n");

					reset_cmd_buffer();

				} else {
					cmd = getcmd();

					if (cmd) {
						WriteLine("Got a command!\r\n");

						// Position change handling
						if (memcmp(cmd, "Pos", 3) == 0) {
							WriteLine("-> Positioning command\r\n");

							sscanf(cmd + 4, "%f %f %f", &x, &y, &z);

							char buf2[256];

							sprintf(&buf2[0], "-> Values: %f %f %f\r\n", x, y, z);

							WriteLine(&buf2[0]);

							XPosOverride = x;
							YPosOverride = y;
							ZPosOverride = z;
						}
						// Rotation change handling
						else if (memcmp(cmd, "Rot", 3) == 0)  {
							WriteLine("-> Rotation command\r\n");
							// ...
						}
						else {
							WriteLine("-> Unrecognized command: ");
							WriteLine(cmd);
							WriteLine("\r\n");
						}

						reset_cmd_buffer();
					}
				}
			}
			
			WriteLine("Exiting per driver request\r\n");

			// Disable external tracking
			TrackingEnabled(false);

			// Enable device orientation
			EnableDeviceOrientation(true);
		}
		else
		{
			// Get last error code from Windows
			result = ::GetLastError();
		}

		// Close connection to driver
		Close();
	}
	else
	{
		// Get last error code from library
		result = this->GetLastError();
	}

	// Socket cleanup
	WinsockQuit();

	// Free the command buffer memory
	free(cmd_buffer);

	return result;
}

/** Pose has been recevied from driver **/
void IvryCustomTrackingApp::OnDevicePoseUpdated(const vr::DriverPose_t &pose)
{
	vr::DriverPose_t updatedPose = pose;

	// Not using device orientation?
	if (!m_bUseDeviceOrientation)
	{
		// Use tracker orientation
		updatedPose.qRotation = { 1, 0, 0, 0 };
	}

	// Use the overriden positions
	updatedPose.vecPosition[0] = XPosOverride;
	updatedPose.vecPosition[1] = YPosOverride;
	updatedPose.vecPosition[2] = ZPosOverride;

	// Send tracker pose to driver
	PoseUpdated(updatedPose);
}

/** Device orientation has been enabled/disabled by user **/
void IvryCustomTrackingApp::OnDeviceOrientationEnabled(bool enable)
{
	m_bUseDeviceOrientation = enable;
}

/** Driver is requesting tracking process quit **/
void IvryCustomTrackingApp::OnQuit()
{
	if (m_hQuitEvent != INVALID_HANDLE_VALUE)
	{
		// Signal that Run() can exit
		::SetEvent(m_hQuitEvent);
	}

	LogMessage("Shutting down\n");
}

