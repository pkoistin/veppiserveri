/* veppiserveri: Really tiny HTTP server.

 * Quick & dirty code by Petri Koistinen (with stolen ideas. ;-)
 * Public Domain.
 *
 * Feel free to send me comments: thoron(at)iki.fi
 *
 * URL: http://veppiserveri.sourceforge.net/
 * $Id$
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#undef DEBUG
#define LOG

#define BUFFER_SIZE		8192
#define DEFAULT_SERVER_PORT	80
#define LISTEN_BACKLOG		21	/* Maximum number of pending users. */
#define LOGBUFFER		120
#define NOBODY_UID		99	/* Nobody on my system, check yours! */

static int connected_socket;
static int server_socket;

void http_error(int error_code);
void http_server(void);
int open_server_port(unsigned int port);
int parse_port(int argc, char *argv[]);
void signal_handler(int sig);
void write_socket(const char *buffer);

int
main(int argc, char *argv[])
{
	server_socket = open_server_port(parse_port(argc, argv));

	setuid(NOBODY_UID);

	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);

	while (server_socket != -1) {
		struct sockaddr_in client_addr;
		unsigned int sin_size = sizeof (struct sockaddr_in);
#ifdef LOG
		struct hostent *host;
		char s[LOGBUFFER];
		time_t tp;
#endif				/* LOG */

		connected_socket =
		    accept(server_socket, (struct sockaddr *) &client_addr,
			   &sin_size);
		if (connected_socket == -1) {
			perror("accept");
			continue;
		}
#ifdef LOG
		time(&tp);
		host = gethostbyaddr((char *) &(client_addr.sin_addr),
				     sizeof (struct in_addr), AF_INET);
		if (host != NULL) {
			printf("%s", host->h_name);
		} else {
			printf("%s", inet_ntoa(client_addr.sin_addr));
		}
		strftime(s, (size_t) LOGBUFFER,
			 " - - [%d/%b/%Y:%X +0000] \"GET /", gmtime(&tp));
		printf("%s", s);
#endif				/* LOG */

		if (chdir(argv[1]) == 0)
			http_server();
		else
			perror("chdir");

		if (close(connected_socket) == -1)
			perror("close");
	}
	return EXIT_SUCCESS;
}

void
http_error(int error_code)
{
	const char error_header[] =
	    "Server: veppiserveri\nContent-type: text/plain\n"
	    "Connection: close\n\n\n";
	const char not_found[] = "HTTP/1.0 404 Not found\n";
	const char internal_server_error[] =
	    "HTTP/1.0 500 Internal Server Error\n";
	const char not_implemented[] = "HTTP/1.0 501 Not implemented\n";

	switch (error_code) {
	case 404:
		write_socket(not_found);
		write_socket(error_header);
		write_socket(not_found);
		break;

	case 500:
		write_socket(internal_server_error);
		write_socket(error_header);
		write_socket(internal_server_error);
		break;

	case 501:
		write_socket(not_implemented);
		write_socket(error_header);
		write_socket(not_implemented);
		break;

	default:
		perror("unknown error");
		fprintf(stderr, "unknown error_code: %d\n", error_code);
		break;
	}

#ifdef LOG
	printf("%d 1 \"-\" \"-\"\n", error_code);
#endif				/* LOG */
}

void
http_server(void)
{
	char parsed_name[BUFFER_SIZE + 12] = { 0 };
	char input[BUFFER_SIZE] = { 0 };
	char *tmp_memory = NULL;
	FILE *fp;
	int bytes, first_char, file_size, i;

	bytes = read(connected_socket, input, BUFFER_SIZE);
	if (bytes == -1 || bytes == 0) {
		perror("read");
		return;
	}
#ifdef DEBUG
	fprintf(stderr, "input: \"%s\".\n", input);
#endif				/* DEBUG */

	/* Parse (malicious) request. */
	first_char = 4;
	if (input[0] == 'G' && input[1] == 'E' && input[2] == 'T') {
		while (input[first_char] == '/' || input[first_char] == '.'
		       || input[first_char] == ':') {
			first_char++;
			if (first_char == BUFFER_SIZE)
				return;
		}
		for (i = first_char; i < BUFFER_SIZE; i++) {
			if (input[i] == '.' && input[i + 1] == '.' &&
			    ((input[i + 2] == '/' || input[i + 2] == ' ')
			     || (input[i] == ':')))
				continue;
			if (input[i] != ' ' && input[i] != '\n'
			    && input[i] != '\r')
				parsed_name[i - first_char] = input[i];
			else
				break;
		}
	} else {
#ifdef LOG
		printf("\b\b\b\b\b(illegal request)\" ");
#endif				/* LOG */
		http_error(501);
		return;
	}

	if (((chdir(parsed_name) == 0) || (strlen(parsed_name) == 0)))
		strcpy(parsed_name, "index.html");

#ifdef LOG
	printf("%s HTTP/1.0\" ", parsed_name);
#endif				/* LOG */

	fp = fopen(parsed_name, "r");
	if (fp == NULL) {
		http_error(404);
		return;
	}

	if (fseek(fp, 0, SEEK_END) == -1)
		perror("fseek");

	file_size = ftell(fp);
	if (file_size == -1)
		perror("ftell");

	rewind(fp);

	if (file_size > BUFFER_SIZE) {	/* malloc is expensive ;) */
		tmp_memory = malloc(file_size);
		if (tmp_memory == NULL) {
			perror("malloc");
			if (fclose(fp) == EOF)
				perror("fclose");
			fp = NULL;
			http_error(500);
			return;
		}
	} else {
		tmp_memory = parsed_name;
	}

	write_socket("HTTP/1.0 200 OK\nServer: veppiserveri\nContent-type: ");

	if (strstr(parsed_name, ".html") != NULL)
		write_socket("text/html\n");

	else if (strstr(parsed_name, ".gif") != NULL)
		write_socket("image/gif\n");

	else if (strstr(parsed_name, ".jp") != NULL)
		write_socket("image/jpeg\n");

	else if (strstr(parsed_name, ".png") != NULL)
		write_socket("image/png\n");

	else
		write_socket("text/plain\n");

	sprintf(input, "Content-length: %d\nConnection: close\n\n", file_size);
	write_socket(input);

	fread(tmp_memory, file_size, 1, fp);
	if (ferror(fp) != 0) {
		perror("fread");
		clearerr(fp);
	}

	bytes = write(connected_socket, tmp_memory, file_size);
	if (bytes == -1)
		perror("write");

	if (file_size > BUFFER_SIZE)
		free(tmp_memory);

	if (fclose(fp) == EOF)
		perror("fclose");

#ifdef LOG
	printf("200 %d \"-\" \"-\"\n", file_size);
#endif				/* LOG */
}

int
open_server_port(unsigned int port)
{
	static struct sockaddr_in sin;
	int one = 1, sd;

	if (port < 1 || port > 65535)
		return -1;

	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);

	sd = socket(AF_INET, SOCK_STREAM, 0);
	if (sd == -1)
		perror("socket");

	if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, (char *) &one,
		       sizeof (int)) == -1)
		perror("setsockopt");

	if (bind(sd, (struct sockaddr *) &sin, sizeof (sin)) == -1) {
		perror("bind");
		if (close(sd) == -1)
			perror("close");
		return -1;
	}

	if (listen(sd, LISTEN_BACKLOG) == -1) {
		perror("listen");
		if (close(sd) == -1)
			perror("close");
		return -1;
	}

	return sd;
}

int
parse_port(int argc, char *argv[])
{
	int port = -1;

	if (argc > 1) {
		if (argv[1][0] != '/') {
			fprintf(stderr, "Given path is not absolute.\n");
			return -1;
		}

		if (chdir("/") == -1 || chdir(argv[1]) == -1) {
			perror("chdir");
			return -1;
		}

		if (argc == 2)
			port = DEFAULT_SERVER_PORT;

		if (argc == 3)
			port = atoi(argv[2]);

		if (port < 1 || port > 65535) {
			fprintf(stderr, "Port is between 1 and 65535.\n");
			return -1;
		}
	} else {
		fprintf(stderr,
			"Usage: %s absolute-path-to-www-root [port]\n",
			argv[0]);
	}

	return port;
}

void
signal_handler(int sig)
{
	if (close(server_socket) == 0)
		server_socket = -1;
	else
		perror("close");
}

void
write_socket(const char *buffer)
{
	if (write(connected_socket, buffer, strlen(buffer)) == -1)
		perror("write");
}
