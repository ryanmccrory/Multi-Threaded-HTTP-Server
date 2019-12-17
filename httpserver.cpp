//Ryan McCrory
//Web Server
//asgn3
//Note: Server will always exit if -a flag is not used 

#include <cstdlib>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include <string.h>
#include <cstdint>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/stat.h>
#include <mutex>
#include <pthread.h>
#include <getopt.h>
#include <queue>
#include "city.h"

using namespace std;

//initialize locks and condition variables
pthread_mutex_t mutex_que = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_thread = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_dispatch = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_avail = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_thread = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_dispatch = PTHREAD_COND_INITIALIZER;

//global variables
int32_t log_file;
int32_t mapping_file;
int32_t start_offset;
int32_t address [8000];
bool is_log;
bool is_mapping;
std::queue<int32_t> clients;
int32_t available_threads;



void patch(int32_t cl, char *file_name, uint32_t bytes){
	cout << '\n' << "in patch" << '\n';
	//declare variables
	bool bad = false;
	char *buffer = (char *) malloc(bytes * sizeof(char));
	char *action = (char *) malloc(bytes * sizeof(char));
	char *resource = (char *) malloc(bytes * sizeof(char));
	char *alias = (char *) malloc(bytes * sizeof(char));
	char *entry = (char *) malloc(bytes * sizeof(char));
	//receive data from client
	recv(cl, buffer, bytes, 0);
	//parse data
	sscanf(buffer, "%s %s %s\n", action, resource, alias);
	//open file if exists
	int32_t local_file = open(resource, O_RDWR);
	if (strlen(resource) != 27){
		size_t length = strlen(resource);
		if (address[CityHash32(resource, length)%8000] == 0){
			bad = true;
			char bad_header [55];
			sprintf(bad_header, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
			send(cl, bad_header, strlen(bad_header), 0);
		} else {
			bad = false;
		} 
	//error if file does not exist
	} else if(local_file == -1){
		bad = true;
		char not_found_header [55];
		sprintf(not_found_header, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
		send(cl, not_found_header, strlen(not_found_header), 0);
	//invalid if over 128 bytes
	}else if ((strlen(resource) + strlen(alias)) > 126 ){
		bad = true;
		char bad_header [55];
		sprintf(bad_header, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
		send(cl, bad_header, strlen(bad_header), 0);
	}
	if (bad == false){
		cout << "alias: " << alias << " resource: " << resource << " spot: " << start_offset << '\n';
		sprintf(entry, "%s %s\n", alias, resource);
		pwrite(mapping_file, entry, 128, start_offset);
		size_t length = strlen(alias);
		address[CityHash32(alias, length)%8000] = start_offset;
		//determine content length
		long length2 = 0;
		struct stat st;
		if (stat(file_name, &st)){
			length2 = 0;
		} else {
			length2 = st.st_size;
		}
		//send ok header
		char ok_header [55];
		sprintf(ok_header, "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\n\r\n", length2);
		send(cl, ok_header, strlen(ok_header), 0);
		start_offset += 128;
	}
	//free memory
	free(buffer);
	free(action);
	free(resource);
	free(alias);
	free(entry);
	close(local_file);
	return;
}


// Not finished
void logging(char *buffer){
	write(log_file, buffer, sizeof(buffer));
	//write(log_file, buffer, sizeof(file_name));
	return;
}



//get method, copies a file from the server and puts it in the client directory
void get(int32_t cl, char *file_name, uint32_t bytes){
	cout << '\n' << "in get" << '\n';
	//deal with mapping
	if (strlen(file_name) != 27){
		char *resource = (char *) malloc(bytes * sizeof(char));
		char *alias = (char *) malloc(bytes * sizeof(char));
		char *buff = (char *) malloc(bytes * sizeof(char));
		size_t length = strlen(file_name);
		uint32_t spot = address[CityHash32(file_name, length)%8000];
		pread(mapping_file, buff, 128, spot);
		//parse
		sscanf(buff, "%s %s\n", alias, resource);
		cout << "alias: " << alias << " resource: " << resource << " spot: " << spot << '\n';
		int32_t iterator = 0;
		//while alias points to another alias
		while(strlen(resource) != 27){
			//possibly hanging? counter to make sure it doesnt
			iterator++;
			length = strlen(resource);
			spot = address[CityHash32(resource, length)%8000]; 
			if (spot == 0 || iterator == 10){
				char bad_header [55];
				sprintf(bad_header, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
				send(cl, bad_header, strlen(bad_header), 0);
				free(resource);
				free(alias);
				free(buff);
				return;
			} else {
				pread(mapping_file, buff, 128, spot);
				//parse
				sscanf(buff, "%s %s\n", alias, resource);
			}
		}
		sprintf(file_name, "%s", resource);
		if (spot == 0){
			char bad_header [55];
			sprintf(bad_header, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
			send(cl, bad_header, strlen(bad_header), 0);
			free(resource);
			free(alias);
			free(buff);
			return;
		}
		free(resource);
		free(alias);
		free(buff);
	}
	//make sure resource is 27 characters
	if (strlen(file_name) != 27){
		char bad_header [55];
		sprintf(bad_header, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
		send(cl, bad_header, strlen(bad_header), 0);
		return;
	}		
	//check that resource is only ascii characters
	if (strspn(file_name, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") != 27){
		char bad_header [55];
		sprintf(bad_header, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
		send(cl, bad_header, strlen(bad_header), 0);
		return;
	}
	//determine content length
	long length = 0;
	struct stat st;
	if (stat(file_name, &st)){
		length = 0;
	} else {
		length = st.st_size;
	}
	//open file if exists
	int32_t local_file = open(file_name, O_RDWR);
	//error if file does not exist
	if (local_file == -1){
		char not_found_header [55];
		sprintf(not_found_header, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
		send(cl, not_found_header, strlen(not_found_header), 0);
		return;
	}
	//send ok header
	char ok_header [55];
	sprintf(ok_header, "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\n\r\n", length);
	send(cl, ok_header, strlen(ok_header), 0);
	//similar code from dog() to read/ write data
	char *buffer = (char *) malloc(bytes * sizeof(char));
	int32_t read_bytes = read(local_file, buffer, bytes);
	while (read_bytes > 0){
		send(cl, buffer, read_bytes, 0);
		read_bytes = read(local_file, buffer, bytes);
	}
	//free memory
	free(buffer);
	return;
}



//put method, copies a file from the client and puts it in the server directory
void put(int32_t cl, char *file_name, uint32_t bytes){
	cout << '\n' << "in put" << '\n';
	//deal with mapping
	if (strlen(file_name) != 27){
		char *resource = (char *) malloc(bytes * sizeof(char));
		char *alias = (char *) malloc(bytes * sizeof(char));
		char *buff = (char *) malloc(bytes * sizeof(char));
		size_t length = strlen(file_name);
		uint32_t spot = address[CityHash32(file_name, length)%8000];
		pread(mapping_file, buff, 128, spot);
		//parse
		sscanf(buff, "%s %s\n", alias, resource);
		cout << "alias: " << alias << " resource: " << resource << " spot: " << spot << '\n';
		int32_t iterator = 0;
		//while alias points to another alias
		while(strlen(resource) != 27){
			//possibly hanging? counter to make sure it doesnt
			iterator++;
			length = strlen(resource);
			spot = address[CityHash32(resource, length)%8000]; 
			if (spot == 0 || iterator == 10){
				char bad_header [55];
				sprintf(bad_header, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
				send(cl, bad_header, strlen(bad_header), 0);
				free(resource);
				free(alias);
				free(buff);
				return;
			} else {
				pread(mapping_file, buff, 128, spot);
				//parse
				sscanf(buff, "%s %s\n", alias, resource);
			}
		}
		sprintf(file_name, "%s", resource);
		if (spot == 0){
			char bad_header [55];
			sprintf(bad_header, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
			send(cl, bad_header, strlen(bad_header), 0);
			free(resource);
			free(alias);
			free(buff);
			return;
		}
		free(resource);
		free(alias);
		free(buff);
	}
	//make sure resource is 27 characters
	if (strlen(file_name) != 27){
		char bad_header [55];
		sprintf(bad_header, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
		send(cl, bad_header, strlen(bad_header), 0);
		return;
	}		
	//check that resource is only ascii characters
	if (strspn(file_name, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") != 27){
		char bad_header [55];
		sprintf(bad_header, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
		send(cl, bad_header, strlen(bad_header), 0);
		return;
	}
	//below is from before
	//create buffer and recieve data from client
	char *buffer = (char *) malloc(bytes * sizeof(char));
	int32_t read_bytes = recv(cl, buffer, bytes, 0);
	//create new file with proper permissions
	int32_t new_file = open(file_name, O_CREAT | O_WRONLY, 0644);
	//send header
	char ok_header [55];
	sprintf(ok_header, "HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n");
	send(cl, ok_header, strlen(ok_header), 0);
	//write to file same as dog
	while (read_bytes > 0){
		write(new_file, buffer, read_bytes);
		read_bytes = recv(cl, buffer, bytes, 0);
	}
	//close file and free memory
	close(new_file);
	free(buffer);
	return;
}



//parse method, argument is client file descriptor
void parse (int32_t cl){
	//read incoming data
	uint32_t bytes = 1024;
	char *buffer = (char *) malloc(bytes * sizeof(char));
	read(cl, buffer, bytes);
	//declare variables
	char *action = (char *) malloc(bytes * sizeof(char));
	char *file_name = (char *) malloc(bytes * sizeof(char));
	char *http = (char *) malloc(bytes * sizeof(char));
	//parse data using sscanf
	sscanf(buffer, "%s %s %s\r\n", action, file_name, http);
	if (is_log == true){
		logging(action);
		logging(file_name);
	}
	//if first character of filename is '/', ignore it
	if (strlen(file_name) == 28){
		//remove it from string
		std::string temp = file_name;
		if(temp.at(0) == '/'){
			temp.erase(0,1);
			strcpy(file_name, temp.c_str());
		} 
	}
	//error checking for not HTTP/1.1
	if (strcmp(http, "HTTP/1.1") != 0){
		char bad_header [55];
		sprintf(bad_header, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
		send(cl, bad_header, strlen(bad_header), 0);
		return;
	}
	//call put or get depending on what action is
	if (strcmp(action, "PUT") == 0){
		put(cl, file_name, bytes);
	} else if (strcmp(action, "GET") == 0){
		get(cl, file_name, bytes);
	} else if (strcmp(action, "PATCH") == 0){
		patch(cl, file_name, bytes);
	} else {
		//if improper message, send error: not put or get
		char bad_header [55];
		sprintf(bad_header, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
		send(cl, bad_header, strlen(bad_header), 0);
		return;
	}
	//free memory
	free(buffer);
	free(action);
	free(file_name);
	free(http);
	return;
}



//deal with threads
void* handle_connection(void* arg){
	//this is just to get rid of warning
	long not_used = (long)arg;
	not_used++;
	while (true){
		//wait for a connection
		pthread_cond_wait(&cond_thread, &mutex_thread);
		pthread_mutex_lock(&mutex_avail);
		//critical region start
		available_threads--;
		//critical region end
		pthread_mutex_unlock(&mutex_avail);
		pthread_mutex_lock(&mutex_que);
		//critical region start
		int32_t client = clients.front();
		clients.pop();
		//critical region end
		pthread_mutex_unlock(&mutex_que);
		parse(client);
		pthread_mutex_lock(&mutex_avail);
		//critical region start
		available_threads++;
		//critical region end
		pthread_mutex_unlock(&mutex_avail);
		//alert dispatcher when finished
		pthread_cond_signal(&cond_dispatch);	
	}
}



//listen to put and get requests on user specified server
int main (int argc, char *argv[]){
	//if no args
	if (argc == 1){
		cerr << "error, no specified IP address" << "\n";
		exit(0);
	}
	char * log_name;
	char * mapping_name = nullptr;
	int32_t num_t = 4;
	//deal with options
	int option;
	extern int optind;
	while ((option = getopt(argc, argv, "a:N:l:")) != -1){
		switch (option){
			case 'a' :
				mapping_name = optarg;
				is_mapping = true;
				break;
			case 'l' :
				log_name = optarg;
				is_log = true;
				break;
			case 'N' :
				num_t = atoi(optarg);
				break;
			case '?': 
				break;
		}
	}
	//exit if mapping file doesnt exist
	if (mapping_name == nullptr){ 
		cout << "error, no specified mapping file" << "\n";
		exit(0);
	}		
	//initialize hostname and port
	char * hostname;
	char * port = nullptr;
	//set port to 2nd arg if there is one, 80 if not
	if (argc == 2){
		hostname = argv[1];
		sprintf(port, "80");
	} else {
		if (argc - optind == 2){
			hostname = argv[optind];
			port = argv[optind + 1];
		} else {
			hostname = argv[1];
			port = argv[2];
		}
	}
	//code given by professor miller to open a socket
	struct addrinfo *addrs, hints = {};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	getaddrinfo(hostname, port, &hints, &addrs);
	int main_socket = socket(addrs->ai_family, addrs->ai_socktype, addrs->ai_protocol);
	int enable = 1;
	setsockopt(main_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
	bind(main_socket, addrs->ai_addr, addrs->ai_addrlen);
	listen(main_socket, 16);
	// Your code, starting with accept(), goes here
	// create the log file if -l is specified.
	if (is_log == true){
		log_file = open(log_name, O_CREAT | O_WRONLY, 0644);
	}
	// create the mapping file if -a is specified.
	if (is_mapping == true){
		cout << "in main" << '\n';
		mapping_file = open(mapping_name, O_CREAT | O_RDWR, 0644);
		start_offset = 1;
		char *buffer2 = (char *) malloc(1024 * sizeof(char));
		char *alias = (char *) malloc(1024 * sizeof(char));
		char *resource = (char *) malloc(1024 * sizeof(char));
		int32_t read_bytes = pread(mapping_file, buffer2, 128, start_offset);
		while (read_bytes > 0){
			sscanf(buffer2, "%s %s\n", alias, resource);
			size_t length = strlen(alias);
			address[CityHash32(alias, length)%8000] = start_offset;
			cout << "next address is " << address[CityHash32(alias, length)%8000] << " with alias " << alias << '\n';
			start_offset += 128;
			read_bytes = pread(mapping_file, buffer2, 128, start_offset);
		}
	}
	//create worker threads
	pthread_t *workers = (pthread_t *)malloc(sizeof(pthread_t) * num_t);
	for (int i = 0; i < num_t; i++){
			pthread_create(&workers[i], NULL, handle_connection, NULL);
	}
	available_threads = num_t;
	while (true){
		int32_t cl = accept(main_socket, NULL, NULL);
		pthread_mutex_lock(&mutex_que);
		//critical region start
		clients.push(cl);
		//critical region end
		pthread_mutex_unlock(&mutex_que);
		pthread_mutex_lock(&mutex_avail);
		while (available_threads == 0){
			//sleep while no available threads 
			pthread_mutex_unlock(&mutex_avail);
			pthread_cond_wait(&cond_dispatch, &mutex_dispatch);
			pthread_mutex_lock(&mutex_avail);
		}
		pthread_mutex_unlock(&mutex_avail);
		pthread_cond_signal(&cond_thread);
	}
}
