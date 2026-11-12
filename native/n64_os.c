/* Native N64 API bridge. Hardware scheduling/RSP APIs are deliberately absent
 * until their real replacements are implemented, so unresolved calls stay visible. */
#include "sys.h"
#include "host.h"

_Static_assert(sizeof(void*)==4,"SF64 native port requires a 32-bit target");
_Static_assert(sizeof(u32)==4,"SF64 u32 must remain 32 bits");
_Static_assert(sizeof(Gfx)==8,"Native GBI must use two 32-bit words");
_Static_assert(sizeof(OSMesgQueue)==24,"N64 queue ABI changed");

u64 osClockRate=OS_CLOCK_RATE;
u32 osMemSize=8*1024*1024;
s32 osTvType=OS_TV_NTSC;
static u64 time_origin;
static u64 time_base;

OSTime osGetTime(void) {
    return time_base+((sf64_host_time_us()-time_origin)*46875)/1000;
}
void osSetTime(OSTime time) {
    time_origin=sf64_host_time_us();
    time_base=time;
}
u32 osGetCount(void) { return (u32)osGetTime(); }

void osCreateMesgQueue(OSMesgQueue *queue,OSMesg *messages,s32 count) {
    queue->mtqueue=NULL;
    queue->fullqueue=NULL;
    queue->validCount=0;
    queue->first=0;
    queue->msgCount=count;
    queue->msg=messages;
    if (sf64_host_mq_create(queue,count)!=0) queue->msgCount=0;
}
s32 osSendMesg(OSMesgQueue *queue,OSMesg message,s32 flag) {
    int result=sf64_host_mq_send(queue,message,flag==OS_MESG_BLOCK);
    queue->validCount=sf64_host_mq_count(queue);
    return result;
}
s32 osRecvMesg(OSMesgQueue *queue,OSMesg *message,s32 flag) {
    int result=sf64_host_mq_recv(queue,message,flag==OS_MESG_BLOCK);
    queue->validCount=sf64_host_mq_count(queue);
    return result;
}

static void controller_status(OSContStatus *status) {
    for (int i=0;i<4;++i) {
        status[i].type=i==0 ? CONT_TYPE_NORMAL : 0;
        status[i].status=0;
        status[i].errno=i==0 ? 0 : CONT_NO_RESPONSE_ERROR;
    }
}
s32 osContInit(OSMesgQueue *queue,u8 *pattern,OSContStatus *status) {
    (void)queue;
    *pattern=1;
    controller_status(status);
    return 0;
}
void osContGetQuery(OSContStatus *status) { controller_status(status); }
s32 osContStartQuery(OSMesgQueue *queue) { return osSendMesg(queue,NULL,OS_MESG_NOBLOCK); }
s32 osContStartReadData(OSMesgQueue *queue) { return osSendMesg(queue,NULL,OS_MESG_NOBLOCK); }
void osContGetReadData(OSContPad *pads) {
    for (int i=0;i<4;++i) {
        pads[i].button=0; pads[i].stick_x=0; pads[i].stick_y=0;
        pads[i].errno=i==0 ? 0 : CONT_NO_RESPONSE_ERROR;
    }
    sf64_host_controller(&pads[0].button,&pads[0].stick_x,&pads[0].stick_y);
}
s32 osEepromProbe(OSMesgQueue *queue) {
    u8 block[8];
    (void)queue;
    return sf64_host_eeprom_read(0,block)==0 ? 1 : 0;
}
s32 osEepromRead(OSMesgQueue *queue,u8 block,u8 *data) {
    (void)queue; return sf64_host_eeprom_read(block,data);
}
s32 osEepromWrite(OSMesgQueue *queue,u8 block,u8 *data) {
    (void)queue; return sf64_host_eeprom_write(block,data);
}

int sf64_native_os_self_test(void) {
    static OSMesgQueue queue;
    static OSMesg storage[2];
    OSMesg received=NULL;
    int result=-1;
    OSTime saved=osGetTime();
    osSetTime(12345);
    if (osGetTime()<12345 || osClockRate!=OS_CLOCK_RATE) return -1;
    osSetTime(saved);
    osCreateMesgQueue(&queue,storage,2);
    do {
        if (queue.msgCount!=2 || queue.validCount!=0) break;
        if (osSendMesg(&queue,(OSMesg)0x1234,OS_MESG_NOBLOCK)!=0) break;
        if (osSendMesg(&queue,(OSMesg)0x5678,OS_MESG_NOBLOCK)!=0) break;
        if (queue.validCount!=2) break;
        if (osSendMesg(&queue,NULL,OS_MESG_NOBLOCK)!=-1) break;
        if (osRecvMesg(&queue,&received,OS_MESG_NOBLOCK)!=0 || received!=(OSMesg)0x1234) break;
        if (osRecvMesg(&queue,&received,OS_MESG_NOBLOCK)!=0 || received!=(OSMesg)0x5678) break;
        if (queue.validCount!=0 || osRecvMesg(&queue,NULL,OS_MESG_NOBLOCK)!=-1) break;
        OSContStatus status[4];
        u8 pattern=0;
        if (osContInit(&queue,&pattern,status)!=0 || pattern!=1) break;
        if (status[0].errno || status[0].type!=CONT_TYPE_NORMAL || status[1].errno!=CONT_NO_RESPONSE_ERROR) break;
        result=0;
    } while (0);
    sf64_host_mq_destroy(&queue);
    return result;
}
