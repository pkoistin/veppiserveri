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
#include <unistd.h>

#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#undef DEBUG
#define LOG

#define BUFFER_SIZE 8192
#define LISTEN_BACKLOG	21
#define LOGBUFFER 9000

static int server_socket;
static int the_socket;

void signal_hander(int sig);
int open_server_port(unsigned short port);
void http_server(void);
void http_error(int error_code);
void write_socket(const char *buffer);

int
main(int argc, char *argv[])
{
#ifdef LOG
	struct hostent *host;
#endif				/* LOG */
	struct sockaddr_in client_addr;
	unsigned int sin_size = sizeof (struct sockaddr_in);
	int port = 80;		/* Default HTTP service port */

	if (argc > 1) {
		chdir("/");
		if (chdir(argv[1])) {
			fprintf(stderr, "invalid www-root-directory, "
				"must be absolute path.\n");
			exit(1);
		}
		if (argc == 3) {
			port = atoi(argv[2]);
			if (port == 0) {
				fprintf(stderr, "invalid port number.\n");
				exit(1);
			}
		}
	} else {
		fprintf(stderr, "usage: %s www-root-directory [port]\n",
			argv[0]);
		exit(1);
	}

	signal(SIGTERM, signal_hander);
	signal(SIGINT, signal_hander);

	the_socket = open_server_port(port);

	setuid(99);		// Nobody on my Red Hat Rawhide Linux system.

	while (the_socket != 0) {
#ifdef LOG
		char s[LOGBUFFER];
		time_t tp;
#endif				/* LOG */
		if ((server_socket = accept(the_socket, (struct sockaddr *)
					    &client_addr, &sin_size)) == -1) {
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
			 " - - [%d/%b/%Y:%X +%Z] \"GET /", localtime(&tp));
		printf("%s", s);
#endif				/* LOG */

		chdir(argv[1]);
		http_server();
		close(server_socket);
	}

	fprintf(stderr, "server closed.\n");
	return 0;
}

void
signal_hander(int sig)
{
	fprintf(stderr, "shutting down server...\n");
	close(the_socket);
	the_socket = 0;
}

int
open_server_port(unsigned short port)
{
	static struct sockaddr_in sin;
	int err, one, tmp_socket;

	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);

	tmp_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (tmp_socket < 0) {
		perror("socket");
		exit(1);
	}

	one = 1;
	if (setsockopt(tmp_socket, SOL_SOCKET, SO_REUSEADDR, (char *) &one,
		       sizeof (int)) == -1) {
		fprintf(stderr, "Error in setsockopt REUSEADDR");
	}

	err = bind(tmp_socket, (struct sockaddr *) &sin, sizeof (sin));
	if (err) {
		close(tmp_socket);
		perror("bind");
		exit(1);
	}

	err = listen(tmp_socket, LISTEN_BACKLOG);
	if (err) {
		close(tmp_socket);
		perror("listen");
		exit(1);
	}

	return tmp_socket;
}

void
http_server(void)
{
	char parsed_name[BUFFER_SIZE + 12] = { 0 };
	char input[BUFFER_SIZE] = { 0 };
	char *temp_memory = NULL;
	FILE *fp;
	int data_got, first_char, file_size, i;

	data_got = read(server_socket, input, BUFFER_SIZE);
	if (data_got <= 0) {
		fprintf(stderr, "send problem.\n");
		return;
	}
#ifdef DEBUG
	fprintf(stderr, "input: \"%s\".\n", input);
#endif				/* DEBUG */

	/* Parse request. */
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
#endif				/* DEBUG */

	fp = fopen(parsed_name, "r");
	if (fp == NULL) {
		http_error(404);
		return;
	}
	fseek(fp, 0, SEEK_END);
	file_size = ftell(fp);
	rewind(fp);

	if (file_size > BUFFER_SIZE) {	/* malloc is expensive ;) */
		temp_memory = malloc(file_size);
		if (temp_memory == NULL) {
			fprintf(stderr, "unable reserve tempoary memory, "
				"memory requested: %d bytes.\n", file_size);
			fclose(fp);
			http_error(500);
			return;
		}
	} else {
		temp_memory = parsed_name;	/* Used as buffer */
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
	sprintf(parsed_name, "Content-length: %d\nConnection: close\n\n",
		file_size);
	write_socket(parsed_name);

	fread(temp_memory, file_size, 1, fp);
	data_got = write(server_socket, temp_memory, file_size);
	if (data_got < 0)
		fprintf(stderr, "write problem.\n");

	if (file_size > BUFFER_SIZE)
		free(temp_memory);

	fclose(fp);

#ifdef LOG
	printf("200 %d \"-\" \"-\"\n", file_size);
#endif				/* LOG */
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

#ifdef LOG
	printf("%d 1 \"-\" \"-\"\n", error_code);
#endif				/* LOG */

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
		fprintf(stderr, "Not defined error. error_code: %d\n",
			error_code);
		break;
	}
}

void
write_socket(const char *buffer)
{
	int code, length;

	length = strlen(buffer);
	code = write(server_socket, buffer, length);

	if (code < 0)
		fprintf(stderr, "write problem.\n");
}
