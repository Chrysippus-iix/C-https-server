// https_server.c
// HTTPS server in C, raw sockets plus OpenSSL for the TLS layer.
//
// POSIX only. Linux/Unix, will not build on native Windows.
//
// Build:  gcc https_server.c -o https_server -lssl -lcrypto
// Certs:  openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes
// Run:    sudo ./https_server     (443 is privileged, or change it to 8443)
//
// im very forgetful so these comments are written in my own words with my own
// analogies. Its how i actually remember what each piece does.

#include <sys/socket.h>
#include <netinet/in.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main() {

    // 1. Create socket, declare variable type integer and name it sockfd as in
    // "socket file descriptor", then use the socket function to create the
    // socket and specify the parameters inside (see manual 2 socket).
    // A file descriptor is just a number pointing at a row in the table the OS
    // keeps for my process. 0 is stdin, 1 is stdout, 2 is stderr, my socket
    // lands in the next open slot. From here on i hand that number back to the
    // OS on every call saying "do this to that one".
    // AF_INET = IPv4, SOCK_STREAM = reliable ordered connection (TCP),
    // 0 = only one protocol matches this combo so let the OS pick it.
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    // 2. Ok our "shop" is open, a process is created, now we gotta bind to a
    // port as in a window for outside connections kinda, like a drive thru or
    // a docking port. Find me at window "443" of the restaurant or something.
    //
    // So here were filling fields in our prepackaged struct it seems, someone
    // already handles our ipv4 info as data already, like, made for us.
    // Whoever wrote the networking headers for Linux already defined that
    // struct so i dont have to. Its in <netinet/in.h> which im already
    // including. Im not building an address, im filling out a form that
    // already exists.
    //
    // Why .sin_? naming convention. The struct is for socket internet
    // addresses so whoever wrote it prefixed every field with sin_ to avoid
    // name collisions. Just a prefix, ignore it.
    //
    // What is htons? "host to network short". My machine might store the
    // number 443 differently than the network expects it. htons wipes it into
    // the correct order for the network. I dont need to know how, just know:
    // always wrap port numbers in htons.
    //
    // Why so many addr? addr.sin_family, addr.sin_port, addr.sin_addr.s_addr.
    // That last one is addr, inside it theres a field called sin_addr, inside
    // THAT theres a field called s_addr. Its a struct inside a struct. Like a
    // box inside a box. addr is the outer box, sin_addr is an inner box,
    // s_addr is the actual value inside that inner box.
    //
    // INADDR_ANY = accept connections on any network interface this box has.
    // The & grabs the memory address of my struct, the cast makes the type
    // match what bind wants, sizeof tells it how much to read.
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(443);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(sockfd, (struct sockaddr*)&addr, sizeof(addr));

    // 3. Listen, tells the OS to start queuing incoming connections.
    // Binding claims the port, listening opens the door.
    // The two arguments are the socket variable name and the backlog, which is
    // how many connections the OS will queue while im busy handling one.
    listen(sockfd, 10);

    // Say something on startup so i know its alive. Without this the terminal
    // just sits there with no output and it looks broken, but a server that
    // prints nothing IS a server thats working, its blocked in accept()
    // waiting at the door for someone to knock. fflush forces it out now
    // instead of sitting in the buffer.
    printf("server up, listening on 443. knock with: curl -k https://localhost\n");
    fflush(stdout);

    // 4. OpenSSL setup. This happens ONCE at startup, before the loop, not per
    // client. The ctx is a settings template. I configure it one time with my
    // cert and my key, then every connection gets stamped out of that same
    // template instead of me reconfiguring for each person walking up.
    //
    // The certificate is the public identity, signed by a CA, it goes out to
    // every client. The private key never leaves this machine. Together they
    // prove the server is who the certificate says it is. Mine are self signed
    // so im my own CA here, browsers will warn, encryption still works the same.
    SSL_library_init();
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    SSL_CTX_use_certificate_file(ctx, "cert.pem", SSL_FILETYPE_PEM);
    SSL_CTX_use_PrivateKey_file(ctx, "key.pem", SSL_FILETYPE_PEM);

    // 5. The accept loop. while(1) just means always true so the loop never
    // stops on its own. A server should never quit after one client, it loops
    // back and waits for the next one. Without the loop i serve exactly one
    // person and then the program hits return 0 and dies.
    while (1) {

        // accept() pulls one waiting connection off the queue and hands back a
        // NEW file descriptor. Not the same one. sockfd keeps it alive while
        // doing other tasks, it stays at the front door listening for the next
        // guy while this new fd handles this one conversation.
        // One door, many conversations.
        // The two NULLs are me saying i dont care about the client address info.
        int clientfd = accept(sockfd, NULL, NULL);

        // accept returns -1 if something went wrong. This check is separate
        // from what drives the loop, the loop runs regardless. continue means
        // abandon this pass and go back to the top, so one failed accept
        // doesnt crash the whole server.
        if (clientfd < 0) continue;

        // 6. Wrap this raw socket in TLS, this is where the S in HTTPS shows up.
        // First time i hear about this "wrapping" thing in this context, but the idea is clear.
        // Right now clientfd is plaintext, anything sent over it goes out
        // readable to anyone in the middle. From here on i read and write
        // through OpenSSL instead of the raw socket.
        SSL *ssl = SSL_new(ctx);       // stamp a fresh SSL object out of the template
        SSL_set_fd(ssl, clientfd);     // tie it to this specific clients socket
        SSL_accept(ssl);               // the whole TLS handshake happens right here

        // 7. Read the request, send the response. SSL_read decrypts for me and
        // SSL_write encrypts for me, i never touch the crypto myself.
        // buf is a 4KB box to hold whatever the client sent.
        //
        // The response below is raw HTTP/1.1 typed out by hand. Status line,
        // headers, blank line, body. The \r\n are the line endings HTTP
        // requires, and that empty \r\n in the middle is the mandatory
        // separator between the headers and the body.
        char buf[4096];
        SSL_read(ssl, buf, sizeof(buf));

        char *response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: 38\r\n"
            "\r\n"
            "Hello, world in C raw network sockets!";

        SSL_write(ssl, response, strlen(response));

        // 8. Cleanup. SSL_free releases the SSL object memory, close releases
        // the file descriptor. Skip either one in a loop that runs forever and
        // it leaks until the server dies. Resource leaks are exactly the kind
        // of thing that shows up in a security review.
        SSL_free(ssl);
        close(clientfd);
    }

    return 0;
}
