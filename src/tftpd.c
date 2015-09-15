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

int getOpcode(char* packet){
	return packet[1];
}

void getFileName(char* packet, char* fileName) {
	strcpy(fileName, packet + 2);
}

void getMode(char* packet, char*fileName, char* mode) {
	int fileNameLength = 0;
	fileNameLength = strlen(fileName); // get the length of the file name
	strcpy(mode, packet + 2 + fileNameLength + 1); // get the mode from the packet
}

void readChunk(char* fileName, int sockfd, struct sockaddr_in client, socklen_t len){

	FILE* file;
	char path[512];
	strcpy(path, "data/");
	strcat(path, fileName);
	file = fopen(path, "r");
	char chunk[516];
	char response[516];
	if (file != NULL) {
		unsigned short packetNumber = 1;    
		size_t numberOfBytes;
		while (!feof(file)) { 
			memset(&chunk,0, sizeof(chunk));
			memset(&response, 0, sizeof(response));
			chunk[0] = 0x00;
			chunk[1] = 3 & 0xff;  // set the opcode 
			// set the packet number 
			chunk[2] = (packetNumber >> 8) & 0xff;
			chunk[3] = packetNumber & 0xff; 

			numberOfBytes = fread(&chunk[4], 1, 512, file); // fill the chunck with data from file 

			sendto(sockfd, chunk, (size_t) sizeof(chunk), 0,
					(struct sockaddr *) &client, (socklen_t) sizeof(client)); 	    

			ssize_t response_length = recvfrom(sockfd, response, sizeof(response) - 1, 0,
					(struct sockaddr *) &client, &len);

			response[response_length] = '\0';

			fprintf(stdout, "Response: %s \n", response); 

			fprintf(stdout, "packetnumber: %zu\n", packetNumber);
			// fprintf(stdout, "%s", chunk);
			packetNumber++;
		}
		fprintf(stdout, "rass \n");
		fclose(file);
	} else {
		perror("Problem with reading file");
		exit(1);
	}
}

int main(int argc, char **argv){
	int sockfd;
	struct sockaddr_in server, client;
	char message[512];

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
			ssize_t n = recvfrom(sockfd, message,
					sizeof(message) - 1, 0,
					(struct sockaddr *) &client,
					&len);			
			/* Send the message back. */
			sendto(sockfd, message, (size_t) n, 0,
					(struct sockaddr *) &client,
					(socklen_t) sizeof(client));
			/* Zero terminate the message, otherwise
			 * printf may access memory outside of the
			 * string. */
			message[n] = '\0';

			int opCode = getOpcode(message);
			char fileName[512];
			getFileName(message, fileName);
			char mode[100];
			getMode(message, fileName, mode);
			fprintf(stdout, "opcode : %d\n", opCode);
			fprintf(stdout, "fileName: %s\n", fileName);
			fprintf(stdout, "mode : %s\n", mode);
			/* Print the message to stdout and flush. */
			// fprintf(stdout, "Received:\n%s\n", message);
			fflush(stdout);
			readChunk(fileName, sockfd, client, len);
			/*FILE* file = NULL;
			  char chunk[512];
			  memset(chunk,0,sizeof(chunk));

			  file = fopen(fileName, "r");
			  fread(message, 512, byteNumber, file);
			  fclose(file);

			  fprintf(stdout, "Chunk: %s\n", message);
			  fflush(stdout);*/
		} else {
			fprintf(stdout, "No message in five seconds.\n");
			fflush(stdout);
		}
	}
}
