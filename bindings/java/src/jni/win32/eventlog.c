#ifdef WIN32
#include "javasigar.h"
#include "win32bindings.h"

#define MAX_INSERT_STRS  8
#define MAX_MSG_LENGTH   4096
#define MAX_ERROR_LENGTH 1024

#define REG_MSGFILE_ROOT "SYSTEM\\CurrentControlSet\\Services\\EventLog\\"

static void win32_set_pointer(JNIEnv *env, jobject obj, const void *ptr)
{
    jfieldID pointer_field;
    int pointer_int;
    jclass cls;

    cls = JENV->GetObjectClass(env, obj);
    
    pointer_field = JENV->GetFieldID(env, cls, "eventLogHandle", "I");
    pointer_int = (int)ptr;

    JENV->SetIntField(env, obj, pointer_field, pointer_int);
}

static HANDLE win32_get_pointer(JNIEnv *env, jobject obj)
{
    jfieldID pointer_field;
    HANDLE h;
    jclass cls;

    cls = JENV->GetObjectClass(env, obj);

    pointer_field = JENV->GetFieldID(env, cls, "eventLogHandle", "I");
    h = (HANDLE)JENV->GetIntField(env, obj, pointer_field);

    if (!h) {
        win32_throw_exception(env, "Event log not opened");
    }

    return h;
}

static int get_messagefile_dll(char *app, char *source, char *dllfile)
{
    HKEY hk;
    DWORD type, data;
    char buf[MAX_MSG_LENGTH];

    sprintf(buf, "%s%s\\%s", REG_MSGFILE_ROOT, app, source);
    
    if (RegOpenKey(HKEY_LOCAL_MACHINE, buf, &hk)) {
        return GetLastError();
    }

    if (RegQueryValueEx(hk, "EventMessageFile", NULL, &type,
                        (UCHAR *)buf, &data)) {
        RegCloseKey(hk);
        return GetLastError();
    }

    strncpy(dllfile, buf, sizeof(dllfile));

    RegCloseKey(hk);

    return 0;
}

static int get_formatted_message(EVENTLOGRECORD *pevlr, char *dllfile,
                                 char *msg)
{
    HINSTANCE hlib;
    LPTSTR msgbuf;
    char msgdll[MAX_MSG_LENGTH];
    char *insert_strs[MAX_INSERT_STRS], *ch;
    int i;

    if (!ExpandEnvironmentStrings(dllfile, msgdll, MAX_PATH))
        return GetLastError();

    if (!(hlib = LoadLibraryEx(msgdll, NULL,
                               LOAD_LIBRARY_AS_DATAFILE)))
        return GetLastError();

    ch = (char *)((LPBYTE)pevlr + pevlr->StringOffset);
    for (i = 0; i < pevlr->NumStrings && i < MAX_INSERT_STRS; i++) {
        insert_strs[i] = ch;
        ch += strlen(ch) + 1;
    }

    FormatMessage(FORMAT_MESSAGE_FROM_HMODULE |
                  FORMAT_MESSAGE_ALLOCATE_BUFFER |
                  FORMAT_MESSAGE_ARGUMENT_ARRAY,
                  hlib,
                  pevlr->EventID,
                  MAKELANGID(LANG_NEUTRAL, SUBLANG_ENGLISH_US),
                  (LPTSTR) &msgbuf,
                  MAX_MSG_LENGTH,
                  insert_strs);

    strncpy(msg, msgbuf, sizeof(msg));

    FreeLibrary(hlib);
    LocalFree((HLOCAL)msgbuf);

    return 0;
}

JNIEXPORT void SIGAR_JNI(win32_EventLog_open)
(JNIEnv *env, jobject obj, jstring lpSourceName)
{
    HANDLE h;
    const char *name;

    name = JENV->GetStringUTFChars(env, lpSourceName, 0);

    h = OpenEventLog(NULL, name);
    if (h == NULL) {
        char buf[MAX_ERROR_LENGTH];
        DWORD lastError = GetLastError();

        sprintf(buf, "Unable to open event log: %d", lastError);
        JENV->ReleaseStringUTFChars(env, lpSourceName, name);
        win32_throw_exception(env, buf);
        return;
    }

    JENV->ReleaseStringUTFChars(env, lpSourceName, name);

    /* Save the handle for later use */
    win32_set_pointer(env, obj, h);
}

JNIEXPORT void SIGAR_JNI(win32_EventLog_close)
(JNIEnv *env, jobject obj)
{
    HANDLE h = win32_get_pointer(env, obj);

    CloseEventLog(h);

    win32_set_pointer(env, obj, NULL);
}

JNIEXPORT jint SIGAR_JNI(win32_EventLog_getNumberOfRecords)
(JNIEnv *env, jobject obj)
{
    DWORD records;
    HANDLE h = win32_get_pointer(env, obj);

    GetNumberOfEventLogRecords(h, &records);

    return records;
}

JNIEXPORT jint SIGAR_JNI(win32_EventLog_getOldestRecord)
(JNIEnv *env, jobject obj)
{
    DWORD oldest;
    HANDLE h = win32_get_pointer(env, obj);

    GetOldestEventLogRecord(h, &oldest);

    return oldest;
}

JNIEXPORT jobject SIGAR_JNI(win32_EventLog_read)
(JNIEnv *env, jobject obj, jint recordOffset)
{
    EVENTLOGRECORD *pevlr;
    BYTE buffer[8192];
    char dllfile[1024];
    DWORD dwRead, dwNeeded;
    LPSTR source, machineName;
    HANDLE h;
    BOOL rv;
    jclass cls = WIN32_FIND_CLASS("EventLogRecord");
    jobject eventObj; /* Actual instance of the EventLogRecord */
    jfieldID id;

    h = win32_get_pointer(env, obj);

    pevlr = (EVENTLOGRECORD *)&buffer;
    rv = ReadEventLog(h,
                      EVENTLOG_SEEK_READ | EVENTLOG_FORWARDS_READ,
                      recordOffset,
                      pevlr,
                      sizeof(buffer),
                      &dwRead,
                      &dwNeeded);
    if (!rv) {
        char buf[MAX_ERROR_LENGTH];
        DWORD lastError = GetLastError();
        
        sprintf(buf, "Error reading from the event log: %d", lastError);
        win32_throw_exception(env, buf);
        return NULL;
    }

    eventObj = JENV->AllocObject(env, cls);

    id = JENV->GetFieldID(env, cls, "recordNumber", "J");
    JENV->SetLongField(env, eventObj, id, pevlr->RecordNumber);

    id = JENV->GetFieldID(env, cls, "timeGenerated", "J");
    JENV->SetLongField(env, eventObj, id, pevlr->TimeGenerated);

    id = JENV->GetFieldID(env, cls, "timeWritten", "J");
    JENV->SetLongField(env, eventObj, id, pevlr->TimeWritten);

    id = JENV->GetFieldID(env, cls, "eventId", "J");
    JENV->SetLongField(env, eventObj, id, pevlr->EventID);

    id = JENV->GetFieldID(env, cls, "eventType", "S");
    JENV->SetShortField(env, eventObj, id, pevlr->EventType);

    /* Extract string data from the end of the structure.  Lame. */

    id = JENV->GetFieldID(env, cls, "source", "Ljava/lang/String;");
    source = (LPSTR)((LPBYTE)pevlr + sizeof(EVENTLOGRECORD));
    SetStringField(env, eventObj, id, source);

    /* Get the formatted message */
    if (!get_messagefile_dll("Application", source, dllfile)) {
        char msg[MAX_MSG_LENGTH];
        if (!get_formatted_message(pevlr, dllfile, msg)) {
            
            id = JENV->GetFieldID(env, cls, "stringData", 
                                  "Ljava/lang/String;");
            SetStringField(env, eventObj, id, msg);
        }
    } else if (pevlr->StringOffset > 0) {
        /* Work around some applications not using a message file */
        char *tmp = (LPSTR)((LPBYTE)pevlr + pevlr->StringOffset);            
        id = JENV->GetFieldID(env, cls, "stringData", "Ljava/lang/String;");
        SetStringField(env, eventObj, id, tmp);
    }

    /* Increment up to the machine name. */
    id = JENV->GetFieldID(env, cls, "computerName", "Ljava/lang/String;");
    machineName = (LPSTR)((LPBYTE)pevlr + sizeof(EVENTLOGRECORD) +
                          strlen(source) + 1);
    SetStringField(env, eventObj, id, machineName);

    /* Get user id info */
    if (pevlr->UserSidLength > 0) {
        char name[256];
        char domain[256];
        DWORD namelen = sizeof(name);
        DWORD domainlen = sizeof(domain);
        DWORD len;
        SID_NAME_USE snu;
        PSID sid;
        
        sid = (PSID)((LPBYTE)pevlr + pevlr->UserSidOffset);
        if (LookupAccountSid(NULL, sid, name, &namelen, domain,
                             &domainlen, &snu)) {
            id = JENV->GetFieldID(env, cls, "user", "Ljava/lang/String;");
            SetStringField(env, eventObj, id, name);
        }
    }
    
    return eventObj;
}

JNIEXPORT void SIGAR_JNI(win32_EventLog_waitForChange)
(JNIEnv *env, jobject obj, jint timeout)
{
    HANDLE h, hEvent;
    DWORD millis;

    h = win32_get_pointer(env, obj);

    hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (hEvent == NULL) {
        win32_throw_exception(env, "Unable to create event");
        return;
    }

    if (timeout == -1)
        millis = INFINITE;
    else
        millis = timeout;

    if(!(NotifyChangeEventLog(h, hEvent))) {
        char buf[MAX_ERROR_LENGTH];
        sprintf(buf, "Error registering for event log to change: %d",
                GetLastError());
        win32_throw_exception(env, buf);
        return;
    }

    if (WaitForSingleObject(hEvent, millis) == WAIT_FAILED)
    {
        char buf[MAX_ERROR_LENGTH];
        sprintf(buf, "Error waiting for event log change: %d",
                GetLastError());
        win32_throw_exception(env, buf);
    }

    return;
}
#endif /* WIN32 */