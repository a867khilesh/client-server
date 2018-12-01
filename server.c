/******************************************
*    Server Program                       *
*                                         *
******************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/mman.h>
#include <pthread.h>
#include <time.h>

#define MAX_MSG_LEN 2000		// max length of Message
#define BUFFER_SIZE 30			// size of buffer to store messages
#define PORT_NO 5000			// port no server through which listens for connections

// list of user defined data types
typedef struct { int socket_fd; int id; int client_index; } client;						//structure for client details
typedef struct { int source; int destination; char message[MAX_MSG_LEN]; } message_pkt;	//structure for message pkt stored at server buffer
char server_ack[MAX_MSG_LEN] = "connection accepted";   								//server ack to client buffer

//	global data
int socket_server_fd;			//server socket descriptor for listening to connections
int socket_client_fd;			//server socket descriptor for listening to connections
client *clientlist;				//list of online clients
int * clientcount;				//count of online clients
message_pkt *messagelist;		//list of messages to be delivered by server
int *messagelistsize;			//count of messages in the buffer
sem_t *client_writer_semaphore;	//semaphore to update client list
sem_t *client_reader_semaphore;	//semaphore to read client list
sem_t *buffer_semaphore;		//semaphore for updating message bufer
int *client_reader_count;		//count of processes reading client list
//	end of global data

/*
Name 				: 	check_empty_text
Desciption 			: 	To check whether text is empty or not, if empty returns 1 else 0

Parameters
line  				:	input text to check for empty or not

*/
int check_empty_text(char *line) 
{
  	while (*line != '\0') 
  	{
    	if (!isspace((unsigned char)*line))
      		return 0;
    	line++;
  	}
  	return 1;
};
/*
Name 				: 	client_error
Desciption 			: 	Run when there is client error to close the client

Parameters
client_id  			:	id of client
client_list 		:	ptr to list of clients
client_count 		:	ptr to count of clients
message_list 		:	ptr to list of messages
message_list_size 	:	ptr to count of messages
client_index		:	index of client in the client list

*/	
void client_error(int client_id,client *client_list, int * client_count,message_pkt *message_list,int *message_list_size,int client_index)
{
	int i,count;								//iterator
	char int_to_str[10];						//temp string variable
	message_pkt message[5];						//message packet
	char client_list_msg[MAX_MSG_LEN]="\n";		//new list to be sent
	
	close(client_list[client_index].socket_fd);	//close client socket

	printf("closing : %d\n", client_id);

	sprintf(int_to_str, "%d", client_id);		//integer to string conversion
	strcat(client_list_msg,int_to_str);
	strcat(client_list_msg," User Disconnected...\nNew list of Online Users\n");

	sem_wait(client_writer_semaphore); 			//lock on client_list

	(*client_count)--;							//decrement cient count

	for(i=client_index;i<*client_count;i++)		//update client list
	{
		client_list[i] = client_list[(i+1)];		//overwrite
	}
           
    printf("updated client list : %d\n", client_id);

	for(i=0;i<*client_count;i++)				//creating message of new list of online clients
	{
		sprintf(int_to_str, "%d", (i+1));
		strcat(client_list_msg,int_to_str);
		strcat(client_list_msg," : ");
		sprintf(int_to_str, "%d", client_list[i].id);
		strcat(client_list_msg,int_to_str);
		strcat(client_list_msg,"\n");
	}

	count = *client_count;						//client count

	sem_post(client_writer_semaphore); 			//release lock on client_list

	sem_wait(buffer_semaphore); 				//lock the message buffer

    sem_wait(client_reader_semaphore); 			//lock on read count variable

    *client_reader_count++;						//increment read count

    if (*client_reader_count == 1) 
        sem_wait(client_writer_semaphore);    	//lock from updating client list

    sem_post(client_reader_semaphore); 			//release lock on read count variable

	for(i=0;i<count;i++)						//store broadcast messages in the buffer
	{	
		message[i].source = client_id;					//client id in source as it is broadcasting
		strcpy(message[i].message,client_list_msg);
		message[i].destination = client_list[i].id;
		message_list[*message_list_size] = message[i];
		(*message_list_size)++;
	}

	sem_wait(client_reader_semaphore); 			//lock on read count variable

    *client_reader_count--;						//decrement read count

    if (*client_reader_count == 0) 
        sem_post(client_writer_semaphore);    	//release lock on read count variable

    sem_post(client_reader_semaphore); 			//release lock on readcount variable

	sem_post(buffer_semaphore); 				//release when buffer empty

	kill(getpid(),SIGTERM);						//kill child process of client
	exit(0);									//in case above command fails
};
/*
Name 		: 	close_session
Desciption 	: 	To close session of the server, or when server abruptly closes

*/
void close_session()
{
	char client_message[MAX_MSG_LEN];			//message to clients
	int i;										//iterator

	strcpy(client_message,"+exit");

	sem_wait(client_reader_semaphore); 			//lock on read count variable

    *client_reader_count++;     				//increment read count variable
                                  
    if (*client_reader_count==1)     
        sem_wait(client_writer_semaphore);    	//lock on client list            

    sem_post(client_reader_semaphore); 			//release lock on read count variable

	for(i=0;i<*clientcount;i++)					//iterate over clients to broadcast the close signal
	{
		write(clientlist[i].socket_fd , client_message, sizeof(client_message));
	}

	sem_wait(client_reader_semaphore); 			//lock on read count variable

    *client_reader_count--;						//increment read count variable

    if (*client_reader_count == 0) 
        sem_post(client_writer_semaphore);    	//release lock client list

    sem_post(client_reader_semaphore); 			//release lock on read count variable
    
    close(socket_server_fd);					//close socket
    printf("\nsession closed...\n");
    signal(SIGQUIT, SIG_IGN);					//signal all the children to quit
	kill(getpid(), SIGQUIT);					//kill the server process
	exit(0);									//in case kill fails
};
/*
Name 				: 	get_client_id
Desciption 			: 	returns socket descriptor of client

Parameters
client_id  			:	id of client
client_list 		:	ptr to list of clients
client_count 		:	ptr to count of clients

*/	
int get_client_id(client *client_list, int * client_count, int client_id)
{	
	int i,status=-1;							//iterator, status code of operations
	sem_wait(client_reader_semaphore); 			//lock on read count variable

    *client_reader_count++;     				//increment read count variable
                                  
    if (*client_reader_count==1)     
        sem_wait(client_writer_semaphore);    	//lock client list             

    sem_post(client_reader_semaphore); 			//release lock on read count variable

	for(i=0;i<*client_count;i++)				//iterate over clients list
	{
		printf("%d:%d\n", client_list[i].id,client_id);
		if(client_id == client_list[i].id)		//found then return descriptor
		{
			status=client_list[i].socket_fd;
			break;
		}
	}

	sem_wait(client_reader_semaphore); 			//lock on read count variable

    *client_reader_count--;     				//decrement read count variable
      
    if (*client_reader_count==0)     
        sem_post(client_writer_semaphore);    	//release lock on client list            

    sem_post(client_reader_semaphore); 			//release lock on readc ount variable
	return status;
}
/*
Name 				: 	get_online_clients
Desciption 			: 	send list of online clients

Parameters
client_id  			:	id of client
client_list 		:	ptr to list of clients
client_count 		:	ptr to count of clients
client_index		:	index of client in the client list

*/
void get_online_clients(int client_id, client *client_list, int * client_count,int client_index)
{
	int i,flag=0;								//iterator , flag
	char client_message[MAX_MSG_LEN];			//client message to be sent
	char int_to_str[10];						//for integer to string conversion

	strcpy(client_message,"\nList of Online Clients\n");

	sem_wait(client_reader_semaphore); 			//lock on read count variable

    *client_reader_count++;     				//increment read count variable
                                  
    if (*client_reader_count==1)     
        sem_wait(client_writer_semaphore);    	//lock client list             

    sem_post(client_reader_semaphore); 			//release lock on read count variable
              
	for(i=0;i<*client_count;i++)				//iterate over clients
	{
		if(client_id == client_list[i].id)
			continue;
		sprintf(int_to_str, "%d", (i+1));
		strcat(client_message,int_to_str);
		strcat(client_message," : ");
		sprintf(int_to_str, "%d", client_list[i].id);
		strcat(client_message,int_to_str);
		strcat(client_message,"\n");
		flag++;
	}

	sem_wait(client_reader_semaphore); 			//lock on read count variable

    *client_reader_count--;						//decrement read count variable

    if (*client_reader_count == 0) 
        sem_post(client_writer_semaphore);    	//release lock on client list

    sem_post(client_reader_semaphore); 			//release lock on read count variable

	if(flag==0)									//flag 0 no clients available
		strcpy(client_message,"\nNo other clients available\n");

	i = write(socket_client_fd , client_message, sizeof(client_message));

	if(i == -1)									//print the error message
	{
	    printf("error: could not send clients list");	
	    client_error(client_id,client_list,client_count,messagelist,messagelistsize,client_index);
	}
};
/*
Name 				: 	forward_client_messages
Desciption 			: 	send messages of client from message buffer

Parameters
client_id  			:	id of client
client_list 		:	ptr to list of clients
client_count 		:	ptr to count of clients
message_list 		:	ptr to list of messages
message_list_size 	:	ptr to count of messages
client_index		:	index of client in the client list

*/	
void forward_client_messages(int client_id,client *client_list, int * client_count,message_pkt *message_list,int *message_list_size,int client_index)
{
	int i,j,size;								//iterators , to store size of 
	int status_code;							//status of operation
	char temp_string[MAX_MSG_LEN];				//temporary string
	char message[MAX_MSG_LEN];					//message to be delivered

	if (*message_list_size)
	{
		sem_wait(buffer_semaphore); 			//lock on message buffer
		size = *message_list_size;
		for(i = 0,j = 0 ; i < size ; i++)		//iterate over the message buffer
		{
			if(message_list[i].destination == client_id)
			{									//if message is for the client
				strcpy(message,"\nFrom client ");
				sprintf(temp_string, "%d", message_list[i].source);
				strcat(message,temp_string);
				strcat(message," : \n");
				strcat(message,message_list[i].message);
				strcat(message,"\n");			//final message string to be delivered to user

				status_code = write(socket_client_fd , message, sizeof(message));

				printf("<SUCCESS> Message sent to User : %d\nMessage : %s",client_id,message);

				if(status_code == -1)			//print the error message
				{
					printf("<ERROR> could not write message to user : %d\nmessage : %s",client_id,message);
					client_error(client_id,client_list,client_count,message_list,message_list_size,client_index);
					break;
				}
				(*message_list_size)--;			//decrement message count
			}
			else								//to drop message from buffer after sending the message to client
			{
				message_list[j] = message_list[i];
				j++;							
			}
		}
		sem_post(buffer_semaphore); 			//release lock on message buffer
	}
};
/*
Name 				: 	send_message
Desciption 			: 	add message of client to message buffer

Parameters
client_id  			:	id of client
client_list 		:	ptr to list of clients
client_count 		:	ptr to count of clients
message_list 		:	ptr to list of messages
message_list_size 	:	ptr to count of messages
client_index		:	index of client in the client list
client_message 		: 	message of client

*/	
int send_message(int client_id,client *client_list, int * client_count,message_pkt *message_list,int *message_list_size,int client_index,char client_message[])
{
	int destination,status_code;				//destination id, status code of operations
	char *tokens,error_msg[MAX_MSG_LEN];		//tokens, error message
	message_pkt message;						//message object to be added to the message buffer

	tokens = strtok(client_message, " ");		//tokenization of message from client
	tokens = strtok(NULL," ");
	if(tokens == NULL)							//if null then no client id provided
	{
		strcpy(error_msg,"\n<ERROR> NO User ID provided\nFormat : send[SPACE]<user id>[SPACE]<your non-empty message>\n");
		status_code = write(socket_client_fd , error_msg, sizeof(error_msg));
		if(status_code == -1)					//print the error message
		{
			printf("<ERROR> could not write message to user : %d\nmessage : %s",client_id,error_msg);
			client_error(client_id,client_list,client_count,message_list,message_list_size,client_index);
			return -1;
		}
		return -1;
	}

	destination = atoi(tokens);					//converting destination id to string

	if(destination <= 0)						//invalid destination user id
	{
		strcpy(error_msg,"\n<ERROR> Invalid User ID provided\nFormat : send[SPACE]<user id>[SPACE]<your non-empty message>\n");
		status_code = write(socket_client_fd , error_msg, sizeof(error_msg));
		if(status_code == -1)					//print the error message
		{
			printf("<ERROR> could not write message to user : %d\nmessage : %s",client_id,error_msg);
			client_error(client_id,client_list,client_count,message_list,message_list_size,client_index);
			return -1;
		}
		return -1;
	}											
	else if( get_client_id(client_list,client_count, destination) == -1)	//user not available
	{											
		strcpy(error_msg,"\n<ERROR> Invalid User ID provided, User not available\nFormat : send[SPACE]<user id>[SPACE]<your non-empty message>\n");
		status_code = write(socket_client_fd, error_msg, sizeof(error_msg));
		if(status_code == -1)					//print the error message
		{
			printf("<ERROR> could not write message to user : %d\nmessage : %s",client_id,error_msg);
			client_error(client_id,client_list,client_count,message_list,message_list_size,client_index);
			return -1;
		}
		return -1;
	}	

	message.source = client_id;					//adding client id to message packet
	message.destination = destination;			//adding destination id to message packet

	tokens = strtok(NULL," ");

	if(tokens == NULL)							//empty message sent from client
	{
		strcpy(error_msg,"\n<ERROR> Cannot send an empty message\nFormat : send[SPACE]<user id>[SPACE]<your non-empty message>\n");
		status_code = write(socket_client_fd , error_msg, sizeof(error_msg));
		if(status_code == -1)					//print the error message
		{
			printf("<ERROR> could not write message to user : %d\nmessage : %s",client_id,error_msg);
			client_error(client_id,client_list,client_count,message_list,message_list_size,client_index);
			return -1;
		}
		return -1;
	}

	strcpy(message.message,tokens);
	while(tokens = strtok(NULL," "))			//creating the message string
	{
		strcat(message.message," ");
		strcat(message.message,tokens);
	}

	if(check_empty_text(message.message))		//check if message has only empty spaces in text or not
	{
		strcpy(error_msg,"\n<ERROR> Cannot send an empty message\nFormat : send[SPACE]<user id>[SPACE]<your non-empty message>\n");
		status_code = write(socket_client_fd, error_msg, sizeof(error_msg));
		if(status_code == -1)					//print the error message
		{
			printf("<ERROR> could not write message to user : %d\nmessage : %s",client_id,error_msg);
			client_error(client_id,client_list,client_count,message_list,message_list_size,client_index);
			return -1;
		}
		return -1;
	}

	sem_wait(buffer_semaphore); 				//lock message buffer

	message_list[*message_list_size] = message;	//add message to the buffer

	(*message_list_size)++;						//increment the message count

	sem_post(buffer_semaphore); 				//release message buffer lock

	return 0;
};
/*
Name 				: 	broadcast_message
Desciption 			: 	add broadcast messages of client to message buffer

Parameters
client_id  			:	id of client
client_list 		:	ptr to list of clients
client_count 		:	ptr to count of clients
message_list 		:	ptr to list of messages
message_list_size 	:	ptr to count of messages
client_index		:	index of client in the client list
client_message 		: 	message of client

*/
int broadcast_message(int client_id,client *client_list, int * client_count,message_pkt *message_list,int *message_list_size,int client_index,char client_message[])
{
	int destination,status_code,i;			//destination id, status code
	char *tokens,error_msg[MAX_MSG_LEN];	//tokens, error message
	char broadcast_msg[MAX_MSG_LEN];		//broadcast message to be sent
	message_pkt message[5];					//list of message objects created

	tokens = strtok(client_message, " ");	//tokenization of client message
	tokens = strtok(NULL," ");
	if(tokens == NULL)						//empty message or not
	{
		strcpy(error_msg,"\n<ERROR> Cannot send an empty message\nFormat : +broadcast[SPACE]<your non-empty message>\n");
		status_code = write(socket_client_fd , error_msg, sizeof(error_msg));
		if(status_code == -1)				//print error message
		{
			printf("<ERROR> could not write message to user : %d\nmessage : %s",client_id,error_msg);
			client_error(client_id,client_list,client_count,message_list,message_list_size,client_index);
			return -1;
		}
		return -1;
	}

	strcpy(broadcast_msg,tokens);			//creating message string
	while(tokens = strtok(NULL," "))
	{
		strcat(broadcast_msg," ");
		strcat(broadcast_msg,tokens);
	}

	if(check_empty_text(broadcast_msg))		//check whether client message is empty text or not
	{
		strcpy(error_msg,"\n<ERROR> Cannot send an empty message\nFormat : +broadcast[SPACE]<your non-empty message>\n");
		status_code = write(socket_client_fd, error_msg, sizeof(error_msg));
		if(status_code == -1)				//print the error message
		{
			printf("<ERROR> could not write message to user : %d\nmessage : %s",client_id,error_msg);
			client_error(client_id,client_list,client_count,message_list,message_list_size,client_index);
			return -1;
		}
		return -1;
	}

	sem_wait(buffer_semaphore); 			//lock message buffer

	sem_wait(client_reader_semaphore); 		//lock on read count variable

    *client_reader_count++;					//increment read count variable

    if (*client_reader_count == 1) 
        sem_wait(client_writer_semaphore);  //lock on client list

    sem_post(client_reader_semaphore); 		//release lock on read count variable

	for(i=0;i<*client_count;i++)			//iterate over client list
	{
		message[i].source = client_id;		//adding message for each client in message buffer
		strcpy(message[i].message,broadcast_msg);
		if(client_list[i].id == client_id)
			continue;
		message[i].destination = client_list[i].id;
		message_list[*message_list_size] = message[i];
		(*message_list_size)++;				
	}
	
	sem_wait(client_reader_semaphore); 		//lock on read count variable

    *client_reader_count--;					//decrement read count variable

    if (*client_reader_count == 0) 
        sem_post(client_writer_semaphore);  //release lock on client list

    sem_post(client_reader_semaphore); 		//release lock on read count variable

	sem_post(buffer_semaphore); 			//release lock on buffer
};
/*
Name 				: 	main
Desciption 			: 	Program control starts from here

*/
int main(int argc , char *argv[])
{
	struct sockaddr_in server_defined_address; 	//to define server address configuration
	struct sockaddr_in client_address; 			//to store client address configuration

	char client_message[MAX_MSG_LEN];			//client message buffer
	char client_message_copy[MAX_MSG_LEN];		//client message buffer
	char error_msg[MAX_MSG_LEN];				//error msg to client

    char *token; 								//for tokenization of client message
	int message_size;							//message sent or received size
	int client_address_size; 					//client address size
    int pid; 									//process id
    int client_id;								//randomly generated client id
    int client_index;							//indes of client in the client list
    int response_code;							//response code of operations

    time_t rawtime;								//raw time from os
  	struct tm * timeinfo;						//time information

  	//shaered data among processes
    int *client_count = (int *)mmap(0, sizeof(int), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);	//no. of clients connected
    client *client_list= mmap(0, 5*sizeof(client),PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS,-1, 0);
    message_pkt *message_list = mmap(0, BUFFER_SIZE*sizeof(message_pkt),PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS,-1, 0);
  	int *message_list_size = (int *)mmap(0, sizeof(int), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
  	//end

  	//list of semaphores
    sem_t* client_list_writer_semaphore =  (sem_t *)mmap(0, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);//semaphore for synchronization of central database
    sem_t* client_list_reader_semaphore =  (sem_t *)mmap(0, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);//semaphore for synchronization of central database
  	sem_t* msg_buffer_semaphore =  (sem_t *)mmap(0, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);//semaphore for synchronization of central database
	int* client_list_reader_count =   (int *)mmap(0, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);//semaphore for synchronization of central database
  	//end

    //semaphores initialization
    sem_init(client_list_writer_semaphore,1,1);
    sem_init(client_list_reader_semaphore,1,1);
    sem_init(msg_buffer_semaphore,1,1);
    *client_list_reader_count = 0;
    client_writer_semaphore = client_list_writer_semaphore;
    client_reader_semaphore = client_list_writer_semaphore;
    client_reader_count = client_list_reader_count;
    buffer_semaphore = msg_buffer_semaphore;
    //end

    *client_count = 0;							//initial client count at main server process
    *message_list_size = 0;						//initial ptr to buffer
    clientlist = client_list;					//pointing to global variable
    clientcount = client_count;					//pointing to global variable

    signal(SIGTSTP,&close_session);      		//for Ctrl+Z
    signal(SIGINT,&close_session);       		//for Ctrl+C
    signal(SIGTERM,&close_session);		 		//for terminal close


	bzero((char *) &server_defined_address, sizeof(server_defined_address));	//to initialize structure with 0

	server_defined_address.sin_family = AF_INET;								//ipv4 setting 
    server_defined_address.sin_addr.s_addr = INADDR_ANY;						//bind to any available local address
    server_defined_address.sin_port = htons(PORT_NO); 							//port no to bind to

    socket_server_fd = socket(AF_INET , SOCK_STREAM , 0);						//server socket

    if (socket_server_fd == -1)													//to check for if socket is not created
    {
        printf("\nerror: socket not created\n");								//print the error message
        return 1;
    }
    printf("\nServer socket created ...\n");

    if( bind(socket_server_fd,(struct sockaddr *)&server_defined_address , sizeof(server_defined_address)) < 0)// to bind server scoket to an ip address and port no
    {
        printf("error: bind failed\n");											//print the error message
        return 1;
    }
    printf("Socket binded to port: %d\n",PORT_NO);

    listen(socket_server_fd,5);													//to listen for connections from clients

    printf("Waiting for incoming connections...\n");	

    while(1)																	//infinite loop
    {
    	memset(client_message,0,sizeof(client_message));    					// to flush the buffer

    	//accept connection from an incoming client
    	socket_client_fd = accept(socket_server_fd, (struct sockaddr *)&client_address, (socklen_t*)&client_address_size);

        if (socket_client_fd < 0 || *client_count == 5)							//reject connection with client
        {	
        	*client_count == 5 ? strcpy(error_msg,"+connection_overflow"):strcpy(error_msg,"error: accept failed < fatal error >\n");
            printf("%s",error_msg);	//print the error message
            message_size = write(socket_client_fd , error_msg, sizeof(error_msg));
	        if(message_size == -1)
	        {
	            printf("error: write to client failed msg:%s",error_msg);		//print the error message
	        }
            close(socket_client_fd);											//close client socket
            continue;
        }

        response_code = fcntl(socket_client_fd, F_SETFL, fcntl(socket_client_fd, F_GETFL, 0) | O_NONBLOCK);

        if(response_code == -1)													//print the error message
	    {
	        printf("error: fcntl could not make non-blocking socket | socket generated is blocking\n");
	    }

        message_size = write(socket_client_fd , server_ack, sizeof(server_ack));

	    if(message_size == -1)
	    { 
	        printf("error: write to client failed msg:%s\n",error_msg);			//print the error message
	    }

        client_id = (rand()%90000) + 10000;										//random client id between 10000 - 99999

        printf("Connection accepted from new client, generated id = %d\n",client_id);

        //client record initialization
        time ( &rawtime );													
  		timeinfo = localtime ( &rawtime );

  		char client_id_str[10];
		sprintf(client_id_str, "%d", client_id);

        strcpy(client_message,"\nWelcome User\nyour User id: ");
        strcat(client_message,client_id_str);strcat(client_message,"\nConnection time at server:");
        strcat(client_message,asctime (timeinfo));

        sem_wait(client_writer_semaphore); 										//lock on client list

  		client_list[*client_count].socket_fd = socket_client_fd;				//store client socket fd
  		client_list[*client_count].id = client_id;								//store client id
  		client_list[*client_count].client_index = *client_count;				//store client index in the list

  		client_index = *client_count;											//store client index in the child process

  		//end of initialization of client

  		++(*client_count);														//increment client count

  		sem_post(client_writer_semaphore); 										//release lock on client list

        message_size = write(socket_client_fd , client_message, sizeof(client_message));

	    if(message_size == -1)
	    {  
	        printf("error: write to client failed msg:%s",error_msg);			//print the error message
	        client_error(client_id,client_list,client_count,message_list,message_list_size,client_index);
	    }
        if ((pid = fork()) == -1)												//if a child process has failed to create, still the listner has to continue
        {
            continue;
        }
        else if(pid > 0)														//returned to the parent process, still it continues to listen
        {
            continue;
        }
        else if(pid == 0)   													//returned to child process
        { 
        	signal(SIGTSTP,SIG_IGN);      										//for Ctrl+Z ignored in child
    		signal(SIGINT,SIG_IGN);       										//for Ctrl+C ignored in child
    		signal(SIGTERM,SIG_IGN);	 										//for terminal close ignored in child
        	while(1)
        	{
        		memset(client_message,0,MAX_MSG_LEN);    								//to flush the buffer
	        	message_size = read(socket_client_fd , client_message , MAX_MSG_LEN);	//read msg from client
		        if(message_size == -1)													//send messages when no data is sent by user
		        {
		            forward_client_messages(client_id,client_list,client_count,message_list,message_list_size,client_index);
		        }
		        else if(*message_list_size == BUFFER_SIZE)								//if buffer full
		        {
		            strcpy(client_message_copy,"ERROR : Server Overload...\nMessage Discarded\nTry again later\n");
			       	message_size = write(socket_client_fd , client_message_copy, sizeof(client_message_copy));
					if(message_size == -1)
					{ 
						printf("ERROR: write to client failed msg:%s\n",error_msg);		//print the error message
						client_error(client_id,client_list,client_count,message_list,message_list_size,client_index);
					}
		        }
		        else
		        {
		        	printf("Message from client : %s\n",client_message);
			        if(!strcmp("EXIT\n",client_message) || !strcmp("exit\n",client_message)||!strcmp("EXIT",client_message) || !strcmp("exit",client_message))
			       	{
			       		printf("Client %d session closed\n",client_id);
			            client_error(client_id,client_list,client_count,message_list,message_list_size,client_index);
			       	}
			       	else if(!strcmp("+online\n",client_message) || !strcmp("+online",client_message))	//request for online clients
			       	{
			       		printf("Online Client list requested by client : %d\n",client_id);
			            get_online_clients(client_id,client_list,client_count,client_index);
			       	}
			       	else
			       	{    
			       		strcpy(client_message_copy,client_message);
			       		token = strtok(client_message_copy, " ");

			       		if(token!= NULL && !strcmp(token,"send"))							//if send command comes
			       		{	
			            	send_message(client_id,client_list,client_count,message_list,message_list_size,client_index,client_message);
			       		}
			       		else if(token!= NULL && !strcmp(token,"+broadcast"))				//if broadcast message
			       		{	
			            	broadcast_message(client_id,client_list,client_count,message_list,message_list_size,client_index,client_message);
			       		}
			       		else
			       		{
			       			strcpy(client_message_copy,"echo server reply : ");
			       			strcat(client_message_copy,client_message);
			       			strcat(client_message_copy,"\n");
			       			if(token == NULL || !strcmp(token,"") || !strcmp(token,"\n"))	//if token null
			       			{
			       				strcpy(client_message_copy,"Error...empty string\n");
			       			}
			       			message_size = write(socket_client_fd , client_message_copy, sizeof(client_message_copy));
						    if(message_size == -1)
						    {   
						        printf("ERROR: write to client failed msg:%s\n",error_msg);	//print the error message
						        client_error(client_id,client_list,client_count,message_list,message_list_size,client_index);
						    }
			       		}
			       	}
			    }
		        memset(client_message,0,sizeof(client_message));    						// to flush the buffer
		    }	
        }
    }
    close(socket_server_fd);																//close server socket
    return 0;	
}