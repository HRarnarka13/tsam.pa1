/* A UDP echo server with timeouts.
 *  *
 *   * Note that you will not need to use select and the timeout for a
 *    * tftp server. However, select is also useful if you want to receive
 *     * from multiple sockets at the same time. Read the documentation for
 *      * select on how to do this (Hint: Iterate with FD_ISSET()).
 *       */

#include <assert.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <arpa/inet.h>
// Define error messages.
#define ERR_MSG_PORT "Transfer already in progress with another client, try again later."
#define ERR_MSG_OPCODE "Invalid request, only download allowed."
#define ERR_MSG_FILE_NOT_FOUND "File not found." 
#define ERR_MSG_ARGUMENTS "Incorrect number of arguments."
#define ERR_MSG_ILLEGAL_TFTP_OP "Illegal TFTP operation. Read request (RRQ) only allowed" 

// Define OP Codes.
#define RRQ 1
#define WRQ 2
#define DATA 3
#define ACK 4
#define ERROR 5

// Define Error codes
#define ERR_CODE_NOT_DEFINED 0
#define ERR_CODE_FILE_NOT_FOUND 1
#define ERR_CODE_ACCESS_VIOLATION 2
#define ERR_CODE_DISK_FULL 3
#define ERR_CODE_ILLEGAL_TFTP_OP 4
#define ERR_CODE_UNKNOWN_TRANS_ID 5
#define ERR_CODE_FILE_EXISTS 6
#define ERR_CODE_NO_SUCH_USER 7

// Define constants
#define PACKET_SIZE 516
#define PACKET_DATA_SIZE 512
#define ARRAY_SMALL 100
#define ACK_SIZE 4


int getOpcode(char* packet){
	return packet[1];
}
// Helper for constructing the path of the file requested
// the parameter directory is argv[2] which is sent as argument to the program
// basename(packet + 2) removes everything but what is behind the last "/" of a path
// securing that going back with ../ is not possible.
void getFilePath(char* packet, char* fileName, char* directory) {
	strcpy(fileName, directory);
	strcat(fileName, "/");
	strcat(fileName, basename(packet + 2));
}

void getMode(char* packet, char*fileName, char* mode) {
	int fileNameLength = 0;
	fileNameLength = strlen(fileName); // get the length of the file name
	strcpy(mode, packet + 2 + fileNameLength + 1); // get the mode from the packet
}

unsigned short getNumber(unsigned char one, unsigned char two) {
	return (one << 8) | two;
}
void sendError(int opcode, int errorCode, char* errorMessage, int sockfd, struct sockaddr_in client ){
	char errorPacket[ARRAY_SMALL];
	memset(&errorPacket, 0, sizeof(errorPacket));
	errorPacket[1] = opcode; // set the error opcode
	errorPacket[3] = errorCode; // set the error code
	strcpy(&errorPacket[4], errorMessage);
	sendto(sockfd, errorPacket, sizeof(errorPacket), 0, (struct sockaddr *) &client, (socklen_t) sizeof(client));
}

void readChunk(char* fileName, int sockfd, struct sockaddr_in client, socklen_t len){

	FILE* file;
	file = fopen(fileName, "r");
	char chunk[PACKET_SIZE];
	char response[ACK_SIZE];

	u_short originalPort = client.sin_port; 

	if (file != NULL) {
		unsigned short packetNumber = 1;    
		size_t numberOfBytes;
		while (!feof(file)) { 
			memset(&chunk,0, sizeof(chunk));
			memset(&response, 0, sizeof(response));
			chunk[1] = DATA;  // set the opcode 
			// set the packet number 
			chunk[2] = (packetNumber >> 8) & 0xff;
			chunk[3] = packetNumber & 0xff; 

			numberOfBytes = fread(&chunk[4], 1, PACKET_DATA_SIZE, file); // fill the chunck with data from file 
			int receivedOpCode = 0;
			unsigned short receivedPacket = '\0';

			// Send the packed and wait for ACK.
			do {	
				sendto(sockfd, chunk, numberOfBytes + 4, 0,
						(struct sockaddr *) &client, (socklen_t) sizeof(client)); 	    
				// get response from client
				ssize_t response_length = recvfrom(sockfd, response, sizeof(response), 0,
						(struct sockaddr *) &client, &len);
				response[response_length] = '\0';
				receivedOpCode = getOpcode(response);			
				receivedPacket = getNumber(response[2], response[3]);
			} while (receivedOpCode == ACK && packetNumber != receivedPacket && client.sin_port == originalPort);	

			if (client.sin_port != originalPort) {
				// Another client interrupting, create and send him an error packet.
				int opCode = ERROR;
				int errorCode = ERR_CODE_UNKNOWN_TRANS_ID;
				char errorMessage[] = ERR_MSG_PORT;
				sendError(opCode, errorCode, errorMessage, sockfd, client); 
			}

			if (receivedOpCode != ACK) {
				// ACK not received, terminate the connection
				// stop sending the file
				int opCode = ERROR;
				int errorCode = ERR_CODE_ILLEGAL_TFTP_OP;
				char errorMessage[] = ERR_MSG_OPCODE;
				sendError(opCode, errorCode, errorMessage, sockfd, client);
				return;
			}
			packetNumber++;
		}
		fclose(file);
	} else {
		int opCode = ERROR;
		int errorCode = ERR_CODE_FILE_NOT_FOUND;
		char errorMessage[] = ERR_MSG_FILE_NOT_FOUND;
		sendError(opCode, errorCode, errorMessage, sockfd, client);
	}
}

int main(int argc, char **argv){
	// If we receive less than 3 arguments, terminate.
	if (argc < 3) {
		perror(ERR_MSG_ARGUMENTS);
		exit(1);
	}

	int sockfd;
	struct sockaddr_in server, client;
	char message[PACKET_DATA_SIZE];

	/* Create and bind a UDP socket */
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	/* Network functions need arguments in network byte order instead of
	 *  host byte order. The macros htonl, htons convert the values, */
	server.sin_addr.s_addr = htonl(INADDR_ANY);
	server.sin_port = htons(atoi(argv[1]));
	bind(sockfd, (struct sockaddr *) &server, (socklen_t) sizeof(server));

	for (;;) {
		fd_set rfds;
		struct timeval tv;
		int retval;

		/* Check whether there is data on the socket fd. */
		FD_ZERO(&rfds);
		FD_SET(sockfd, &rfds);

		tv.tv_sec = 5;
		tv.tv_usec = 0;
		retval = select(sockfd + 1, &rfds, NULL, NULL, &tv);

		if (retval == -1) {
			perror("select()");
		} else if (retval > 0) {
			/* Data is available, receive it. */
			assert(FD_ISSET(sockfd, &rfds));

			/* Copy to len, since recvfrom may change it. */
			socklen_t len = (socklen_t) sizeof(client);
			/* Receive one byte less than declared,
			 * because it will be zero-termianted
			 * below. */
			ssize_t n = recvfrom(sockfd, message, sizeof(message) - 1, 0,
					(struct sockaddr *) &client, &len);			
			/* Send the message back. */
			sendto(sockfd, message, (size_t) n, 0, (struct sockaddr *) &client,
					(socklen_t) sizeof(client));
			/* Zero terminate the message, otherwise
			 * printf may access memory outside of the
			 * string. */
			message[n] = '\0';	
			int opCode = getOpcode(message);
			// If the op code is a read request
			if (opCode == 1) {
				char fileName[ARRAY_SMALL];	
				char mode[ARRAY_SMALL];
				char *ipNumber;
				getFilePath(message, fileName, argv[2]);
				getMode(message, fileName, mode);
				readChunk(fileName, sockfd, client, len);
				ipNumber = inet_ntoa(client.sin_addr); 
				fprintf(stdout, "file \"%s\" requested from ", fileName);
				fprintf(stdout, "%s", ipNumber);
				fprintf(stdout, ":%d\n", client.sin_port);
				fflush(stdout);
			} else {
				int opCode = ERROR;
				int errorCode = ERR_CODE_ILLEGAL_TFTP_OP;
				char errorMessage[] = ERR_MSG_ILLEGAL_TFTP_OP;
				// Create and send an error packet to the client 
				sendError(opCode, errorCode, errorMessage,  sockfd, client );
			}
		} else {
			fprintf(stdout, "No message in five seconds.\n");
			fflush(stdout);
		}
	}
}
