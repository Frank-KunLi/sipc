#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>

#include "sipc.h"

#define SERVER "server"
#define CLIENT "client"

static unsigned int cli_msg_cookie = 0;

char *date(void)
{
    time_t now = time(&now);
    struct tm *info = localtime(&now);
    char *text = asctime(info);
    text[strlen(text)-1] = '\0'; // remove '\n'
    return (text);
}

int ipcMsgHandler(void *arg, int *data)
{
    char *msg  = (char *)arg;
    int msglen = (int)data;

    if(msg)
    {
        printf("%s: RPC MSG : %s len %d\n", __func__, msg, msglen);
        free(msg);
    }
    else
    {
        printf("Invalid or empty RPC MSG : len %d\n", msglen);  
    }

    return 0;
}

int node(char *name, int count, int sleepTime, char* cbSubModule, char *cbSubFunc, char *cbPubFunc)
{
    printf("%s starting %s ...\n", __func__, name);

    IpcHandle_t *ipcHdl = NULL;
    IpcInit(&ipcHdl, ipcMsgHandler, name);

    IpcCallBackRegister(ipcHdl, cbSubModule, cbSubFunc);

    while (count--) {
        char msg[256];

        snprintf(msg, sizeof(msg), "%s msg <%s> <c=%d>", name, date(), ++cli_msg_cookie);
        Ipc_MessageSend(ipcHdl, cbSubModule, msg, strlen(msg)+1);

        sleep(sleepTime);

        char cb_msg[256];
        snprintf(cb_msg, sizeof(cb_msg), "CB MESSAGE from %s <%s> <c=%d>", name, date(), ++cli_msg_cookie);
        Ipc_CallBackNotify(ipcHdl, cbPubFunc, cb_msg, strlen(cb_msg)+1);
    }

    return 0;
}

int main(const int argc, const char **argv)
{
    if (argc < 3) 
    {
        printf("Usage: pubsub %s|%s <URL> <ARG> ...\n", SERVER, CLIENT);
        return 1;
    }

    char *moduleName    = (char *)argv[1];
    int count           = atoi((char *)argv[2]);
    int sleepTime       = atoi((char *)argv[3]);
    char* cbSubModule   = (char *)argv[4];
    char *cbSubFunc     = (char *)argv[5];
    char *cbPubFunc     = (char *)argv[6];

    return (node(moduleName, count, sleepTime, cbSubModule, cbSubFunc, cbPubFunc));
}
