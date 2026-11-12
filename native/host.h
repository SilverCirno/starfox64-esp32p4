#pragma once
#ifdef __cplusplus
extern "C" {
#endif
unsigned long long sf64_host_time_us(void);
int sf64_host_self_test(void);
int sf64_native_os_self_test(void);
int sf64_host_mq_create(void *queue,int capacity);
int sf64_host_mq_send(void *queue,void *message,int block);
int sf64_host_mq_recv(void *queue,void **message,int block);
int sf64_host_mq_count(void *queue);
int sf64_host_mq_destroy(void *queue);
void sf64_host_controller(unsigned short *buttons,signed char *x,signed char *y);
int sf64_host_eeprom_read(unsigned block,unsigned char *data);
int sf64_host_eeprom_write(unsigned block,const unsigned char *data);
#ifdef __cplusplus
}
#endif
