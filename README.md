# vbridge
Cloud desktop software

## Usage
vBridge is a client/server tool.
`vbridged`, the server, must be launched on the X session you want to connect to.
`vbridge`, the client, can be launched anywhere as long as it can connect to `vbridged`.

### Example
On the desktop you want to connect:
~~~
$ vbridged
~~~

On the desktop you want to see the remote one:
~~~
$ vbridge host-to-connect-to.tld
~~~

## Authentication
Two methods are possible: via a key or via login/password.

For using the login/password, simply use the login and password of the account where you launched the server.

For the key:
- Launch the server
- Launch the client. The client will ask you to confirm you trust the server. Accepts both times and quit without performing the login with password.
- On the client's host, copy the content of the `~/.vbridge/connect` file
- On the servers's host, paste the content copied on the previous step into `~/.vbridge/accept` file
- On the servers's host, kill and relaunch the server `vbridged`
- On the client's host, launch the client. It will be authenticated without asking login and password.

### Options
~~~
  --port=PORT              port to listen
  --ciphers=LIST           sets the list of ciphers
  --rsa-length=NUMBER      RSA modulus length (in bits)
  --rsa-exponent=NUMBER    RSA public exponent
  --ecdh-curve=NAME        ECDH curve name
  --background             run in background
  --reinit-cred            reinitialize the user's pam credentials
  --timeout=NUMBER         inactivity timeout
  --version                display version information
  --help                   display this help
~~~

## Compilation
### Compilation requirements
vBridge needs the following packets to be installed for the compilation:
- gcc
- make
- libx11-dev
- libxfixes-dev
- libxext-dev
- libxi-dev
- libxtst-dev
- libxrandr-dev
- libkrb5-dev
- libpam0g-dev
- libssl-dev
- libcap-dev

### Compilation
`make` compiles and produces the `vbridge` and `vbridged` binaries.

Additionnally, you can compile it inside a Docker container.
Simply type the commands below:
~~~
$ docker build -t vbridge .
$ docker create vbridge vbridge
$ docker cp vbridge:/home/vbridge/vbridge .
$ docker cp vbridge:/home/vbridge/vbridged .
~~~

