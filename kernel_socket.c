#include "tinyos.h"
#include "kernel_proc.h"
#include "kernel_pipe.h"
#include "kernel_streams.h"
#include "util.h"
#include "kernel_dev.h"
#include "kernel_cc.h"
#include "kernel_pipe.h"

// Port map - each port maps to a listener socket
socket_cb* PORT_MAP[MAX_PORT+1] = {NULL};

// Dummy functions for unused file_ops slots
void* do_nothing_pt(uint minor){
    return NULL;
}

int do_nothing(void* this, char* buf, unsigned int size){
    return -1;
}

// Read data from socket
int socket_read(void* socket_cb_t, char* buf, unsigned int n){

	socket_cb* socketcb = (socket_cb*)socket_cb_t;
	if(socketcb->type!=SOCKET_PEER) return -1;
	
	if(socketcb->peer_s.read_pipe!=NULL){
		int read_num = pipe_read(socketcb->peer_s.read_pipe, buf, n);
		return read_num;
	}else
	{
		return -1;
	}
	
}

// Write data to socket
int socket_write(void* socket_cb_t, const char *buf, unsigned int n){
	
	socket_cb* socketcb = (socket_cb*)socket_cb_t;
	if(socketcb->type!=SOCKET_PEER) return -1;
	if(socketcb->peer_s.write_pipe!=NULL){
		int write_num = pipe_write(socketcb->peer_s.write_pipe, buf, n);
		return write_num;
	}else
	{
		return -1;
	}
	
}

// Close socket
int socket_close(void* socket_cb_t){

	socket_cb* socketcb = (socket_cb*)socket_cb_t;

	if(socketcb->refcount==1){	 // Waiting in accept/connect
		socketcb->refcount--;
		if(socketcb->type == SOCKET_LISTENER){ 
			PORT_MAP[socketcb->port] = NULL;
				while(!is_rlist_empty(&socketcb->listener_s.queue))
					rlist_pop_front(&socketcb->listener_s.queue);
				kernel_broadcast(&socketcb->listener_s.req_available);	
				return 0;
		}
		else{ 					
			return 0;
		}
	}
	else if(socketcb->refcount==0){	// Not waiting anywhere
			if(socketcb->type==SOCKET_PEER){
					if(socketcb->peer_s.write_pipe != NULL){
						pipe_writer_close(socketcb->peer_s.write_pipe);
						socketcb->peer_s.write_pipe = NULL;
					}
					if(socketcb->peer_s.read_pipe != NULL){
						pipe_reader_close(socketcb->peer_s.read_pipe);	
						socketcb->peer_s.read_pipe = NULL;
					}		
				free(socketcb);
				return 0;
			}
			else if(socketcb->type==SOCKET_LISTENER){
				PORT_MAP[socketcb->port] = NULL;
				free(socketcb);
				return 0;
			}
			else if(socketcb->type==SOCKET_UNBOUND){
				free(socketcb);
				return 0;
			}
		 }
		 return -1;

}

// File operations for sockets
file_ops socket_file_ops = {
  .Open = do_nothing_pt,
  .Read = socket_read,
  .Write = socket_write,
  .Close = socket_close
};

// Create and initialize socket control block
socket_cb* initialize_socket_cb(){
	
	socket_cb* socketcb = (socket_cb*)xmalloc(sizeof(socket_cb));
	
	socketcb->refcount = 0;
	socketcb->fcb = NULL;
	socketcb->type = SOCKET_UNBOUND;
	socketcb->port = NOPORT;

	return socketcb;
}

// Create socket
Fid_t sys_Socket(port_t port)
{
	// Validate port
	if(port<0 || port>MAX_PORT) return NOFILE;

	Fid_t fid;
	FCB* fcb;

	socket_cb* socketcb = initialize_socket_cb();

	// Reserve FCB
	if(!FCB_reserve(1, &fid, &fcb)) return NOFILE;

	socketcb->fcb = fcb;

	// Configure FCB
	socketcb->fcb->streamfunc = &socket_file_ops;
	socketcb->fcb->streamobj = socketcb;

	socketcb->port = port;
	return fid;
}

// Set socket to listening state
int sys_Listen(Fid_t sock)
{
	// Validate fid
	if(sock<0 || sock>15) return -1;

	FCB* fcb = get_fcb(sock);
	
	// Check it's a socket
	if(fcb==NULL || fcb->streamfunc != &socket_file_ops) return -1;

	socket_cb* socketcb = (socket_cb*)fcb->streamobj;

	// Check preconditions
	if(socketcb->type != SOCKET_UNBOUND ||
		socketcb->port == NOPORT ||
			PORT_MAP[socketcb->port]!=NULL) return -1;
	
	// Register in port map
	PORT_MAP[socketcb->port] = socketcb;
	socketcb->type = SOCKET_LISTENER;

	// Initialize listener
	socketcb->listener_s.req_available = COND_INIT;
	rlnode_init(&socketcb->listener_s.queue, NULL);

	return 0;
}

// Connect two sockets with pipes
void connect_pipes(socket_cb* request_socket, socket_cb* new_socket){

	request_socket->peer_s.peer = request_socket;
	new_socket->peer_s.peer = new_socket;

	// Create two pipes
	pipe_cb* pipe_one = initialize_pipe_cb();
	pipe_cb* pipe_two = initialize_pipe_cb();

	// Connect pipes
	request_socket->peer_s.read_pipe = pipe_one;
	request_socket->peer_s.write_pipe = pipe_two;

	new_socket->peer_s.read_pipe = pipe_two;
	new_socket->peer_s.write_pipe = pipe_one;

	// Set FCB pointers in pipes
	pipe_one->reader = request_socket->fcb;
	pipe_one->writer = new_socket->fcb;

	pipe_two->reader = new_socket->fcb;
	pipe_two->writer = request_socket->fcb;

	// Change type to PEER
	request_socket->type = SOCKET_PEER;
	new_socket->type = SOCKET_PEER;
}

// Accept new connection
Fid_t sys_Accept(Fid_t lsock)
{
	// Validate
	if(lsock<0 || lsock > 15) {
		return NOFILE;
	}

	FCB* fcb = get_fcb(lsock);

	if(fcb==NULL || fcb->streamfunc != &socket_file_ops) {
		return NOFILE;
	}

	socket_cb* socketcb = (socket_cb*)fcb->streamobj;

	if(socketcb->type!=SOCKET_LISTENER) {
		return NOFILE;
  }
	
	socketcb->refcount++;  

	// Wait until request arrives
	while(is_rlist_empty(&socketcb->listener_s.queue) && socketcb->refcount==1){
		kernel_wait(&socketcb->listener_s.req_available, SCHED_USER);
	}

	// Check if closed while waiting
	if(socketcb->refcount==0){ 
		free(socketcb);
		return NOFILE;
	}

	// Get request from queue
	rlnode* queue_node = rlist_pop_front(&socketcb->listener_s.queue);
	connection_request* request = (connection_request*)queue_node->obj;
	socket_cb* request_socket = request->peer;
	
	// Create new socket
	Fid_t new_fid = sys_Socket(socketcb->port);
	if(new_fid==NOFILE){
		kernel_signal(&request->connected_cv);
		return NOFILE;
	}

	FCB* new_fcb = get_fcb(new_fid);
	socket_cb* new_socketcb = (socket_cb*)new_fcb->streamobj;

	// Connect the two sockets
	connect_pipes(request_socket, new_socketcb);

	// Notify client we connected
	request->admitted = 1;
	kernel_signal(&request->connected_cv);
	socketcb->refcount--;
	return new_fid;
}

// Create connection request
connection_request* initialize_request(socket_cb* socketcb){

	connection_request* request = (connection_request*)xmalloc(sizeof(connection_request));
	request->admitted=0;
	request->connected_cv = COND_INIT;
	request->peer=socketcb;
	rlnode_init(&request->queue_node, request);
	return request;
}

// Connect to listener socket
int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
	// Validate parameters
	if(sock<0 || sock > 15 ||
		port > MAX_PORT || port <= 0)  return -1;
	
	// Find listener socket
	socket_cb* lsocketcb = PORT_MAP[port];
	if(lsocketcb==NULL ||
		lsocketcb->type!=SOCKET_LISTENER)  return -1;

	FCB* fcb = get_fcb(sock);
	if(fcb==NULL || 
		fcb->streamfunc != &socket_file_ops) return -1;
		
	socket_cb* socketcb = (socket_cb*)fcb->streamobj;

	if(socketcb->type!=SOCKET_UNBOUND)
		return -1;

	socketcb->refcount++; 
	
	// Create request
	connection_request* request = initialize_request(socketcb);

	// Add to listener queue
	rlist_push_back(&lsocketcb->listener_s.queue, &request->queue_node);

	// Notify listener
	kernel_signal(&lsocketcb->listener_s.req_available);

	// Wait for admission with timeout
	kernel_timedwait(&request->connected_cv, SCHED_USER, timeout);

	int retVal = request->admitted;
	free(request);
	
	// Check if closed while waiting
	if(socketcb->refcount==0){
		free(socketcb);
		return retVal-1;
	}else if(socketcb->refcount==1){
		socketcb->refcount--;
		return retVal-1;
	}
   return 0;
}

// Close socket end
int sys_ShutDown(Fid_t sock, shutdown_mode how)
{
	// Validate
	if(sock<0 || sock>15) return -1;

	FCB* fcb = get_fcb(sock);
	if(fcb==NULL ||
		fcb->streamfunc != &socket_file_ops) 
		   return -1;

	socket_cb* socketcb = (socket_cb*)fcb->streamobj;
	if(socketcb->type!=SOCKET_PEER) 
		return -1;

	switch(how)
		{
		case SHUTDOWN_READ:	
			if(socketcb->peer_s.read_pipe != NULL){
				pipe_reader_close(socketcb->peer_s.read_pipe);
				socketcb->peer_s.read_pipe = NULL;
			}
				return 0;	
			break;
		case SHUTDOWN_WRITE: 
			if(socketcb->peer_s.write_pipe != NULL){
				pipe_writer_close(socketcb->peer_s.write_pipe);
				socketcb->peer_s.write_pipe = NULL;
			}
				return 0;
			break;
	    case SHUTDOWN_BOTH:
			if(socketcb->peer_s.read_pipe != NULL){
				pipe_reader_close(socketcb->peer_s.read_pipe);
				socketcb->peer_s.read_pipe = NULL;
			}
			if(socketcb->peer_s.write_pipe != NULL){
				pipe_writer_close(socketcb->peer_s.write_pipe);
				socketcb->peer_s.write_pipe = NULL;
			}
				return 0;
			 break;
		default:
			return -1;
			break;

	}
return 0;

}