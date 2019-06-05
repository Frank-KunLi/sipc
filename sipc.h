
typedef struct _IpcHandle_t IpcHandle_t;

typedef int (*IpcMsgClientHandler)(void * arg, int *data);

int Ipc_MessageSend(IpcHandle_t *ipcHdl, char *moduleName, char *msg, int msglen);

int Ipc_CallBackNotify(IpcHandle_t *ipcHdl, char *cbFunc, char *msg, int msglen);

int IpcCallBackRegister(IpcHandle_t *ipcHdl, char *moduleName, char *cbName);

int IpcCallBackUnregister(IpcHandle_t *ipcHdl, char *moduleName, char *cbName);

int IpcInit(IpcHandle_t **ipcHdl, IpcMsgClientHandler func, char *moduleName);

