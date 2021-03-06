#include "headers.h"
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <strings.h>
#include <string.h>
#include <arpa/inet.h> /*used by inet_ntoa*/
#include <unistd.h>
#include <stdlib.h>
#include <netdb.h>
#include <signal.h>
#define BUFSIZE 1000000
#define BUF_SIZE 65536
#define NAME_SIZE 100
#define PATHMAX 1024
#include <sys/wait.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int is_full_request(char * req, int req_len) {
	char *full_headers = strstr(req, "\r\n\r\n");
	int content_len;
	char *ptr = strstr(req, "Content-Length:");
	if (!full_headers)
	   	return 0;
	if (ptr) {
		ptr += strlen("Content-Length:");
		content_len = strtol(ptr, NULL, 10);
		// request length is equal headers length plus content plus \r\n\r\n
		return full_headers + 4 - req + content_len == req_len;
	} else {
		// no content-length and headers were received
		return 1;
	}
}

/*function that handles the http request from the client*/
int handle_http(int clisockfd){

	size_t response_to_client_size;
	int bytes, i;
	int type = 0, total = 0;
	long length;
	char * req = malloc(BUFSIZE);
	char *filename, *filenamed;
	char *file_contents;
	FILE *doc, *doc_upload;
	char *response;
	size_t response_size;
	size_t *response_size_ptr = &response_size;
	char * req_copy = malloc(BUFSIZE);
	int headers_size;
	char *double_newline = 0;
	int content_length;
	char *ptr, *uploadfile_contents;
	char *response_to_client;
	char home[PATHMAX];
	char *ptr1, *ptr2, *domain;
	size_t domain_size;
	int query_type = 1;
	printf("gets here");
	
	bzero(home, PATHMAX);
	/*get currently working directory*/
	if (getcwd(home, PATHMAX) == NULL){
		perror("getcwd error\n");
		free(req);
		free(req_copy);
		return -1;
	} 
	//home[strlen(home)] = '\0';
	
	printf("home: %s\n", home);
	/*	size_t *uploadfile_ptr = &uploadfile;*/
	bzero(req, BUFSIZE);

	total = 0;
	while((bytes = read(clisockfd, req + total, BUFSIZE - total)) > 0) {
		total += bytes;
		int is_full = is_full_request(req, total);
		printf("Received bytes: %d\nIs full: %d\n", bytes, is_full);
		// printf("Whole request:\n%s\n-- End whole request --\n", req);
		if (is_full_request(req, total)) {
			break;
		}
	}
		

	if (!type && (strstr(req, "PUT") || strstr(req, "GET"))){
		/*for (i=0; i<BUFSIZE; i++){
			req_copy[i] = req[i];		
		}*/

		memcpy(req_copy, req, BUFSIZE);

		/*filename will be dns-query in case of DNS request*/
		strtok(req_copy, "/");
		filenamed = strtok(NULL, " ");
		filename = malloc(strlen(filenamed) + 1);
		strcpy(filename, filenamed);
	}


	if ((!type) && ((strstr(req, "GET")) != NULL)){	
		printf("This is a GET request\n");
		printf("the request sent looks like this:\n%s\n", req);
		type = 1;
	
	}

	/*checking if the request has PUT in its headers or is known to be PUT req */
	if ((!type) && ((strstr(req, "PUT")) || (strstr(req, "POST /dns-query")))){
		/*printf("This is a PUT request\n");*/
		if (strstr(req, "PUT")){
			type = 2;
		} else {
			type = 3;
		}
		/*calculating headers' size in the request sent by client*/
		if (!double_newline){
			if ((double_newline = strstr(req, "\r\n\r\n"))){
				headers_size = double_newline - req + 4;
				printf("Headers' size: %d\n", headers_size);
			}								
		}
	
		bzero(req_copy, BUFSIZE);		
		/*identifying content length*/
		if ((ptr=strstr(req, "Content-Length:"))){
			memcpy(req_copy, req, BUFSIZE);
		
			if ((strtok(ptr, " "))){ 
				 content_length = strtol(strtok(NULL, "\r\n\r\n"), NULL, 10);
				 printf("Content-length: %d\n", content_length);
				 uploadfile_contents = calloc(content_length, sizeof(char));
				 memcpy(uploadfile_contents, req_copy + headers_size, total - headers_size);
/*					 printf("uploadfile_contents:\n%s\n", uploadfile_contents);*/
			}
		}
		if ((type==3) && (ptr1=strstr(req_copy, "Name="))){
			/*parse contents of the dns query*/		
			ptr2=strstr(req_copy, "&");
			domain_size = ptr2 - ptr1 - 5;
			printf("domain size: %lu\n", (unsigned long int) domain_size);
			char * type_ptr = strstr(req_copy, "Type=") + 5;
			if (strstr(type_ptr, "AAAA")) {
				query_type = 28;
			} else if (strstr(type_ptr, "A")) {
				query_type = 1;
			} else if (strstr(type_ptr, "MX")) {
				query_type = 15;
			}
			/*allocate enough bytes for domain name, including null byte*/
			domain = calloc(domain_size + 1, sizeof(char));
			for (i=0; i<domain_size; i++){
				domain[i] = ptr1[i+5];
			}
			domain[domain_size] = '\0';
		}

		

	}

	if (type == 0){
		printf("This type of request cannot be handled by this server\n");
		free(req);
		free(req_copy);
		return -1;
	}
	
	/*Open file for reading*/
	if (type != 3){
	//	strcat(final, home);
		strcat(home, "/");
		strcat(home, filename);
		printf("filename: %s\n", home);	
		doc = fopen(home, "r");
	}
	switch(type){
		case 3:
			response_to_client = dns_query(domain, query_type, &response_to_client_size);
			send_response(clisockfd, response_to_client, response_to_client_size);		
			/*printf("domain: %s\n", domain);*/
			free(domain);
			free(uploadfile_contents);
			free(response_to_client);
/*			free(filename);*/
			break;
		case 1:
			if (doc){
				fseek(doc, 0, SEEK_END);
				length = ftell(doc);
				fseek(doc, 0, SEEK_SET);
				file_contents = malloc(length);
				printf("%p\n", file_contents);
				bzero(file_contents, length);	
				if (file_contents){
					/*reading file contents into file_contents char array*/
					int n = fread(file_contents, 1, length, doc);
					printf("n = %d", n);
				}
				fclose(doc);
				
				/*calling http_response to form whole HTTP response*/
				response = http_response(1, length, file_contents, response_size_ptr);
				printf("%p\n", response);
			
				/*calling send_response to send HTTP response to the client*/	
				send_response(clisockfd, response, response_size);
				free(file_contents);		
			}
			else {
				perror("File could not be opened\n");
				response = http_response(0, 0, "", response_size_ptr);
				send_response(clisockfd, response, response_size);			
			}
			free(response);		
			free(filename);
			break;
		case 2:
			if (doc){
				printf("File exists and it will be rewritten\n");
				fclose(doc);	
				doc_upload = fopen(home, "w+");
				fwrite(uploadfile_contents, content_length, 1, doc_upload);
				send_status(clisockfd, "200 OK");
			} else if (!doc) {
				printf("New file will be created\n");
				doc_upload = fopen(home, "w");
			 	fwrite(uploadfile_contents, content_length, 1, doc_upload);	
				send_status(clisockfd, "201 Created");
			}
			
			fclose(doc_upload);				
			free(uploadfile_contents);
			free(filename);
			
			break;
	}
	close(clisockfd);
	free(req);
	free(req_copy);
	return 0;

}

void send_status(int clisockfd, char *status){
	int bytes;
	size_t status_size = strlen(status);
	while((bytes=write(clisockfd, status, status_size)) > 0){
		if (bytes == status_size){
			break;
		}		
	}
	if (bytes < 0){
		perror("Error when sending status response\n");
	}
}



/*after processed request by handle_http, this function sends response to client*/
void send_response(int clisockfd, char *response, size_t response_size) {
	int total=0, nbytes;
	/*we subtract 1 from response_size because the size includes the null byte*/
	while((nbytes=write(clisockfd, response, response_size)) > 0){
		total += nbytes;
/*		printf("Sent %d bytes, total length of response = %u\n", nbytes, response_size);*/
		if (total == response_size){
			
		/*	printf("Sent data looks like this:\n%s\n", response);*/
			break;
		}

	}

/*	printf("nbytes:%d\n", nbytes);*/
	if (nbytes < 0){
		perror("Error sending response\n");
	}

}

/*function that prevents from zombies */
void sig_child(int signo){
	pid_t pid;
	int stat;
	
	pid = wait(&stat);
	printf("A child whose pid is %d has been terminated\n", pid);
	return;	
}

/*function that listens to request from clients*/
int listening(char *hostname, char *service){
	int sockfd;
	int turn_on = 1;
	struct addrinfo hints, *res, *ressave;

	bzero(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_socktype = SOCK_STREAM;
	
	if (getaddrinfo(hostname, service, &hints, &res) != 0){
		perror("Error obtaining IP address for supplied host\n");
		exit(1);
	}
	ressave = res;
	do {
		if ((sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) < 0){
			perror("Error creating socket\n");
			continue;
		}

		setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &turn_on, sizeof(turn_on));
		
		/*bind returns zero on success
	 	socket is bind to local address*/
		if (bind(sockfd, res->ai_addr, res->ai_addrlen) == 0){	
		/*listen returns zero on success*/
			if ((listen(sockfd, 5)) == 0){
				printf("Listening to requests\n");
				break;		
			} else {
				perror("Error listening\n");

			}
			} else {
			perror("Error binding\n");
		}	
		close(sockfd);
	} while ((res=res->ai_next) != NULL);

	/*handling of exceptional situations*/
	if (res == NULL){
		fprintf(stderr, "Failed to create any socket\n");
	}
	freeaddrinfo(ressave);
	/*socket descriptor returned on which server is listening*/
	return(sockfd);

}

int daemon_init(char *program_name, int facility){

	pid_t pid;
	int i;

	if ((pid=fork()) < 0){
		return 1;	
	}
	/*parent terminates in case forking was successful*/
	else if (pid){
		exit(0);
	}
	/*child process becomes session leader*/
	if (setsid() < 0){
		return 1;
	}
	/*changing working directory*/
	//chdir("/");

	/*closing file descriptors*/
	for(i=0; i<64; i++){
		close(i);
	}

	/*redirect stdin, stdout, stderr to /dev/null*/
	open("/dev/null", O_RDONLY);
	open("/dev/nul", O_RDWR);
	open("/dev/null", O_RDWR);

	openlog(program_name, LOG_PID, facility);

	return 0;

}

int main(int argc, char **argv){
	int sockfd, clisockfd;
	/*char buf[BUFSIZE];*/
	pid_t childpid;
	struct sockaddr_storage cliaddr;
	socklen_t len = sizeof(cliaddr);
	/*making sure right amount of arguments are input by user*/
	if (argc != 3){
		fprintf(stderr, "Usage: ./s IP_address port/service\n");
		return 1;
	}	

	/*comment out the daemon functionality for debugging purposes*/
//	daemon_init(argv[0], 0);
	/*get listening socket file descriptor*/
	sockfd = listening(argv[1], argv[2]);

	/*establishing signal handler to catch SIGCHLD*/
	signal(SIGCHLD, sig_child);	
	for(;;){
		if (sockfd >= 0) { 
			if ((clisockfd = accept(sockfd, (struct sockaddr *) &cliaddr, &len)) == -1){
				perror("Accept error\n");
				return -1;			
			}
			printf ("A client %s has connected\n", sock_ntop((struct sockaddr *) &cliaddr, len));
			if ((childpid = fork()) == 0){
				/*closing listening socket inside child process*/
				close(sockfd);
				printf("gets here first, clisockfd: %d\n", clisockfd);
			
				if (handle_http(clisockfd) < 0){
					fprintf(stderr, "Failed to handle http request\n");
				}				
				exit(0);
			}
			/*closing connnection socket in the parent process*/
		
			close(clisockfd);
		}	
		
	}	
	closelog();
	return 0;



}
