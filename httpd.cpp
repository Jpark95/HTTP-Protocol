#include <iostream>
#include "httpd.h"
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <time.h>

const int BUFSIZE = 8192;
const int MAXPENDING = 5;

bool err;
bool host;
bool con;

struct HTTPMessage {			// Holds raw client message
	char buffer[BUFSIZE];
} message;

struct HTTPRequest {			// Parsed client message
	char message[BUFSIZE];
	char request[BUFSIZE];
	char uri[BUFSIZE];
	char vers[BUFSIZE];
	const char *keys[BUFSIZE];
	char file[BUFSIZE];
} clntReq;

struct HTTPResponse {			// Response components
	char HTTPversion[BUFSIZE];
	char resCode[BUFSIZE];
	char Server[BUFSIZE];
	char lastMod[BUFSIZE];
	char conType[BUFSIZE];
	char contLen[BUFSIZE];
	char file[BUFSIZE];
	char header[BUFSIZE];
	char connect[BUFSIZE];
} response;

using namespace std;

void DieWithSystemMessage(const char *msg) {
  	perror(msg);
  	exit(1);
}

/* Parses Client's message */
void parseMessage() {
	char * token;
	token = strtok(message.buffer, "\r\n");			// PARSE MESSAGE by [CRLF]
	
	int i;
	for (i = 0; token != NULL; i++){		
		if(i == 0)	
			strcpy(clntReq.message, token);		// REQUEST INITIAL LINE
		else
			clntReq.keys[i-1] = token;
		token = strtok(NULL, "\r\n");
	}

	clntReq.keys[i-1] = '\0';				// END OF KEYS

	// wrong REQUEST syntax
	if ((sscanf( clntReq.message, "%s %s %s", clntReq.request, clntReq.uri, clntReq.vers)) != 3) {
		err = true;
		strcpy(response.resCode, "400 Client Error\r\n");
	}

	if (strcmp(clntReq.uri, "/")==0)			// Default to index.html
		strcat(clntReq.uri, "index.html");

	strcat(clntReq.file, clntReq.uri); 		// create complete file path
	
	char key[BUFSIZE];				// Check key/value pairs
	memset(response.connect, '\0', 1);

	for (int j = 0; j < i-1; j++) {
		if ( strcmp(clntReq.keys[j], "Connection: close") == 0) {	// CONNECTION key found
			strcpy(response.connect, "Connection: close\r\n");
			con = true;
		}
		if ((sscanf(clntReq.keys[j], "%s: ", key) != 1) && (err == false)) { // wrong KEY syntax
			cerr << "FAIL" << clntReq.keys[j] << endl;
			err = true;
			strcpy(response.resCode, "400 Client Error\r\n");
		}
		if (strcmp(key,"Host:") == 0) 			// HOST key found
			host = true;
	}
	return;
}

/* Builds Response based off Parsed Message */
off_t frameResponse() {
	struct stat filestat;
	if (host == false) {					// NO "HOST:" ERROR
		err = true;
		strcpy(response.resCode, "400 Client Error\r\n");
	}

	if ( stat(clntReq.file, &filestat) < 0  && (err == false)) {  // Check if file exists
		err = true;
		strcpy(response.resCode, "404 Not Found\r\n");
	}

	if ((filestat.st_mode & S_IROTH) == 0 && (err == false)){   // Check file permission
		err = true;
		strcpy(response.resCode, "403 Forbidden\r\n");
	}

	if(err == false) {				// CREATE HEADER FOR [ 200 OK ]
		struct tm *gmt;
		gmt = gmtime(&filestat.st_mtime);
		strftime(response.lastMod, 1024, "Last-Modified: %a, %d %b %y %H:%M:%S GMT\r\n", gmt);
		strcpy(response.resCode, "200 OK\r\n");
		const char* html = ".html";
		const char* png = ".png";
		const char* jpg = ".jpg";
		char *output1 = NULL;
		char *output2 = NULL;
		char *output3 = NULL;
		output1 = strstr(clntReq.uri,html);
		output2 = strstr(clntReq.uri,jpg);
		output3 = strstr(clntReq.uri,png);
		if(output1)							// HTML file
			strcpy(response.conType, "Content-Type: text/html\r\n");
		else if(output2)						// JPEG file
			strcpy(response.conType, "Content-Type: image/jpeg\r\n");
		else if(output3)						// PNG file
			strcpy(response.conType, "Content-Type: png\r\n");
	}

	strcat(response.header, response.resCode);		// ADD code TO HEADER

	strcpy(response.Server, "Server: Server(Jaehee)\r\n");	// ADD SERVER TO HEADER
	strcat(response.header, response.Server);

	if (err == true) 					// ADD CONTENT LENGTH
		sprintf(response.contLen, "Content-Length: %d\r\n", 0);
	else {
		sprintf(response.contLen, "Content-Length: %ld\r\n", (long) filestat.st_size);
		strcat(response.header, response.lastMod);
	}

	strcat(response.header, response.contLen);		// ADD FILE LENGTH

	if (!err)						// ADD FILE TYPE
		strcat(response.header, response.conType);	

	if (con == true)					// ADD CONNECTION KEY
		strcat(response.header, response.connect);

	strcat(response.header, "\r\n");			// CONCLUDE HEADER & SEND
	return filestat.st_size;
}

/* Receives message, Parses, Frames response, Sends response */
void HandleTCPClient(int clntSocket, string doc_root) {
	strcpy(clntReq.file, doc_root.c_str()); // Copy root directory
	err = false;
	host = false;
	con = false;

	// Begin receive
	ssize_t numBytesRcvd = recv(clntSocket, message.buffer, BUFSIZE, 0);
	
	// Return if no bytes received
	if (numBytesRcvd == 0)
		return;
	memset(response.header, '\0', BUFSIZE);

	// Receive header message from client
	while( (message.buffer[numBytesRcvd-4] != '\r') && (message.buffer[numBytesRcvd-3] != '\n') &&
		(message.buffer[numBytesRcvd-2] != '\r') && (message.buffer[numBytesRcvd-1] != '\n') ){
		numBytesRcvd += recv(clntSocket, message.buffer+numBytesRcvd, BUFSIZE-numBytesRcvd, 0);
	}
	
	parseMessage();					// PARSE MESSAGE //
	
	strcpy(response.HTTPversion, "HTTP/1.1 ");	// initialize HTTPRequest
	strcpy(response.header, response.HTTPversion);

	off_t siz = frameResponse();				// FRAME RESPONSE //

	int h = 0;						// Get size of header
	while(response.header[h] != '\0')
		h++;
	
	send(clntSocket, response.header, h, 0);		// send header
	
	if(!err) {						// send file if [200 OK]
		int fd = open(clntReq.file, O_RDONLY);
		sendfile(clntSocket, fd, NULL, siz);
	}
	
	close(clntSocket);		// CLOSE SOCKET
}

void start_httpd(unsigned short port, string doc_root)
{
	cerr << "Starting server (port: " << port <<
		", doc_root: " << doc_root << ")" << endl;
	
	// Create socket for incoming connections
	int servSock;	// Socket descriptor for server
	if ((servSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
		DieWithSystemMessage("socket() failed");
	

	// Construct local address structure
	struct sockaddr_in servAddr;			// Local address
	memset(&servAddr, 0, sizeof(servAddr));		// Zero out structure
	servAddr.sin_family = AF_INET;			// IPv4 address family
	servAddr.sin_addr.s_addr = htonl(INADDR_ANY);	// Any incoming interface
	servAddr.sin_port = htons(port);		// Local port
	
	// Bind to the local address
	if (bind(servSock, (struct sockaddr*) &servAddr, sizeof(servAddr)) < 0)
		DieWithSystemMessage("bind() failed");

	// Mark the socket so it will listen for incoming connections
	if (listen(servSock, MAXPENDING) < 0)
		DieWithSystemMessage("listen() failed");

	for (;;) { // Run forever
    		struct sockaddr_in clntAddr; // Client address
    		// Set length of client address structure (in-out parameter)
    		socklen_t clntAddrLen = sizeof(clntAddr);

    		// Wait for a client to connect
    		int clntSock = accept(servSock, (struct sockaddr *) &clntAddr, &clntAddrLen);
    		if (clntSock < 0)
      			DieWithSystemMessage("accept() failed");

    		// clntSock is connected to a client!

		char clntName[INET_ADDRSTRLEN]; // String to contain client address
    		if (inet_ntop(AF_INET, &clntAddr.sin_addr.s_addr, clntName, sizeof(clntName)) != NULL)
     			printf("Handling client %s/%d\n", clntName, ntohs(clntAddr.sin_port));
    		else
      			puts("Unable to get client address");

    		HandleTCPClient(clntSock, doc_root);
  	}
	// NOT REACHED	
}
