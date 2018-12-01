/******************************************
*    Client Program                       *
*                                         *
******************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <unistd.h>

#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

#include <netinet/in.h>

#include <netdb.h>

int socket_client_fd;	// client socket desciptor

void close_session()
{
	char client_message[2000];
	strcpy(client_message,"exit");

    write(socket_client_fd , client_message, strlen(client_message));

    //close socket
    close(socket_client_fd);
    printf("\nsession closed...\n");
	exit(0);
};
int check_empty_text(char *line) {
  while (*line != '\0') {
    if (!isspace((unsigned char)*line))
      return 0;
    line++;
  }
  return 1;
};

int main(int argc , char *argv[])
{
	int port_no = 5000;	                    //port number to listen from for server

	struct sockaddr_in server_address;      // to define server address configuration

	char client_message[2000]="connect";	//client message buffer
    char server_reply[2000];   //server reply message buffer
    char *data;                //client data input
	size_t len = 0;	           //len of line

	int message_size;	//message sent or received size
    int status_code ;   //for status code
    int choice;         //choice of user

    fd_set readfds;	//fd set

    struct hostent *server_details; //to store server host information

    signal(SIGTSTP,&close_session);      //for Ctrl+Z
    signal(SIGINT,&close_session);       //for Ctrl+C
    signal(SIGTERM,&close_session);		 //for terminal close

    //Create client socket
    socket_client_fd = socket(AF_INET , SOCK_STREAM , 0);

    server_details = gethostbyname("localhost");    //to resolve host name to an ip address 

    bzero((char *) &server_address, sizeof(server_address)); // to initialize structure with 0

	server_address.sin_family = AF_INET;	//ipv4 setting     
    bcopy((char *)server_details->h_addr, (char *)&server_address.sin_addr.s_addr, server_details->h_length); // copy server ip address

    server_address.sin_port = htons(port_no); // port no of server

    //to establish connection with the server
    status_code = connect(socket_client_fd, (struct sockaddr *)&server_address, sizeof(server_address));

    if (status_code < 0)	//error case
    {
    	//print the error message
        printf("error: connection to server failed\n");
        return 1;
    }

    message_size = read(socket_client_fd , server_reply , 2000);	//read ack from server

    if(!strcmp("+connection_overflow\n",server_reply) || !strcmp("+connection_overflow",server_reply))	//when limit exceeds
	{
		printf("Connection Limit Exceeded !!\n");
		close_session();
	}

    printf("Connection established with server...\n");
    printf("Please enter \n\"EXIT\" to close the session\n");
    printf("\"send <user-id> <message>\" to send messages\n");
    printf("\"+broadcast <message>\" to broadcast\n");
    printf("\"+online\" for online users\n");

    while(1)
	{
	   	FD_CLR(socket_client_fd, &readfds);	
        FD_SET(socket_client_fd, &readfds);	//add socket to fd set
        FD_SET(0, &readfds);	//add stdin to fd set

        select(socket_client_fd+1, &readfds, NULL, NULL, NULL);	//to run read and write parallel

        if (FD_ISSET(0, &readfds)) 
        {
            status_code = getline(&data, &len, stdin);

            if (status_code == 0 )	//error case
            {
                printf("<ERROR> read from console failed\n");
                continue;
            }
            else if (check_empty_text(data))
            {
                printf("<ERROR> cannot send empty messages\n");
                continue;
            }
            else if(!strcmp("EXIT\n",data) || !strcmp("exit\n",data))	//when client wants to logout
			{
				close_session();	//user session close
				break;
			}
			strcpy(client_message,strtok(data, "\n"));
            message_size = write(socket_client_fd , client_message, strlen(client_message));
            if(message_size == -1)
            {
                //print the error message
                printf("error: write to client failed msg:%s",client_message);
            }
            memset(client_message,0,sizeof(client_message));    // to flush the buffer
        } 
        else if (FD_ISSET(socket_client_fd, &readfds)) //if there is anything from server to write
        {
            message_size = read(socket_client_fd , server_reply , 2000);   //read from socket_client_fd

			if(message_size == -1)	//error
			{
				//print the error message
				printf("error: recv from server failed\n");
				printf("Please enter \"EXIT\" to close the session\n");
				continue;
			}
			//when server exits a message is recieved from server
			if(!strcmp("+EXIT\n",server_reply) || !strcmp("+exit\n",server_reply)||!strcmp("+EXIT",server_reply) || !strcmp("+exit",server_reply))
		    {
		    	printf("Server shutdown\n");
		       	close_session();	//close session
				break;
		    }
			printf("Message from server: \n%s\n",server_reply);	//display messages from server
            memset(server_reply,0,sizeof(server_reply));    // to flush the buffer
        }
	}
    //close socket
    close(socket_client_fd);
	return 0;
}