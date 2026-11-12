#define INIT_EVENT_IDS
#include "port/hooks/Events.h"
#include <string.h>
typedef struct { EventID event; ListenerID id; EventCallback callback; EventPriority priority; bool active; } Slot;
static Slot listeners[64];
static EventID next_event;
static ListenerID next_listener;
EventID EventSystem_RegisterEvent(void) { return next_event++; }
ListenerID EventSystem_RegisterListener(EventID event,EventCallback callback,EventPriority priority) {
    for(int i=0;i<64;++i) if(!listeners[i].active) {
        listeners[i]=(Slot){event,next_listener++,callback,priority,true};
        return listeners[i].id;
    }
    return (ListenerID)-1;
}
void EventSystem_UnregisterListener(EventID event,ListenerID id) {
    for(int i=0;i<64;++i) if(listeners[i].event==event && listeners[i].id==id) listeners[i].active=false;
}
void EventSystem_CallEvent(EventID event,void *data) {
    for(int priority=EVENT_PRIORITY_HIGH;priority>=EVENT_PRIORITY_LOW;--priority)
        for(int i=0;i<64;++i) if(listeners[i].active && listeners[i].event==event && listeners[i].priority==priority)
            listeners[i].callback(data);
}
typedef struct { char name[96]; float value; bool active; } Config;
static Config settings[32];
int32_t CVarGetInteger(const char *name,int32_t fallback) {
    for(int i=0;i<32;++i) if(settings[i].active && !strcmp(settings[i].name,name)) return (int32_t)settings[i].value;
    return fallback;
}
float CVarGetFloat(const char *name,float fallback) {
    for(int i=0;i<32;++i) if(settings[i].active && !strcmp(settings[i].name,name)) return settings[i].value;
    return fallback;
}
void CVarSetFloat(const char *name,float value) {
    for(int i=0;i<32;++i) if(settings[i].active && !strcmp(settings[i].name,name)) { settings[i].value=value; return; }
    if(strlen(name)>=sizeof(settings[0].name)) return;
    for(int i=0;i<32;++i) if(!settings[i].active) {
        strcpy(settings[i].name,name); settings[i].value=value; settings[i].active=true; return;
    }
}
bool gBackToMap=false;
int32_t GetNumAudioChannels(void) { return 2; }
