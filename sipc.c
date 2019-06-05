#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>

#include "pthread.h"

#include <nanomsg/nn.h>
#include <nanomsg/pubsub.h>
#include <nanomsg/reqrep.h>

#include "sipc.h"

#define SIZEOF(x)   (sizeof((x))/sizeof((x)[0]))

#define FREE_MEM(x) if((x)) { free((void *)(x)); (x)=NULL; }

#define IPC_URL_MAX_LEN                     256
#define IPC_CALLBACK_MAX_SUBSCRIPTIONS      20
#define MAX_POLL_FDS                        (IPC_CALLBACK_MAX_SUBSCRIPTIONS + 1)
#define IPC_CALLBACK_MAX_FUNCS              20

#define SOCK_UNUSED                         -1

/*---------------- IPC MESSAGE STRUCTURE ----------------

 IpcHdr # <Type = IpcCb/IpcMsg> # <TypeData = Cb/Rpc FuncName> # <from_module> # <to_module>  # <AppData>

 ---------------- IPC MESSAGE STRUCTURE ----------------*/

#define IPC_MSG_FIELD_DELIMITER       '#'
#define IPC_MSG_FIELD_DELIMITER_STR   "#"

#define IPC_MSG_HDR_STR             "IpcHdr"
#define IPC_MSG_HDR_STR_SZ          sizeof(IPC_MSG_HDR_STR)

#define IPC_MSG_TYPE_CB_STR         "IpcCb"
#define IPC_MSG_TYPE_CB_STR_SZ      sizeof(IPC_MSG_TYPE_CB_STR)

#define IPC_MSG_TYPE_MSG_STR        "IpcMsg"
#define IPC_MSG_TYPE_MSG_STR_SZ     sizeof(IPC_MSG_TYPE_MSG_STR)

#define IPC_MSG_TYPE_STR_SZ         IPC_MSG_TYPE_MSG_STR_SZ

#define IPC_MSG_TYPE_DATA_FIELD_SZ  128

#define IPC_MSG_APPDATA_FIELD_SZ    4096

#define IPC_MSG_MAX_SZ              (IPC_MSG_APPDATA_FIELD_SZ + IPC_MSG_TYPE_DATA_FIELD_SZ + IPC_MSG_TYPE_STR_SZ + IPC_MSG_HDR_STR_SZ + 3 + 1)  //3 for delimiters + 1 null char

typedef enum _IpcMsgField_t
{
    IpcMsgField_Hdr,
    IpcMsgField_Type,
    IpcMsgField_TypeData,
    IpcMsgField_SrcModule,
    IpcMsgField_DstModule,
    IpcMsgField_AppData,
    IpcMsgField_Max
}IpcMsgField_t;

typedef struct _cbPub_t
{
    int pubSock;
}cbPub_t;

typedef struct _cbSub_t
{
    int     subSock;
    char    *moduleName;
    char    *cbFuncs[IPC_CALLBACK_MAX_FUNCS];
}cbSub_t;

struct _IpcHandle_t
{
    pthread_t           tId;    
    IpcMsgClientHandler func;
    int                 repSock;
    int                 reqSock;
    cbPub_t             cbPub;
    cbSub_t             cbSub[10];
    char*               self;
};

typedef enum _IpcModuleUrlType_t{
    IpcModuleUrl_Cb,
    IpcModuleUrl_Ipc
}IpcModuleUrlType_t;

typedef struct _nnStats
{
    int stat;
    char *statName;
}nnStats_t;

extern int errno;

/*---------------- FUNCTIONS ----------------*/

__attribute__((unused)) static void dbg_printStats(int sockfd) 
{
    nnStats_t connStats[] = 
    {
        {NN_STAT_MESSAGES_SENT          , "NN_STAT_MESSAGES_SENT"},
        {NN_STAT_MESSAGES_RECEIVED      , "NN_STAT_MESSAGES_RECEIVED"},
        {NN_STAT_BYTES_SENT             , "NN_STAT_BYTES_SENT"},
        {NN_STAT_BYTES_RECEIVED         , "NN_STAT_BYTES_RECEIVED"},
        {NN_STAT_CURRENT_CONNECTIONS    , "NN_STAT_CURRENT_CONNECTIONS"},
        {NN_STAT_ACCEPT_ERRORS          , "NN_STAT_ACCEPT_ERRORS"},
        {NN_STAT_BIND_ERRORS            , "NN_STAT_BIND_ERRORS"},
        {NN_STAT_CONNECT_ERRORS         , "NN_STAT_CONNECT_ERRORS"},
        {NN_STAT_BROKEN_CONNECTIONS     , "NN_STAT_BROKEN_CONNECTIONS"},
        {NN_STAT_DROPPED_CONNECTIONS    , "NN_STAT_DROPPED_CONNECTIONS"},
        {NN_STAT_ACCEPTED_CONNECTIONS   , "NN_STAT_ACCEPTED_CONNECTIONS"},
        {NN_STAT_ESTABLISHED_CONNECTIONS, "NN_STAT_ESTABLISHED_CONNECTIONS"},
    };

    int idx = 0;
    for(idx = 0; idx < SIZEOF(connStats); idx++ ) {
        int val = nn_get_statistic (sockfd, connStats[idx].stat);
        printf ("%s: Value of %s = %d\n", __func__, connStats[idx].statName, val);
    }
}

static bool IsValidModule(char *moduleName)
{
    return((moduleName != NULL));
}

static int CBSub_GetModuleIndex(IpcHandle_t *ipcHdl, char *moduleName)
{
    int i = 0;
    bool modFound = false;

    while((ipcHdl->cbSub[i].subSock != SOCK_UNUSED) && (i < IPC_CALLBACK_MAX_SUBSCRIPTIONS)) {
        if(ipcHdl->cbSub[i].moduleName && (strcmp(ipcHdl->cbSub[i].moduleName, moduleName) == 0)) {
            modFound = true;
            break;
        }
        i++;
    }

    return((modFound) ? i : -1);
}

static int CBSub_GetCbIndex(IpcHandle_t *ipcHdl, char *moduleName, char *cbName)
{
    int j = 0;
    bool cbFound  = false;

    int i = CBSub_GetModuleIndex(ipcHdl, moduleName);

    if(i >= 0){
        while(j < IPC_CALLBACK_MAX_FUNCS) {
            if(ipcHdl->cbSub[i].cbFuncs[j] && (strcmp(ipcHdl->cbSub[i].cbFuncs[j], cbName) == 0)){
                cbFound = true;
                break; 
            }
            j++;
        }
    }

    return (cbFound) ? j : -1;
}

static bool CBSub_IsRegistered(IpcHandle_t *ipcHdl, char *moduleName)
{
    int i = CBSub_GetModuleIndex(ipcHdl, moduleName);

    return(i >= 0);
}

static int CbSub_GetFreeModuleSlot(IpcHandle_t *ipcHdl)
{
    int i = 0;

    while((i < IPC_CALLBACK_MAX_SUBSCRIPTIONS) && (ipcHdl->cbSub[i].subSock != SOCK_UNUSED)) {
        i++;
    }

    return ( i == IPC_CALLBACK_MAX_SUBSCRIPTIONS ) ? -1 : i;
}

static int CbSub_GetFreeCbSlot(IpcHandle_t *ipcHdl, char *moduleName)
{
    int i = CBSub_GetModuleIndex(ipcHdl, moduleName);
    if(i < 0) {
        printf("%s: Module %s not registered", __func__, moduleName);
        return -1;
    }

    int j = 0;
    while((j < IPC_CALLBACK_MAX_FUNCS) && (ipcHdl->cbSub[i].cbFuncs[j] != NULL)) {
        j++;
    }

    return ( j == IPC_CALLBACK_MAX_FUNCS ) ? -1 : j;
}

static int CBSub_RequestNotification(IpcHandle_t *ipcHdl, char *moduleName, char *cbName)
{
    int i = CBSub_GetModuleIndex(ipcHdl, moduleName);

    if(i >= 0)
    {
        int j = CBSub_GetCbIndex(ipcHdl, moduleName, cbName);
        if(j < 0)
        {
            j = CbSub_GetFreeCbSlot(ipcHdl, moduleName);
            if(j >= 0) {
                ipcHdl->cbSub[i].cbFuncs[j] = strdup(cbName);
                printf("%s: CB func %s registered", __func__, cbName);
            }
            else
                return -1;
        }
        else
            printf("%s: CB func %s already registered", __func__, cbName);
    }
    
    return 0;
}

static int CBSub_AddModule(IpcHandle_t *ipcHdl, char *moduleName)
{
    if(!moduleName){
        printf("Invalid module param NULL");
        return -1;    
    }

    if(CBSub_IsRegistered(ipcHdl, moduleName))
    {
        printf("%s: CB module %s already registered", __func__, moduleName);
        return -1;
    }

    int i = CbSub_GetFreeModuleSlot(ipcHdl);

    if(i < 0){
        printf("%s: No free callback slots for registering %s\n", __func__, moduleName);
        return -1;
    }
    
    ipcHdl->cbSub[i].moduleName = strdup(moduleName);
    printf("%s: CB for module %s registered", __func__, moduleName);

    return 0;
}

static int Ipc_GetModuleUrl(char *moduleName, char **url, IpcModuleUrlType_t urlType)
{
    int rv = 0;

    if(moduleName == NULL) {
        *url = NULL;
        return -1;
    }

    *url = calloc(1, IPC_URL_MAX_LEN);
    if(*url) {
        char *urlFormat = (urlType == IpcModuleUrl_Ipc) ? "ipc:///tmp/ipc_%s.ipc" : "ipc:///tmp/cb_%s.ipc";
        int n = snprintf(*url, IPC_URL_MAX_LEN, urlFormat, moduleName);
        if(n > IPC_URL_MAX_LEN) {
            printf("Module name too long\n");
            return -1;
        }
    }

    return rv;
}

static char **getMsgTokens(char *buf)
{
    char **tokens;
    char *saveptr1 = NULL;
    char *tokenStr = NULL;
    int i = 0;

    tokens = calloc(IpcMsgField_Max, sizeof(char *));

    for(i = 0; i < IpcMsgField_Max; i++ )
        tokens[i] = NULL;

    tokenStr = strtok_r(buf, IPC_MSG_FIELD_DELIMITER_STR, &saveptr1);
    if(tokenStr)
    {
        i = 0;
        tokens[i++] = strdup(tokenStr);

        while(true)
        {
            tokenStr = strtok_r(NULL, IPC_MSG_FIELD_DELIMITER_STR, &saveptr1);
            if (tokenStr == NULL)
                break;
            tokens[i] = strdup(tokenStr);
            //printf("i = %d tokens = %s [buf = %s]\n", i, tokens[i], buf);
            i++;
        }
    }

    return tokens;
}

static int formIpcMsgBuffer(char *src_module, char *dst_module, char *cbFunc, char *in_msg, char **out_msg, bool isCallbackMsg)
{
    int retval = 0;
    char *buf = calloc(1, IPC_MSG_MAX_SZ);
    if(buf)
    {
        int n = snprintf(buf, IPC_MSG_MAX_SZ, "%s%c%s%c%s%c%s%c%s%c%s", IPC_MSG_HDR_STR, IPC_MSG_FIELD_DELIMITER, 
                                                            (isCallbackMsg?IPC_MSG_TYPE_CB_STR:IPC_MSG_TYPE_MSG_STR), IPC_MSG_FIELD_DELIMITER, 
                                                            cbFunc, IPC_MSG_FIELD_DELIMITER,
                                                            src_module, IPC_MSG_FIELD_DELIMITER,
                                                            dst_module, IPC_MSG_FIELD_DELIMITER,
                                                            (in_msg)?in_msg:"");
        if(n > IPC_MSG_MAX_SZ){
            printf("%s: msg too long - max size=%d...\n", __func__, IPC_MSG_APPDATA_FIELD_SZ);
            retval = -1;
        }
        else {
            //printf("%s: msg created = %s [%d]\n", __func__, buf, n);
            *out_msg = buf;
            retval = n+1;   //include 1 for '\0'
        }
    }
    else {
        printf("%s: calloc failed\n", __func__);
        retval = -1;
    }

    return retval;
}

static int processCallbackNtf(IpcHandle_t *ipcHdl, char *buf, int bytes)
{
    int retval = 0;
    char **tokens = getMsgTokens(buf);
    char *msgType       = tokens[IpcMsgField_Type];
    char *msgTypeData   = tokens[IpcMsgField_TypeData];
    char *msgSrcModule  = tokens[IpcMsgField_SrcModule];
    char *msgAppData = tokens[IpcMsgField_AppData];

    if(msgType && (strcmp(msgType, IPC_MSG_TYPE_CB_STR) ==0)) 
    {
        if(msgTypeData)
        {
            if(CBSub_GetCbIndex(ipcHdl, msgSrcModule, msgTypeData) >= 0) {
                //printf("%s:calling App CB handler...\n", __func__);
                ipcHdl->func(strdup((void*)msgAppData), (int *)strlen(msgAppData));
            }
            else {
                printf("Unsolicted callback %s from %s, ignoring...\n", msgTypeData, msgSrcModule);
            }
        }
        else {
            printf("%s:Invalid msgTypeData...[0x%x]\n", __func__, (int)msgTypeData);
            retval = -1;
        }
    }
    else{
        printf("Error - Not a callback msg %s, ignoring...\n", buf);
    }

    FREE_MEM(msgType);
    FREE_MEM(msgTypeData);
    FREE_MEM(msgSrcModule);
    FREE_MEM(msgAppData);
    //todo free tokens

    return retval;
}

static int processMsg(IpcHandle_t *ipcHdl, char *buf, int bytes)
{
    int retval = 0;
    char **tokens = getMsgTokens(buf);
    char *msgType       = tokens[IpcMsgField_Type];
    char *msgTypeData   = tokens[IpcMsgField_TypeData];
    char *msgSrcModule  = tokens[IpcMsgField_SrcModule];
    char *msgDstModule  = tokens[IpcMsgField_DstModule];
    char *msgAppData    = tokens[IpcMsgField_AppData];

    if(msgType && (strcmp(msgType, IPC_MSG_TYPE_MSG_STR) ==0)) 
    {
        char *msgBuf = NULL;
        int bytes = 0;
        int n = formIpcMsgBuffer(msgDstModule, msgSrcModule, "IpcMsgAck", NULL, &msgBuf, false);

        if(n > 0){
            if ((bytes = nn_send(ipcHdl->repSock, msgBuf, n, 0)) < 0) {
                printf("%s:Send Failed %s [len %d]\n", __func__, msgBuf, n);
                retval = -1;
            }
        }

        FREE_MEM(msgBuf);

        if(msgTypeData && (retval == 0))
        {
            if(strcmp(msgDstModule, ipcHdl->self) == 0)
            {
                if(strcmp(msgTypeData, "IpcMsgAck") == 0) {
                    printf("%s:Msg ACK received[ %s # %s # %s # %s ]...\n", __func__, msgType, msgSrcModule, msgDstModule, msgTypeData);
                }
                else {
                    // printf("%s:calling App Msg handler[%s]...\n", __func__, msgTypeData);
                    ipcHdl->func(strdup((void*)msgAppData), (int *)strlen(msgAppData));
                }
            }
            else {
                printf("Msg %s not for %s [s=%s,d=%s], ignoring...\n", msgTypeData, ipcHdl->self, msgDstModule, msgSrcModule);
            }
        }
        else {
            printf("%s:Invalid msgTypeData...[0x%x]\n", __func__, (int)msgTypeData);
            retval = -1;
        }
    }
    else{
        printf("Error - Not a IPC msg %s, ignoring...\n", buf);
    }

    FREE_MEM(msgType);
    FREE_MEM(msgTypeData);
    FREE_MEM(msgSrcModule);
    FREE_MEM(msgDstModule);
    FREE_MEM(msgAppData);
    FREE_MEM(tokens);

    return retval;
}

static void* ipcMessageRecv(void *arg)
{
    int rv;
    struct nn_pollfd pfd[MAX_POLL_FDS];
    int i = 0, j = 0;

    IpcHandle_t *ipcHdl = (IpcHandle_t *)arg;
    
    for(i = 0; (ipcHdl->cbSub[i].subSock != SOCK_UNUSED) && (i < MAX_POLL_FDS); i++) 
    {
        pfd [i].fd     = ipcHdl->cbSub[i].subSock;       // poll for incoming callback notifications
        pfd [i].events = NN_POLLIN;
    }

    pfd [i].fd = ipcHdl->repSock;               // poll for incoming requests
    pfd [i].events = NN_POLLIN;

    for (;;) 
    {
        rv = nn_poll (pfd, (i+1), -1);

        // printf("%s: Data available for reading...\n", __func__);
    
        if (rv == 0) {
            printf ("%s:nn_poll Timeout!\n", __func__);
            continue;
        }

        if (rv == -1) {
            printf ("%s: Error nn_poll!\n", __func__);
            continue;
        }

        for(j=0; j < i; j++) 
        {
            if (pfd [j].revents & NN_POLLIN) 
            {
                //printf ("reading from subSock %d...\n", j);
                
                char *buf = NULL;
                int bytes;

                if ((bytes = nn_recv(ipcHdl->cbSub[j].subSock, &buf, NN_MSG, 0)) < 0) {
                    printf("Error nn_recv from subSock\n ");
                    continue;
                }
                
                printf("%s[%d]: RECEIVED CB[%s][bytes=%d]\n", __func__, getpid(), buf, bytes);
                processCallbackNtf(ipcHdl, buf, bytes);
                
                nn_freemsg(buf);
            }
        }

        if (pfd [i].revents & NN_POLLIN)
        {            
            //printf ("reading from repSock...\n");
            
            char *buf = NULL;
            int bytes;

            if ((bytes = nn_recv(ipcHdl->repSock, &buf, NN_MSG, 0)) < 0) {
                printf("Error nn_recv from reqSock\n ");
                continue;
            }

            printf("%s[%d]: RECEIVED REQUEST[%s][bytes=%d]\n", __func__, getpid(), buf, bytes);
            processMsg(ipcHdl, buf, bytes);

            nn_freemsg(buf);
        }
    }

    return 0;
}

int Ipc_MessageSend(IpcHandle_t *ipcHdl, char *moduleName, char *msg, int msglen)
{
    char *url = NULL;
    int bytes = -1;
    int rv = 0;

    if(msg == NULL) {
        printf("%s:msg string NULL\n", __func__);
        return -1;
    }

    if(!IsValidModule(moduleName)) {
        printf("%s:Invalid module %s\n", __func__, moduleName);
        return -1;
    }

    if ((ipcHdl->reqSock = nn_socket(AF_SP, NN_REQ)) < 0) {
        printf("%s:Create reqSock Failed\n", __func__);
        return -1;
    }

    Ipc_GetModuleUrl(moduleName, &url, IpcModuleUrl_Ipc);

    if ((rv = nn_connect (ipcHdl->reqSock, url)) < 0) {
        printf("%s:Connect Failed %s\n", __func__, url);        
        FREE_MEM(url);
        return -1;
    }
    
    FREE_MEM(url);
    
    printf("%s[%d]: SENDING %s\n", __func__, getpid(), msg);

    char *msgBuf = NULL;

    int n = formIpcMsgBuffer(ipcHdl->self, moduleName, "UNUSED", msg, &msgBuf, false);

    if(n > 0){
        if ((bytes = nn_send(ipcHdl->reqSock, msgBuf, n, 0)) < 0) {
            printf("%s:Send Failed %s [len %d]\n", __func__, msgBuf, n);
            rv = -1;
        }
        else
        {
            FREE_MEM(msgBuf);
            rv = 0;
        }
    }
    else {
        rv = -1;
    }

    nn_shutdown(ipcHdl->reqSock, 0);
    return rv;
}

int Ipc_CallBackNotify(IpcHandle_t *ipcHdl, char *cbFunc, char *msg, int msglen)
{
    char *msgBuf = NULL;
    int rv = 0;

    int n = formIpcMsgBuffer(ipcHdl->self, "UNUSED", cbFunc, msg, &msgBuf, true);

    if(n > 0) {
        int bytes = nn_send(ipcHdl->cbPub.pubSock, msgBuf, n, 0);
        if (bytes < 0) {
            printf("%s:CB Publish Failed %s [len %d]\n", __func__, msgBuf, n);
            rv = -1;
        }
        else
            printf("%s: PUBLISHING %s\n", __func__, msg);
    }
    else {
        rv = -1;
    }

    return rv;
}

int IpcCallBackRegister(IpcHandle_t *ipcHdl, char *moduleName, char *cbName)
{
    int i = 0;
    char *url = NULL;

    if(!IsValidModule(moduleName)){
        printf("%s: Invalid module %s\n", __func__, moduleName);
        return 1;
    }

    if(CBSub_IsRegistered(ipcHdl, moduleName)) {
        printf("%s: CB already registered[%s %s]\n", __func__, moduleName, cbName);        
    }
    else {

        i = CbSub_GetFreeModuleSlot(ipcHdl);

        if(i < 0){
            printf("%s: No free callback slots![%s %s]\n", __func__, moduleName, cbName);
            return -1;
        }

        CBSub_AddModule(ipcHdl, moduleName);

        printf("%s: SUBSCRIBING to %s %s\n", __func__, moduleName, cbName);

        if ((ipcHdl->cbSub[i].subSock = nn_socket(AF_SP, NN_SUB)) < 0) {
            printf("%s: Creating SUB socket failed!\n", __func__);
            return -1;
        }

        // subscribe to everything ("" means all topics)
        if (nn_setsockopt(ipcHdl->cbSub[i].subSock, NN_SUB, NN_SUB_SUBSCRIBE, "", 0) < 0) {
            printf("%s: nn_setsockopt failed\n", __func__);
            return -1;
        }

        Ipc_GetModuleUrl(moduleName, &url, IpcModuleUrl_Cb);

        if (nn_connect(ipcHdl->cbSub[i].subSock, url) < 0) {
            printf("%s: connect to %s failed\n", __func__, url);
            FREE_MEM(url);
            return -1;
        }

        FREE_MEM(url);
    }

    CBSub_RequestNotification(ipcHdl, moduleName, cbName);
    return 0;   
}

int IpcCallBackUnregister(IpcHandle_t *ipcHdl, char *moduleName, char *cbName)
{
    if(!IsValidModule(moduleName)){
        printf("%s: Invalid module %s\n", __func__, moduleName);
        return -1;
    }

    if(!CBSub_IsRegistered(ipcHdl, moduleName)){
        printf("%s: module %s already unregistered \n", __func__, moduleName);
        return 0;    
    }    

    return 0;
}

int IpcInit(IpcHandle_t **pIpcHdl, IpcMsgClientHandler func, char *moduleName)
{
    int rv = 0;
    char *cburl = NULL, *ipcurl = NULL;
    int i = 0;

    // TODO : free this memory
    *pIpcHdl = calloc(1, sizeof(IpcHandle_t));
    
    IpcHandle_t *ipcHdl = *pIpcHdl;
    if(ipcHdl == NULL){
        printf("%s:failed to allocate memory for IpcHandle_t\n", __func__);
        return -1;
    }

    ipcHdl->reqSock = ipcHdl->repSock = SOCK_UNUSED;
    ipcHdl->cbPub.pubSock = SOCK_UNUSED;

    for(i = 0; i < IPC_CALLBACK_MAX_SUBSCRIPTIONS; i++)
        ipcHdl->cbSub[i].subSock = SOCK_UNUSED;

    if(!func) {
        printf("%s:Invalid ipc msg handler func\n", __func__);
        return -1;
    }

    ipcHdl->func = func;

    if(!IsValidModule(moduleName)) {
        printf("Error: Invalid module: %s\n", moduleName);
        return -1;
    }

    ipcHdl->self = strdup(moduleName);

    if ((ipcHdl->cbPub.pubSock = nn_socket(AF_SP, NN_PUB)) < 0) {
        printf("%s:failed to create pub sock\n", __func__);
        return -1;
    }

    Ipc_GetModuleUrl(ipcHdl->self, &cburl, IpcModuleUrl_Cb);    
    if (nn_bind(ipcHdl->cbPub.pubSock, cburl) < 0) {
        printf("%s: Binding pubsock to %s failed[%s]\n", __func__, cburl, strerror(errno));
        FREE_MEM(cburl);
        return -1;
    }

    if ((ipcHdl->repSock = nn_socket(AF_SP, NN_REP)) < 0) {
        printf("%s:Failed to open repSock!\n", __func__);
        FREE_MEM(cburl);
        return -1;
    }
    
    Ipc_GetModuleUrl(ipcHdl->self, &ipcurl, IpcModuleUrl_Ipc);    
    if ((rv = nn_bind(ipcHdl->repSock, ipcurl)) < 0) {
        printf("%s:failed to bind repSock to %s[%s]\n", __func__, ipcurl, strerror(errno));
        FREE_MEM(ipcurl);
        return -1;
    }

    if ( pthread_create(&(ipcHdl->tId),  NULL, (void*(*)(void*))ipcMessageRecv, (void *)ipcHdl) < 0 ) {
        printf("%s:Create thread fail!\n", __func__); 
        FREE_MEM(cburl);
        FREE_MEM(ipcurl);
        return -1;
    }
    
    FREE_MEM(cburl);
    FREE_MEM(ipcurl);

    return 0;
}


