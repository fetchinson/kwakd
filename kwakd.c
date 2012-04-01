/*
 * A web server that serves a blank html page for any request
 *
 * By Daniel Fetchinson <fetchinson@gmail.com> 2007
 * http://code.google.com/p/kwakd/
 *
 * A stripped down version of
 *
 * cheetah
 *
 * Copyright (C) 2003 Luke Reeves (luke@neuro-tech.net)
 * http://www.neuro-tech.net/
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

#include "config.h"

#include <sys/wait.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>

#define INFO    0
#define WARNING 1
#define PANIC   2

#define SAFESEND( fd, msg ) \
    if( send( fd, msg, strlen( msg ), 0 ) == -1 ) { logmessage( WARNING, "Error sending data to client." ); return; }

/* globals */
int verbose = 0;        /* verbose output to stdout */
int quiet = 0;          /* suppress any output */
int background = 0;     /* go to background */
int sockfd = -1;

static void help( void )
{
    printf( "Usage: kwakd [OPTIONS]\n\n" );
    printf( "  Serve a blank html page for any request\n\n" );
    printf( "  -b, --background     background mode (disables console output, and allows\n" );
    printf( "                       multiple requests to be served simultaneously)\n" );
    printf( "  -p, --port           port to listen for requests on, defaults to 8000\n" );
    printf( "  -v, --verbose        verbose output\n" );
    printf( "  -q, --quiet          suppress any output\n" );
    printf( "  -V, --version        print version and exit\n" );
    printf( "  -h, --help           display this message and exit\n" );
}

/* prototypes */
static void handle_connection( int fd );
static void handle_request( int fd );
static void logmessage( int level, char *message );
static void sigcatch( int signal );

int main( int argc, char *argv[] )
{
    int port = 8000;
    struct sockaddr_in my_addr;
    struct sockaddr_in remote_addr;
    int sin_size;
    int newfd;
    int i, fr, rv;

    /* Parse options */
    for( i = 1; i < argc; i++ )
    {
	if( strcmp( argv[i], "-V" ) == 0 )
	{
	    printf( "This is kwakd %s.\n", VERSION );
	    exit( 0 );
	}
	else if( ( strcmp( argv[i], "-h" ) == 0 ) || ( strcmp( argv[i], "--help" ) == 0 ) )
	{
	    help(  );
	    exit( 0 );
	}
	else if( ( strcmp( argv[i], "-p" ) == 0 ) || ( strcmp( argv[i], "--port" ) == 0 ) )
	{
	    port = atoi( argv[i + 1] );
	    i++;
	}
	else if( ( strcmp( argv[i], "-v" ) == 0 ) || ( strcmp( argv[i], "--verbose" ) == 0 ) )
	{
	    verbose++;
	}
	else if( ( strcmp( argv[i], "-q" ) == 0 ) || ( strcmp( argv[i], "--quiet" ) == 0 ) )
	{
	    quiet = 1;
	}
	else if( ( strcmp( argv[i], "-b" ) == 0 ) || ( strcmp( argv[i], "--background" ) == 0 ) )
	{
	    background = 1;
	}
    }

    /* fork if necessary */
    if( background )
    {
	verbose = 0;
	rv = fork(  );
	if( rv == -1 )
	{
	    logmessage( PANIC, "Error forking." );
	}
	else if( rv > 0 )
	{
	    /* Exit if this is the parent */
	    _exit( 0 );
	}
	if( setsid(  ) == -1 )
	    logmessage( PANIC, "Couldn't create SID session." );
	if( signal( SIGCHLD, SIG_IGN ) == SIG_ERR )
	{
	    logmessage( PANIC, "Couldn't initialize signal handlers." );
	}
	if( ( close( 0 ) == -1 ) || ( close( 1 ) == -1 ) || ( close( 2 ) == -1 ) )
	{
	    logmessage( PANIC, "Couldn't close streams." );
	}
    }

    /* Trap signals */
    if( ( signal( SIGTERM, sigcatch ) == SIG_ERR ) || ( signal( SIGINT, sigcatch ) == SIG_ERR ) )
    {
	logmessage( PANIC, "Couldn't setup signal traps." );
    }

    sockfd = socket( AF_INET, SOCK_STREAM, 0 );
    if( sockfd == -1 )
	logmessage( PANIC, "Couldn't create socket." );

    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons( port );
    my_addr.sin_addr.s_addr = INADDR_ANY;
    bzero( &( my_addr.sin_zero ), 8 );

    if( bind( sockfd, ( struct sockaddr * ) &my_addr, sizeof( struct sockaddr ) ) == -1 )
	logmessage( PANIC, "Couldn't bind to specified port." );

    sin_size = sizeof( struct sockaddr_in );

    if( listen( sockfd, 25 ) == -1 )
	logmessage( PANIC, "Couldn't listen on specified port." );

    if( verbose )
	printf( "Listening for connections on port %d...\n", port );

    while( 1 )
    {
	newfd = accept( sockfd, ( struct sockaddr * ) &remote_addr, &sin_size );
	if( newfd == -1 )
	    logmessage( PANIC, "Couldn't accept connection!" );

        logmessage( INFO, "Connected, handling requests." );

	if( background )
	{
	    fr = fork(  );
	    if( fr != 0 )
		continue;
	    handle_connection( newfd );
	    _exit( 0 );
	}
	handle_connection( newfd );
    }
}

static void handle_connection( int fd )
{
    handle_request( fd );

    /* Shutdown socket */
    if( shutdown( fd, SHUT_RDWR ) == -1 )
    {
	logmessage( WARNING, "Error shutting down client socket." );
	return;
    }

    if( close( fd ) == -1 )
	logmessage( WARNING, "Error closing client socket." );
}

static void handle_request( int fd )
{
    int rv;
    char inbuffer[2048];

    rv = recv( fd, inbuffer, sizeof( inbuffer ), 0 );
    if( rv == -1 )
    {
	logmessage( WARNING, "Error receiving request from client." );
	return;
    }

    SAFESEND( fd, "HTTP/1.1 200 OK\r\n" );
    SAFESEND( fd, "Content-Type: text/html\r\n" );
    SAFESEND( fd, "Last-Modified: Sat, 08 Jan 1492 01:12:12 GMT\r\n" );
    SAFESEND( fd, "Content-Length: 15\r\n\r\n" );
    SAFESEND( fd, "<html> </html>\r\n" );
}

static void logmessage( int level, char *message )
{
    switch( level )
    {
        case INFO:
            if( verbose )
                printf( "[info] %s\n", message );
            break;
        case WARNING:
            if( !quiet )
                fprintf( stderr, "[warning] %s\n", message );
            break;
        case PANIC:
            if( !quiet )
                fprintf( stderr, "[panic] %s\n", message );
            exit( 1 );
            break;
    }
}

static void sigcatch( int signal )
{
    if( verbose )
	printf( "Signal caught, exiting.\n" );
    if( sockfd != -1 )
    {
	if( close( sockfd ) == -1 )
            logmessage( WARNING, "Error closing socket." );
	exit( 0 );
    }
}
