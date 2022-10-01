#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <pthread.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>


void parseArgs(int, char *[]);
void free_ssl_serverfd_ctx(SSL*, int , SSL_CTX*);
void* downloadPart(void *);
int mergeParts(int, const char *);
void parse_full_url(char[]);
int create_and_connect_socket(char[], BIO*);
char* get_parsed_request_message(char [], char*, char*);
char* get_range_request_message(char [], char*, char*, int, int);
int get_content_length(SSL*, char[]);

typedef struct {
    char *url;
    int start;
    int end;
    char f_part_name[10];
    SSL *ssl;
    int serverfd;
    SSL_CTX* ctx;
    pthread_t thread_id;
} download_part_struct;

int num_parts;
char *https_url;
char *output_file;
char hostname[256] = {0}; // e.g. cobweb.cs.uga.edu
char res_path[256] = {0};
int port;
char* tmp_ptr;

int main(int argc, char *argv[])
{
    // 1. Takes command line arguments
    parseArgs(argc, argv);
    parse_full_url(https_url); // obtain hostname and resource path from url

    // 2. Opens [num_parts] parallel TCP+TLS connections to [https_url] on port 443 and retrieves file in [num_parts] parts
    
    // Load algorithms and strings needed by OpenSSL
    OpenSSL_add_all_algorithms();
    ERR_load_BIO_strings();
    ERR_load_crypto_strings();
    SSL_load_error_strings();

    // Create the Input/Output BIOs
    BIO* outbio = BIO_new_fp(stdout, BIO_NOCLOSE);

    // Initialize OpenSSL.
    if (SSL_library_init() < 0) BIO_printf(outbio, "Could not initialize the OpenSSL library.\n");
    const SSL_METHOD* method = SSLv23_client_method();

    // Create an SSL context.
    SSL_CTX* ctx = SSL_CTX_new(method);
    if (!ctx) BIO_printf(outbio, "Unable to create SSL context.\n");
    SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2);

    // Create a new SSL session. This does not connect the socket.
    SSL* ssl = SSL_new(ctx);

    // Create a socket and connect it to the server over TCP.
    int serverfd = create_and_connect_socket(https_url, outbio);
    if (serverfd)
        BIO_printf(outbio,
                   "Successfully made the TCP connection to: %s.\n",
                   https_url);

    // Attach the SSL session to the socket file descriptor
    SSL_set_fd(ssl, serverfd);

    // Connect to the server over SSL
    if (SSL_connect(ssl) < 1) {
        BIO_printf(outbio,
                   "Error: Could not build an SSL session to: %s.\n",
                   https_url);
    } else {
        BIO_printf(outbio,
                   "Successfully enabled SSL/TLS session to: %s.\n\n",
                   https_url);
    }

    /*
     Send HTTP/1.1 requests and get responses
    */
    char message[2048];
    get_parsed_request_message(message, hostname, res_path);
    if (SSL_write(ssl, message, strlen(message)) < 0)
        perror("ERROR writing to socket.");

    // get file content_length without getting the actual file
    int content_length = get_content_length(ssl, message);
    // calculate part sizes -> almost equal length
    int part_size = content_length / num_parts;
    int remainder_bytes = content_length % num_parts;
    int last_part_size = part_size + remainder_bytes;
    int prev = 0, count = 0;

    download_part_struct thread_args[num_parts];

    // open num_parts TCP+TLS connections to https_url
    // and retrieve file in approximately equal length
    for (int i = part_size; i <= content_length; i = i + part_size)
    {
        // last part end takes all the remaining bytes
        if ((count + 1) == num_parts) i = prev + last_part_size; 
        
        download_part_struct args;

        char file_name[10];
        sprintf(file_name, "part_%d", (count+1)); // format file name 
        // BIO* outbio_n = BIO_new_fp(stdout, BIO_NOCLOSE);
        // const SSL_METHOD* method_n = SSLv23_client_method();
        SSL_CTX* ctx_n = SSL_CTX_new(method);
        SSL_CTX_set_options(ctx_n, SSL_OP_NO_SSLv2);
        SSL* ssl_n = SSL_new(ctx_n);
        int serverfd_n = create_and_connect_socket(https_url, outbio);
        SSL_set_fd(ssl_n, serverfd_n);
        if (SSL_connect(ssl_n) < 1) printf("Failed to connect to ssl_n!\n");

        strcpy(args.f_part_name, file_name);
        args.url = https_url;
        args.start = prev;
        args.end = (i - 1);
        args.ssl = ssl_n;
        args.serverfd = serverfd_n;
        args.ctx = ctx_n;
        thread_args[count] = args;
        pthread_create(&thread_args[count].thread_id, NULL, downloadPart, &thread_args[count]);

        ++count;
        prev = i;
    }
    // set up threads to download file parts in parallel
    // for (int i = 0; i < num_parts; ++i){
    //     pthread_create(&thread_args[i].thread_id, NULL, downloadPart, &thread_args[i]);
    // }
    for (int i = 0; i < num_parts; ++i){
        pthread_join(thread_args[i].thread_id, NULL);
    }
    //////////////////////////////////////////////////////////////////
    // Close connections and clean up
    SSL_free(ssl);
    close(serverfd);
    SSL_CTX_free(ctx);
    BIO_printf(outbio, "\nFinished SSL/TLS connection with server: %s.\n", https_url);

    // 3. Puts the [num_parts] parts together and writes to output file
    mergeParts(num_parts, output_file);

    return 0;
}

// parse command line arguments and store them in global variables
void parseArgs(int argc, char *argv[])
{
    int opt;

    while ((opt = getopt(argc, argv, ":u:n:o:")) != -1)
    {
        switch (opt)
        {
        case 'u':
            https_url = optarg;
            break;
        case 'n':
            num_parts = atoi(optarg);
            break;
        case 'o':
            output_file = optarg;
            break;
        }
    }
}

// function to close TCP+TLS connections
void free_ssl_serverfd_ctx(SSL *ssl, int serverfd, SSL_CTX *ctx){
    SSL_free(ssl);
    close(serverfd);
    SSL_CTX_free(ctx);
};

// function to handle downloading within a single thread
void* downloadPart(void* args)
{
    download_part_struct *actual_args = args;
    int start = actual_args->start;
    int end = actual_args->end;
    char *f_part_name = actual_args->f_part_name;
    SSL* ssl = actual_args->ssl;
    
    // Properly format the https request
    int n = 0;
    char message[2048];
    get_range_request_message(message, hostname, res_path, start, end);
    // Send request
    if ((n=SSL_write(ssl, message, strlen(message))) < 0)
        perror("ERROR writing to socket.");
    // printf("\nSent Headers:\n\n'''\n%s'''\n\n\n", message);

    // Get response headers
    // int bytes_recv = SSL_read(ssl, message, 2048);
    // printf("\nResponse (%d bytes): \n'''\n%.*s'''\n\n", bytes_recv, bytes_recv, message);
    SSL_read(ssl, message, 2048);
    
    // Get actual part content
    char buffer[4096] = {0};
    int bytes = 0;
    FILE *fp = fopen(f_part_name, "wb"); 
    for (;;) {
        if ((bytes = SSL_read(ssl, buffer, sizeof(buffer))) < 0) {
            perror("ERROR reading from socket.");
            return NULL;
        }
        if (!bytes) break;
        fwrite(buffer, 1, bytes, fp);
    }
    fclose(fp);

    printf("[-]%s Downloaded!\n", f_part_name);
    // close connection once download is complete
    free_ssl_serverfd_ctx(actual_args->ssl, actual_args->serverfd, actual_args->ctx);
    // free memory allocated
    // free(actual_args);
    return NULL;
}

// function to handle merging logic of downloaded parts
int mergeParts(int num_parts, const char *output_file)
{
    FILE *out_ptr = fopen(output_file, "wb");
    if (out_ptr == NULL)
        return 1;
    char buffer[4097];

    for (int i = 1; i < (num_parts + 1); ++i)
    {
        char file_name[10];
        sprintf(file_name, "part_%d", i); // convert index to string 
        const char *f_part_name = file_name;
        FILE *part_ptr = fopen(f_part_name, "rb");
        if (part_ptr == NULL)
            return 1;

        int n;
        while ((n = fread(buffer, sizeof(char), 4096, part_ptr)))
        {
            int k = fwrite(buffer, sizeof(char), n, out_ptr);

            if (!k)
                return 1;
        }
        fclose(part_ptr);
    }
    fclose(out_ptr);
    return 0;
}

// parse the given https url and store hostname, resource path and port
// in global variables
void parse_full_url(char url_str[1]){
    char tmp_hostname[256] = {0};
    char portnum[6] = "443";
    char proto[6]={0};

    // Remove trailing slash '/' from the URL, if any.
    tmp_ptr = url_str + strlen(url_str);
    if (*tmp_ptr == '/') *tmp_ptr=0;

    // Extract the protocol string from the URL - the substring up to the first
    // colon ':'.
    strncpy(proto, url_str, (strchr(url_str, ':') - url_str));

    // Extract the hostname from the URL - the substring after the first "://"
    strncpy(tmp_hostname, strstr(url_str, "://") + 3, sizeof(tmp_hostname));

    // Extract the resource_path from the URL - the substring after the second "/"
    strncpy(res_path, strstr(tmp_hostname, "/"), sizeof(res_path));

    // Extract valid host from the hostname - the substring up to the first '/'
    strncpy(hostname, tmp_hostname, (strchr(tmp_hostname, '/') - tmp_hostname));

    // Extract the port number from the hostname, if any.
    tmp_ptr = strchr(hostname, ':');
    if (tmp_ptr) {
        strncpy(portnum, tmp_ptr + 1, sizeof(portnum));
        *tmp_ptr=0;
    }

    port = atoi(portnum);
}

// Create a socket and connect it to the server over TCP.
int create_and_connect_socket(char url_str[1], BIO* out) {
    struct hostent* host = gethostbyname(hostname);
    if (!host) {
        BIO_printf(out, "Error: Cannot resolve hostname %s.\n", hostname);
        exit(EXIT_FAILURE);
    }

    // Create a socket capable of TCP communication.
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    // Create the sockaddr_in which contains the address of the server we want
    // to connect the socket to.
    struct sockaddr_in dest_addr={0};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    dest_addr.sin_addr.s_addr = *(long*)(host->h_addr);

    // Convert the binary address (in network byte order) stored in the struct
    // sin_addr into numbers-and-dots notation, and store it in tmp_ptr.
    tmp_ptr = inet_ntoa(dest_addr.sin_addr);

    // Connect the socket to the server.
    if (connect(sockfd,
                (struct sockaddr*) &dest_addr,
                sizeof(struct sockaddr)) < 0) {
        BIO_printf(out, "Error: Cannot connect to host %s [%s] on port %d.\n",
                   hostname, tmp_ptr, port);
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

// Format a HTTP/1.1 request message without Range option
char* get_parsed_request_message(char message[], char* hostname, char* res_path)
{
    sprintf(message, "GET %s HTTP/1.1\r\n", res_path);
    sprintf(message + strlen(message), "Host: %s\r\n", hostname);
    sprintf(message + strlen(message), "Connection: close\r\n");
    sprintf(message + strlen(message), "User-Agent: https_simple\r\n");
    sprintf(message + strlen(message), "\r\n");
    return message;
}

// Format a HTTP/1.1 request message to include the Range
char* get_range_request_message(char message[], char* hostname, char* res_path, int start, int end)
{
    sprintf(message, "GET %s HTTP/1.1\r\n", res_path);
    sprintf(message + strlen(message), "Host: %s\r\n", hostname);
    sprintf(message + strlen(message), "Range: bytes=%d-%d\r\n", start, end);
    // sprintf(message + strlen(message), "Connection: keep-alive\r\n");
    sprintf(message + strlen(message), "User-Agent: https_simple\r\n");
    sprintf(message + strlen(message), "\r\n");
    return message;
}

// Get Content-Length without getting the actual file 
int get_content_length(SSL* ssl, char message[]){
    int bytes_received = SSL_read(ssl, message, 2048);
    if (bytes_received < 1) printf("Connection closed by peer!\n");
    // printf("\nResponse (%d bytes): \n'''%.*s'''\n\n", bytes_received, bytes_received, message);
    strcpy(message, strstr(message, "Content-Length:"));
    sscanf(message,"%*s %d", &bytes_received);
    return bytes_received;
}
