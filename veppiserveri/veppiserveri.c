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

#define DEFAULT_SERVER_PORT 80
#define INPUT_BUFFER 8180
#define LISTEN_BACKLOG 21	/* Maximum number of pending users. */
#define NOBODY_UID 99		/* Nobody on my system, check yours! */
#define PARSE_BUFFER (INPUT_BUFFER + 12)
#define TMP_LOG 120
#define TMP_SIZE 120

static int connected_socket;
static int server_socket;

void http_error(const int error_code);
void http_server(void);
int open_server_port(unsigned int port);
int parse_port(const int argc, char *argv[]);
void send_file(const char *name);
void signal_handler(const int sig);
void write_socket(const char *buffer);

int
main(int argc, char *argv[])
{
	int port;

	port = parse_port(argc, argv);
	if (port == -1)
		return EXIT_FAILURE;

	server_socket = open_server_port(port);
	if (server_socket == -1)
		return EXIT_FAILURE;

	setuid(NOBODY_UID);

	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);

	fprintf(stderr, "%s: server started.\n", argv[0]);

	while (server_socket != -1) {
		struct sockaddr_in client_addr;
		unsigned int sin_size = sizeof (struct sockaddr_in);
#ifdef LOG
		struct hostent *host;
		char s[TMP_LOG];
		time_t tp;
#endif				/* LOG */

		connected_socket =
		    accept(server_socket, (struct sockaddr *) &client_addr,
			   &sin_size);
		if (connected_socket == -1) {
			if (server_socket != -1)
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
		strftime(s, (size_t) TMP_LOG,
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

	fprintf(stderr, "%s: server stopped.\n", argv[0]);

	return EXIT_SUCCESS;
}

void
http_error(const int error_code)
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
	char file[PARSE_BUFFER] = { 0 };
	char input[INPUT_BUFFER] = { 0 };
	int bytes, first_char, i;

	bytes = read(connected_socket, input, INPUT_BUFFER);
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
			if (first_char == INPUT_BUFFER)
				return;
		}
		for (i = first_char; i < INPUT_BUFFER; i++) {
			if (input[i] == '.' && input[i + 1] == '.' &&
			    ((input[i + 2] == '/' || input[i + 2] == ' ')
			     || (input[i] == ':')))
				continue;
			if (input[i] != ' ' && input[i] != '\n'
			    && input[i] != '\r')
				file[i - first_char] = input[i];
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

	if (((chdir(file) == 0) || (strlen(file) == 0)))
		strcpy(file, "index.html");

	send_file(file);
}

int
open_server_port(unsigned int port)
{
	static struct sockaddr_in sin;
	int one = 1, sd;

	if (port > 65535)
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
parse_port(const int argc, char *argv[])
{
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
			return DEFAULT_SERVER_PORT;

		if (argc == 3) {
			unsigned int port = atoi(argv[2]);

			if (port > 65535) {
				fprintf(stderr, "Max. port is 65535.\n");
				return -1;
			}
			return port;
		}

	} else
		fprintf(stderr, "Usage: %s absolute-path-to-www-root [port]\n",
			argv[0]);
	return -1;
}

void
send_file(const char *name)
{
	char tmp[TMP_SIZE] = { 0 };
	FILE *fp = NULL;
	char *buffer = NULL;
	ssize_t size;

#ifdef LOG
	printf("%s HTTP/1.0\" ", name);
#endif				/* LOG */

	fp = fopen(name, "r");
	if (fp == NULL) {
		http_error(404);
		return;
	}

	if (fseek(fp, 0, SEEK_END) == -1)
		perror("fseek");

	size = ftell(fp);
	if (size == -1)
		perror("ftell");

	rewind(fp);

	buffer = malloc(size);	/* Served files have to fit into memory. */
	if (buffer == NULL) {
		perror("malloc");
		if (fclose(fp) == EOF)
			perror("fclose");
		fp = NULL;
		http_error(500);
		return;
	}

	write_socket("HTTP/1.0 200 OK\nServer: veppiserveri\nContent-type: ");

	if (strstr(name, ".html"))
		write_socket("text/html\n");
	else if (strstr(name, ".jpeg"))
		write_socket("image/jpeg\n");
	else if (strstr(name, ".png"))
		write_socket("image/png\n");
	else
		write_socket("text/plain\n");

	if (snprintf(tmp, TMP_SIZE,
		     "Content-length: %d\nConnection: close\n\n", size) > -1)
		write_socket(tmp);
	else
		perror("snprintf");

	fread(buffer, size, 1, fp);
	if (ferror(fp) != 0) {
		perror("fread");
		clearerr(fp);
	}

	if (write(connected_socket, buffer, size) != size)
		perror("write");

	free(buffer);

	if (fclose(fp) == EOF)
		perror("fclose");

#ifdef LOG
	printf("200 %d \"-\" \"-\"\n", size);
#endif				/* LOG */
}

void
signal_handler(const int sig)
{
	if (close(server_socket) == 0)
		server_socket = -1;
	else
		perror("close");
}

void
write_socket(const char *buffer)
{
	size_t length = strlen(buffer);

	if (write(connected_socket, buffer, length) != length)
		perror("write");
}
