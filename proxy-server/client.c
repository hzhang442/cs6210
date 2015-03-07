#include <stdlib.h>
#include <stdio.h>
#include <rpc/rpc.h>
#include "proxy_rpc.h" 

int main(int argc, char* argv[]){
  CLIENT *cl;
  char** result;
  char* url;
  char* server;
  
if (argc < 3) {
    fprintf(stderr, "usage: %s host url\n", argv[0]);
    exit(1);
  }
  /*
   * Save values of command line arguments
   */
  server = argv[1];
  url = argv[2];

  /*
   * Your code here.  Make a remote procedure call to
   * server, calling httpget_1 with the parameter url
   * Consult the RPC Programming Guide.
   */

  /* Create a client stub using tcp for transport mechanism */
  cl = clnt_create(server, PROXY_RPC, HTTPGETVERS, "tcp");

  if (cl == (CLIENT *) NULL) {
    clnt_pcreateerror(server);
    exit(1);
  }
   
  result = httpget_1(&url, cl);

  /* Check if RPC mechanism failed... if so, bail */
  if (result == (char **) NULL) {
    clnt_perror(cl, server);
    exit(1);
  }

  /* Do client application stuff starting here */

  /* Check if a string was returned by application on proxy server */
  if (*result == (char *) NULL) {

  } else {
    printf(*result);
  }

  return 0;
}
