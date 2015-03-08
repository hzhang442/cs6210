#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <rpc/rpc.h>

/*
 * Sends a request to *url and returns an address to
 * an array containing the response.  Your implementation
 * should work when req is null.
 *
 * Consult the libcurl library
 *
 * http://curl.haxx.se/libcurl/
 *
 * and the example getinmemory.c in particular
 *
 * http://curl.haxx.se/libcurl/c/getinmemory.html
 */

typedef struct web_contents {
  char *data;
  size_t size;
} web_contents_t;

static size_t cache_contents(void *contents, size_t size, size_t nmemb, void *userp) {
  size_t realsize = size * nmemb;
  web_contents_t *web_contents = (web_contents_t *) userp;

  web_contents->data = realloc(web_contents->data, web_contents->size + realsize + 1);

  /* If we cannot get the memory to cache this page, return an error */
  if (web_contents->data == (char *) NULL) {
    return 0;
  }

  memcpy(&(web_contents->data[web_contents->size]), contents, realsize);
  web_contents->size += realsize;
  web_contents->data[web_contents->size] = '\0';

  return realsize;
}

char ** httpget_1_svc(char** url, struct svc_req* req){
  static web_contents_t web_contents;
  CURL *curl_handle;
  CURLcode res;

  /* FIXME: If we don't have contents cached... */

  web_contents.data = malloc(1);
  web_contents.size = 0;

  /* Initialize curl handler for this call */
  curl_global_init(CURL_GLOBAL_ALL);
  curl_handle = curl_easy_init();
  curl_easy_setopt(curl_handle, CURLOPT_URL, *url);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, cache_contents);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *) &web_contents);
  curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

  /* Make curl call and see if it was successful */
  res = curl_easy_perform(curl_handle);
  if (res != CURLE_OK) {
    /* On failure, return a NULL string */
    free(web_contents.data);
    web_contents.data = NULL;
    return &web_contents.data; 
  }

  curl_easy_cleanup(curl_handle);
  curl_global_cleanup();

  /* FIXME: Write data to a file for later requests */

  /* Return data */
  return &web_contents.data;
}
