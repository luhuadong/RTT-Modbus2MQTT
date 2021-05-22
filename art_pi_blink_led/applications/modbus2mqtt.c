/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2021-03-25     Administrator       the first version
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "drv_common.h"

#include "mb.h"
#include "mb_m.h"
#include "user_mb_app.h"

#include "mqttclient.h"
#include "ftp.h"
#include "netdev_ipaddr.h"
#include "netdev.h"

#include <dfs.h>
#include <dfs_posix.h>
#include "dfs_private.h"

#define SLAVE_ADDR      0x01        // modbus 浠庢満鐨勫湴鍧�
#define PORT_NUM        0x01        // 涓插彛鍙�
#define PORT_BAUDRATE   115200      // 涓插彛鐨勬尝鐗圭巼
#define PORT_PARITY     MB_PAR_EVEN // 涓插彛鐨勬牎楠屼綅

#define MB_POLL_THREAD_PRIORITY  10 // modbus poll 绾跨▼鐨勪紭鍏堢骇
#define MB_SEND_THREAD_PRIORITY  RT_THREAD_PRIORITY_MAX - 1 // modbus 璇诲啓鏁版嵁绾跨▼鐨勪紭鍏堢骇

#define MB_SEND_REG_START  2  // modbus 鍐欐暟鎹殑璧峰鍦板潃
#define MB_SEND_REG_NUM    2  // modbus 鍐欐暟鎹殑闀垮害
#define MB_POLL_CYCLE_MS   1  // modbus poll 绾跨▼鐨勮疆璁懆鏈�
#define MB_RECV_REG_START  0  // modbus 璇绘暟鎹殑璧峰鍦板潃
#define MB_RECV_REG_NUM    5  // modbus 璇绘暟鎹殑闀垮害
#define MB_MQTT_REG_START  5  // modbus 鎺ユ敹 mqtt 鏁版嵁鐨勮捣濮嬪湴鍧�

#define LED_PIN_RED GET_PIN(C, 15) // 绾㈣壊 LED 鐨勫紩鑴氬彿

#define WIFI_SSID  "mnano-5f"     // wifi 鐨� SSID
#define WIFI_KEY   "mnano-5f" // wifi 鐨� 瀵嗙爜

#define SUB1_NAME    "/sub/rudy/led"    // 璁㈤槄鐨勪富棰橈紝璇峰皢 xxx 淇敼涓鸿嚜宸变釜浜虹殑鍞竴鏍囪瘑绗︼紝鐗㈣杩欎釜鍚嶅瓧锛孭C 涓婄殑娴嬭瘯瀹㈡埛绔淇濇寔涓�鑷�
#define SUB2_NAME    "/sub/rudy/num"    // 璁㈤槄鐨勪富棰橈紝璇峰皢 xxx 淇敼涓鸿嚜宸变釜浜虹殑鍞竴鏍囪瘑绗︼紝鐗㈣杩欎釜鍚嶅瓧锛孭C 涓婄殑娴嬭瘯瀹㈡埛绔淇濇寔涓�鑷�
#define PUB_NAME     "/pub/rudy/modbus" // 鍙戝竷鐨勪富棰橈紝璇峰皢 xxx 淇敼涓鸿嚜宸变釜浜虹殑鍞竴鏍囪瘑绗︼紝鐗㈣杩欎釜鍚嶅瓧锛孭C 涓婄殑娴嬭瘯瀹㈡埛绔淇濇寔涓�鑷�

/* 濡傛灉鏈嶅姟鍣ㄨ繛鎺ヤ笉涓婏紝璇锋洿鎹㈠叾浠栨湇鍔″櫒 */
//#define MQTT_URL  "115.159.217.166"
//#define MQTT_PORT "1883"
//#define MQTT_URL  "119.23.105.150"
//#define MQTT_PORT "8002"
#define MQTT_URL  "jiejie01.top"
#define MQTT_PORT "1883"

extern USHORT   usMRegHoldBuf[MB_MASTER_TOTAL_SLAVE_NUM][M_REG_HOLDING_NREGS]; // modbus master 鐢ㄥ叏灞�浜岀淮鏁扮粍鏉ヨ褰曞涓粠鏈虹殑鏁版嵁

static mqtt_client_t *client = NULL;
static char cid[30] = { 0 };
static struct netdev *net_dev;
static int is_started = 0;
static USHORT num_data = 0;

static struct rt_semaphore sem_demo;
static struct rt_messagequeue mq_demo;

static rt_uint8_t msg_pool[2048];
/*
 * 鏂囦欢淇濆瓨涓� csv 鏍煎紡
 * | 鏃堕棿鎴� | 鍛戒护瀛� | 鐘舵�� |
 * | 123456 |  led   |  on  |
 * | 234567 |  led   |  off |
 * | 345678 |  num   |  123 |
 * */
struct file_msg
{
    char *timestamp; // tick 鏃堕棿鎴�
    char *cmd;       // 鍛戒护瀛�
    char *state;     // 鐘舵�佸瓧
    char **str;
};

typedef struct {
    char **str;     //the PChar of string array
    size_t num;     //the number of string
}IString;

/* 鎷嗗垎瀛楃涓� */
static int Split(char *src, char *delim, IString* istr)//split buf
{
    int i;
    char *str = NULL, *p = NULL;

    (*istr).num = 1;
    str = (char*)rt_calloc(strlen(src)+1,sizeof(char));
    if (str == NULL) return 0;
    (*istr).str = (char**)rt_calloc(1,sizeof(char *));
    if ((*istr).str == NULL) return 0;
    strcpy(str,src);

    p = strtok(str, delim);
    (*istr).str[0] = (char*)rt_calloc(strlen(p)+1,sizeof(char));
    if ((*istr).str[0] == NULL) return 0;
    strcpy((*istr).str[0],p);
    for(i = 1; (p = strtok(NULL, delim)); i++)
    {
        (*istr).num++;
        (*istr).str = (char**)rt_realloc((*istr).str,(i+1)*sizeof(char *));
        if ((*istr).str == NULL) return 0;
        (*istr).str[i] = (char*)rt_calloc(strlen(p)+1,sizeof(char));
        if ((*istr).str[0] == NULL) return 0;
        strcpy((*istr).str[i],p);
    }
    rt_free(str);
    str = p = NULL;

    return 1;
}

/* modbus master 璇诲啓鏁版嵁绾跨▼ */
static void send_thread_entry(void *parameter)
{
    eMBMasterReqErrCode error_code = MB_MRE_NO_ERR;
    rt_uint16_t error_count = 0;
    USHORT data[2] = {0};

    rt_thread_mdelay(RT_TICK_PER_SECOND);

    while (1)
    {
        /* Test Modbus Master */
        data[0] = (USHORT)(rt_tick_get() / 10);
        data[1] = (USHORT)(rt_tick_get() % 10);

        error_code = eMBMasterReqWriteMultipleHoldingRegister(SLAVE_ADDR,          /* salve address */
                                                              MB_SEND_REG_START,   /* register start address */
                                                              MB_SEND_REG_NUM,     /* register total number */
                                                              data,                /* data to be written */
                                                              RT_TICK_PER_SECOND/100); /* timeout */
        rt_thread_mdelay(MB_POLL_CYCLE_MS*50);
        error_code = eMBMasterReqReadHoldingRegister(SLAVE_ADDR,          /* salve address */
                                                     MB_RECV_REG_START,
                                                     MB_RECV_REG_NUM + 1,
                                                     RT_TICK_PER_SECOND/100);
        rt_thread_mdelay(MB_POLL_CYCLE_MS*50);
        if (rt_sem_trytake(&sem_demo) == RT_EOK) {
            eMBMasterReqWriteMultipleHoldingRegister(SLAVE_ADDR,        /* salve address */
                                                     MB_MQTT_REG_START, /* register start address */
                                                     0x01,              /* register total number */
                                                     &num_data,         /* data to be written */
                                                     RT_TICK_PER_SECOND/100);
            rt_thread_mdelay(MB_POLL_CYCLE_MS*100);
        }

        /* Record the number of errors */
        if (error_code != MB_MRE_NO_ERR)
        {
            error_count++;
        }
    }
}

/* modbus master 杞绾跨▼ */
static void mb_master_poll(void *parameter)
{
    eMBMasterInit(MB_RTU, PORT_NUM, PORT_BAUDRATE, PORT_PARITY);
    eMBMasterEnable();
    rt_pin_mode(LED_PIN_RED, PIN_MODE_OUTPUT);
    rt_pin_write(LED_PIN_RED, PIN_HIGH);

    while (1)
    {
        eMBMasterPoll();
        rt_thread_mdelay(MB_POLL_CYCLE_MS);
    }
}

/* 鍒ゆ柇鎺ユ敹鍒扮殑鏄瓧绗﹁繕鏄暟瀛� : 鏁板瓧杩斿洖1, 瀛楃杩斿洖 0*/
static int char_or_num(char *str, int len)
{
    for(int j = 0; j < len; j++)
    {
        if(!(str[j] >= '0' && str[j] <='9'))
        {
            return 1;
        }
    }
    return 0;
}

static void sub_topic_handle_num(void* client, message_data_t* msg)
{
    (void) client;
    KAWAII_MQTT_LOG_I("-----------------------------------------------------------------------------------\r\n");
    KAWAII_MQTT_LOG_I("%s:%d %s()...\ntopic: %s\nmessage:%s\r\n", __FILE__, __LINE__, __FUNCTION__, msg->topic_name, (char*)msg->message->payload);
    KAWAII_MQTT_LOG_I("-----------------------------------------------------------------------------------\r\n");

    int i;
    IString istr;
    struct file_msg file_msg;
    char *tick_num;

    if (Split(msg->message->payload," ",&istr))
    {
        for (i = 0; i < istr.num; i++)
            rt_kprintf("%s\n",istr.str[i]);

        if(istr.num == 2)
        {
            if(rt_strncmp(istr.str[0], "num", 3))
            {
                KAWAII_MQTT_LOG_I("command num error!\r\n");
            }
            else
            {
                if(char_or_num(istr.str[1], rt_strlen(istr.str[1])))
                {
                    KAWAII_MQTT_LOG_E("A string contains letters!\r\n");
                }
                else
                {
                    num_data = atoi(istr.str[1]);
                    rt_sem_release(&sem_demo);

                    tick_num = rt_calloc(1, 10);
                     if (tick_num == RT_NULL) {
                         rt_kprintf("memory is not enough \r\n");
                         for (i = 0; i < istr.num; i++)
                             rt_free(istr.str[i]);
                         rt_free(istr.str);
                     }
                     else {
                         itoa(rt_tick_get(), tick_num, 10);
                         file_msg.timestamp = tick_num;
                         file_msg.cmd = istr.str[0];
                         file_msg.state = istr.str[1];
                         file_msg.str = istr.str;

                         rt_mq_send(&mq_demo, &file_msg, sizeof(file_msg));
                     }
                }
            }
        }
        else {
            KAWAII_MQTT_LOG_E("command error! \r\n");
            for (i = 0; i < istr.num; i++)
                rt_free(istr.str[i]);
            rt_free(istr.str);
        }
    }
    else
    {
        KAWAII_MQTT_LOG_E("Split failure!\r\n");
    }
}

static void sub_topic_handle_led(void* client, message_data_t* msg)
{
    (void) client;
    KAWAII_MQTT_LOG_I("-----------------------------------------------------------------------------------\r\n");
    KAWAII_MQTT_LOG_I("%s:%d %s()...\ntopic: %s\nmessage:%s\r\n", __FILE__, __LINE__, __FUNCTION__, msg->topic_name, (char*)msg->message->payload);
    KAWAII_MQTT_LOG_I("-----------------------------------------------------------------------------------\r\n");

    int i;
    IString istr;
    struct file_msg file_msg;
    char *tick_num;

    if (Split(msg->message->payload," ",&istr))
    {
        for (i = 0; i < istr.num; i++)
            rt_kprintf("%s\n",istr.str[i]);

        if(i == 2)
        {
            if(rt_strncmp(istr.str[0], "led", 3))
            {
                KAWAII_MQTT_LOG_E("command error!\r\n");
            }
            else
            {
                if(!(rt_strncmp(istr.str[1], "off", 3)))
                {
                    rt_pin_write(LED_PIN_RED, PIN_HIGH);
                }
                else if(!(rt_strncmp(istr.str[1], "on", 2)))
                {
                    rt_pin_write(LED_PIN_RED, PIN_LOW);
                }
                else
                {
                    KAWAII_MQTT_LOG_E("state command error!\r\n");
                    for (i = 0; i < istr.num; i++)
                        rt_free(istr.str[i]);
                    rt_free(istr.str);
                    return;
                }

                tick_num = rt_calloc(1, 10);
                if (tick_num == RT_NULL) {
                    rt_kprintf("memory is not enough \r\n");
                }
                else {
                    itoa(rt_tick_get(), tick_num, 10);
                    file_msg.timestamp = tick_num;
                    file_msg.cmd = istr.str[0];
                    file_msg.state = istr.str[1];
                    file_msg.str = istr.str;

                    rt_mq_send(&mq_demo, &file_msg, sizeof(file_msg));
                }
            }
        }
        else
        {
            KAWAII_MQTT_LOG_E("command error! \r\n");
            for (i = 0; i < istr.num; i++)
                rt_free(istr.str[i]);
            rt_free(istr.str);
        }

    }
    else
    {
        KAWAII_MQTT_LOG_E("Split failure!\r\n");
    }
}

/* 淇濆瓨 MQTT 涓嬪彂鐨勬暟鎹� */
static void file_save(void *parameter)
{
    struct file_msg file_msg;
    rt_err_t result;
    char *write_buf;
    int fd;

    while(1)
    {
        result = rt_mq_recv(&mq_demo, &file_msg, sizeof(file_msg), RT_WAITING_FOREVER);
        if (result == RT_EOK)
        {
            write_buf = rt_calloc(1, 100);
            if (write_buf == RT_NULL) {
                rt_kprintf("memory is not enough \r\n");

                rt_free(file_msg.timestamp);
                rt_free(file_msg.cmd);
                rt_free(file_msg.state);
            }
            else {
            int ret = rt_snprintf(write_buf, 100, "%s%s%s%s%s%s",file_msg.timestamp,",",file_msg.cmd,",",file_msg.state,"\n");
            if(ret > 0)
            {
                fd = open("/flash/demo.csv", O_RDWR | O_APPEND);
                if (fd >= 0) {
                    write(fd, write_buf, strlen(write_buf));
                    close(fd);
                }
            }
            rt_free(write_buf);
            rt_free(file_msg.timestamp);
            rt_free(file_msg.cmd);
            rt_free(file_msg.state);
            rt_free(file_msg.str);
            }
        }
    }
}

/* 鏉垮瓙鍙戝竷娑堟伅鐨勭嚎绋� */
static void mqtt_t_publish(void *parameter)
{
    char pub_buf[64];
    mqtt_message_t msg;
    memset(&msg, 0, sizeof(msg));

    rt_thread_mdelay(2 * RT_TICK_PER_SECOND);

    while(1)
    {
        if(is_started)
        {
            memset(pub_buf, 0, 64);

            sprintf(pub_buf, "%d", usMRegHoldBuf[SLAVE_ADDR - 1][MB_RECV_REG_NUM]);

            msg.qos = QOS0;
            msg.payload = pub_buf;

            mqtt_publish(client, PUB_NAME, &msg);
        }
        rt_thread_mdelay(5 * RT_TICK_PER_SECOND);
    }
}

/* 鍚姩 demo 娴嬭瘯绋嬪簭 */
static int modbus2mqtt(int argc,char **argv)
{
    rt_thread_delay(10000);
    rt_uint32_t timeout = 0;
    rt_thread_t tid_file = RT_NULL;
    int  fd;
    char file_head[] = "time_stamp,cmd,state\n";

    rt_sem_init(&sem_demo, "sem_demo", 0, RT_IPC_FLAG_FIFO);
    rt_mq_init(&mq_demo, "mq_demo", &msg_pool, sizeof(struct file_msg), sizeof(msg_pool),RT_IPC_FLAG_FIFO);

    /* 1. led initialization */
    rt_pin_mode(LED_PIN_RED, PIN_MODE_OUTPUT);

//    /* 2. ftp server initialization */
//    ftp_init(2048, 27, 100);

    /* 3. The file system has been automatically mounted */
    fd = open("/flash/demo.csv", O_WRONLY | O_APPEND);
    if(fd < 0)
    {
        rt_kprintf("The file does not exist, recreate the file\r\n");
        fd = open("/flash/demo.csv", O_WRONLY | O_CREAT | O_APPEND);
        if(fd < 0)
        {
            rt_kprintf("File system initialization failed\r\n");
        }
        else {
                write(fd, file_head, strlen(file_head));
                close(fd);
        }
    }
    else {
        close(fd);
    }

    tid_file = rt_thread_create("file_save", file_save, RT_NULL, 1024*4, 6, 10);
    if (tid_file != RT_NULL)
    {
        rt_thread_startup(tid_file);
    }
    else
    {
        goto __exit;
    }

    /* 4. modbus master initialization */
    static rt_uint8_t is_init = 0;
    rt_thread_t tid1 = RT_NULL, tid2 = RT_NULL, tid3 = RT_NULL;

    if (is_init > 0)
    {
        rt_kprintf("sample is running\n");
        return -RT_ERROR;
    }
    tid1 = rt_thread_create("md_m_poll", mb_master_poll, RT_NULL, 512, 3, 10);
    if (tid1 != RT_NULL)
    {
        rt_thread_startup(tid1);
    }
    else
    {
        goto __exit;
    }

    tid2 = rt_thread_create("md_m_send", send_thread_entry, RT_NULL, 512, MB_SEND_THREAD_PRIORITY, 10);
    if (tid2 != RT_NULL)
    {
        rt_thread_startup(tid2);
    }
    else
    {
        goto __exit;
    }

    is_init = 1;

    /* 5. wifi connect */
    rt_wlan_connect(WIFI_SSID, WIFI_KEY);

    /* 6. startup mqtt client  */
    mqtt_log_init();
    rt_snprintf(cid, sizeof(cid), "rtthread%d", rt_tick_get());
    /* check network connection status */

    net_dev = netdev_get_by_name("w0");
    while(!(netdev_is_internet_up(net_dev)))
    {
        rt_thread_mdelay(100);
        timeout++;
        if(timeout == 200)
        {
            rt_kprintf("wifi connect failed!\r\n");
            return -RT_ERROR;
        }
    }

    client = mqtt_lease();

    mqtt_set_host(client, MQTT_URL);
    mqtt_set_port(client, MQTT_PORT);
    mqtt_set_user_name(client, "rt-thread");
    mqtt_set_password(client, "rt-thread");
    mqtt_set_client_id(client, cid);
    mqtt_set_clean_session(client, 1);

    if(mqtt_connect(client))
    {
        KAWAII_MQTT_LOG_E("%s:%d %s()... mqtt connect failed...", __FILE__, __LINE__, __FUNCTION__);
        is_started = 0;
        return -RT_ERROR;
    }

    is_started = 1;
    mqtt_subscribe(client, SUB1_NAME, QOS0, sub_topic_handle_led);
    mqtt_subscribe(client, SUB2_NAME, QOS1, sub_topic_handle_num);

    tid3 = rt_thread_create("mq_pub", mqtt_t_publish, RT_NULL, 2048, 13, 10);
    if (tid3 != RT_NULL)
    {
        rt_thread_startup(tid3);
    }
    else
    {
        goto __exit;
    }

    return RT_EOK;

__exit:
    if (tid_file)
        rt_thread_delete(tid_file);
    if (tid1)
        rt_thread_delete(tid1);
    if (tid2)
        rt_thread_delete(tid2);
    if (tid3)
        rt_thread_delete(tid3);

    return -RT_ERROR;
}
MSH_CMD_EXPORT(modbus2mqtt,modbus to mqtt);
INIT_APP_EXPORT(modbus2mqtt);


static int mqtt_recon(int argc,char **aggv)
{
    if(!(netdev_is_internet_up(net_dev)))
    {
        rt_kprintf("wifi connect failed!\r\n");
        return -RT_ERROR;
    }

    if(is_started)
    {
        return -RT_ERROR;
    }

    client = mqtt_lease();

    mqtt_set_host(client, MQTT_URL);
    mqtt_set_port(client, MQTT_PORT);
    mqtt_set_user_name(client, "rt-thread");
    mqtt_set_password(client, "rt-thread");
    mqtt_set_client_id(client, cid);
    mqtt_set_clean_session(client, 1);

    if(mqtt_connect(client))
    {
        KAWAII_MQTT_LOG_E("%s:%d %s()... mqtt connect failed...", __FILE__, __LINE__, __FUNCTION__);
        is_started = 0;
        return -RT_ERROR;
    }
    is_started = 1;

    mqtt_subscribe(client, SUB1_NAME, QOS0, sub_topic_handle_led);
    mqtt_subscribe(client, SUB2_NAME, QOS1, sub_topic_handle_num);

    return RT_EOK;
}
MSH_CMD_EXPORT(mqtt_recon,mqtt reconnect);

#include <fal.h>

#define RBL_BUFFSZ 4096
#define RBL_PART   "download"
static uint8_t rbl_buff[RBL_BUFFSZ];
static char rbl_name[32];

int upgrade(int argc, char **argv)
{
    int fd, len,res;
    uint32_t addr = 0;

    const struct fal_partition *fal_part = RT_NULL;

    if (argc != 2) {
        rt_kprintf("Command error ! Please run 'upgrade app'\r\n");
        return -RT_ERROR;
    }

    rt_memset(rbl_name, 0, 32);

    rt_snprintf(rbl_name, sizeof(rbl_name), "/flash/%s.rbl", argv[1]);

    fd = open(rbl_name, O_RDONLY);
    if(fd < 0)
    {
        rt_kprintf("Upgrade file not found !\r\n");
        return -RT_ERROR;
    }

    fal_part = fal_partition_find(RBL_PART);
    if (fal_part == RT_NULL) {
        rt_kprintf("The partition %s was not found !\r\n", RBL_PART);
        return -RT_ERROR;
    }

    rt_kprintf("Writing file !\r\n");

    while(1)
    {
        len = read(fd, rbl_buff, RBL_BUFFSZ);
        if(len < 0)
        {
            rt_kprintf("File read failed !\r\n");
            return -RT_ERROR;
        }
        else if(len == 0)
        {
            break;
        }

        res = fal_partition_erase(fal_part, addr, RBL_BUFFSZ);
        if(res < 0)
        {
            rt_kprintf("Erase to %s partition failed !\r\n", RBL_PART);
            return -RT_ERROR;
        }

        res = fal_partition_write(fal_part, addr, rbl_buff, len);
        if(res < 0)
        {
            rt_kprintf("Write to %s partition failed !\r\n", RBL_PART);
            return -RT_ERROR;
        }

        addr += len;

        rt_kprintf(">");
    }

    close(fd);
    rt_kprintf("\r\n");
    rt_kprintf("End of file write !\r\n");
    rt_hw_cpu_reset();
    /* wait some time for terminal response finish */
    rt_thread_delay(rt_tick_from_millisecond(200));

    return RT_EOK;
}
MSH_CMD_EXPORT(upgrade, e g : upgrade app);
