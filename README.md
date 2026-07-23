# HTTPS Server in C

An HTTPS server written from scratch in C using raw sockets and OpenSSL for the TLS layer, no web framework and no server library doing the work for me.

## Foreword

Third one in the pile. After the shell and the bare metal UART I wanted to go sideways instead of further down, into networking, i knew what an HTTPS server *does* long before i knew what it actually *is*. This is me finding out. It is currently July 2026, and same as my other repos im using AI to assist me as i reason through what im trying to accomplish. This time the manual was the linux man pages (man 2 socket, man 2 bind, man 2 accept), which is where i kept going back to every time i needed to know what a call actually wanted from me.

⚠️SMALL DISCLAIMER same as my other repos, im very forgetful, so you'll see a LOT of comments in the code written in my own words with analogies. Its how I actually understand and remember what each piece does. Its part of my process.

## Challenges where I got really lost

File descriptors. That one broke me for a while. I kept hearing "everything is a file" and it meant nothing to me until it clicked that its just a number pointing at a row in a table the OS keeps for my process. Once i got that, the whole chain of calls made sense because every single one of them takes that number as the first argument.

The struct threw me too. I saw `struct sockaddr_in addr` and could not tell what was the type and what was the name, and then `addr.sin_addr.s_addr` looked like the word addr three times for no reason. Turned out its a box inside a box.

The theory side i actually felt fine about, the CIA triad is just cybersec and i already had that. Confidentiality, authentication, integrity. TLS is the CIA triad over a wire.

Honestly some things just look cryptic and the way most people explain it? Makes it worse. Also the wrapping on ssl thing but that idea is kinda easy to grasp.

## Notes

Creates a TCP socket and binds it to port 443. Listens with a backlog. Loops on accept, one client at a time. Loads a self signed cert and private key into an OpenSSL context once at startup. Wraps each accepted connection in TLS. Sends a hardcoded page back over the encrypted connection.

## How it works

`socket()` asks the OS for a network endpoint and hands back a file descriptor, the shop is open. `bind()` claims port 443, thats the drive thru window, find me here. `listen()` opens the door and tells the OS to start queuing people up. OpenSSL setup happens once before the loop, the ctx is a settings template holding my cert and key so i configure it one time instead of per client. `accept()` pulls one person off the queue and hands me a NEW file descriptor, because the original one has to stay at the door listening for the next guy. One door, many conversations. `SSL_new` stamps a fresh SSL object out of the template, `SSL_set_fd` ties it to that clients socket, and `SSL_accept` is where the entire handshake actually happens in one call. After that `SSL_read` and `SSL_write` do the same thing read and write do, except OpenSSL handles the crypto and i never touch it.

## What I wrote vs what OpenSSL handled

Worth being straight about this since its the first thing anyone will ask. I wrote the socket layer, the server lifecycle and the HTTP response by hand. OpenSSL handles the TLS handshake and the cryptography. Writing TLS from scratch is a completely different and much bigger project, and rolling your own crypto is a bad idea anyway.

## Requirements

POSIX only. Linux/Unix, will not build on native Windows.

- gcc
- libssl-dev (the OpenSSL headers, this is the one that gets you if you skip it)
- openssl (the command line tool, for generating the cert)

On Debian/Ubuntu:

```
apt update && apt install -y gcc libssl-dev openssl
```

## Build

```
gcc https_server.c -o https_server -lssl -lcrypto
```

The `-lssl -lcrypto` flags link OpenSSL. Without them it will not build. And `-lssl` only links the library, you still need libssl-dev for the header files, those are two different things and i found that out the hard way.

Cert and key are self signed for local testing, im my own CA here so browsers will warn that its untrusted. The encryption works identically, theres just nobody vouching for the identity.

```
openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes
```

Run with sudo since ports under 1024 are privileged, or change it to 8443 in the source if you would rather not.

```
sudo ./https_server
```

Then knock on it from another shell:

```
curl -k https://localhost
```

## Lessons Learned

A file descriptor is just a number pointing at a row in the table the OS keeps for my process. 0 is stdin, 1 is stdout, 2 is stderr, my socket lands in the next open slot. Everything after that is me handing that number back to the OS saying "do this to that one."

Someone already built the IPv4 address as a data type for me, its in `<netinet/in.h>`. Im not constructing an address, im filling in fields on a form that already exists.

`accept()` giving back a second file descriptor instead of reusing the first one is the whole design. The listening socket is the front door and it has to stay at the door. If i talked to a client through it, nobody would be watching for the next one.

TLS is the CIA triad over a wire. Confidentiality is the encryption, authentication is the certificate and the CA vouching for it, integrity is knowing nothing got tampered with in transit. The handshake exists because two machines that never met need to agree on a shared key over a line anyone can watch.

Content-Length has to match the actual byte count of the body. I changed the message and forgot the header still said the old number, which truncates the response or leaves the client hanging waiting for bytes that never come. The header is a promise about how much youre sending.

A server that prints nothing is a server thats working. It just sits there blocked in accept() waiting for someone to knock, and it looks broken when it isnt. Added a printf on startup so i can tell the difference.

Cleanup matters in a loop that never ends. `SSL_free` and `close` on every pass, skip either one and it leaks until the server dies. Resource leaks are exactly the kind of thing that shows up in a security review.

## Status

It serves one hardcoded page over TLS and thats it.

Planned additions: actually parse the HTTP request instead of ignoring it, serve real files, path traversal hardening once file serving exists, error checking on every syscall and every OpenSSL call, and handling more than one client at a time with fork, threads, or epoll. Keep-alive after that.

Built in Debian in a Docker container inside WSL2, written in nano because thats where i was.
