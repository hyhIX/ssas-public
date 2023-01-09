/**
 * SSAS - Simple Smart Automotive Software
 * Copyright (C) 2021-2022 Parai Wang <parai@foxmail.com>
 */
/* ================================ [ INCLUDES  ] ============================================== */
#include <pthread.h>
#include <sys/queue.h>

#include "canlib.h"
#include "canlib_types.h"
#include "TcpIp.h"
/* ================================ [ MACROS    ] ============================================== */
#define CAN_MAX_DLEN 64 /* 64 for CANFD */
#define CAN_MTU sizeof(struct can_frame)

#define CAN_CAST_IP TCPIP_IPV4_ADDR(224, 244, 224, 245)
#define CAN_PORT_MIN 8000

#define CAN_FRAME_TYPE_RAW 0
#define CAN_FRAME_TYPE_MTU 1
#define CAN_FRAME_TYPE CAN_FRAME_TYPE_RAW
#if (CAN_FRAME_TYPE == CAN_FRAME_TYPE_RAW)
#define mCANID(frame)                                                                              \
  (((uint32_t)frame.data[CAN_MAX_DLEN + 0] << 24) +                                                \
   ((uint32_t)frame.data[CAN_MAX_DLEN + 1] << 16) +                                                \
   ((uint32_t)frame.data[CAN_MAX_DLEN + 2] << 8) + ((uint32_t)frame.data[CAN_MAX_DLEN + 3]))

#define mSetCANID(frame, canid)                                                                    \
  do {                                                                                             \
    frame.data[CAN_MAX_DLEN + 0] = (uint8_t)(canid >> 24);                                         \
    frame.data[CAN_MAX_DLEN + 1] = (uint8_t)(canid >> 16);                                         \
    frame.data[CAN_MAX_DLEN + 2] = (uint8_t)(canid >> 8);                                          \
    frame.data[CAN_MAX_DLEN + 3] = (uint8_t)(canid);                                               \
  } while (0)

#define mCANDLC(frame) ((uint8_t)frame.data[CAN_MAX_DLEN + 4])
#define mSetCANDLC(frame, dlc)                                                                     \
  do {                                                                                             \
    frame.data[CAN_MAX_DLEN + 4] = dlc;                                                            \
  } while (0)
#else
#define mCANID(frame) frame.can_id

#define mSetCANID(frame, canid)                                                                    \
  do {                                                                                             \
    frame.can_id = canid;                                                                          \
  } while (0)

#define mCANDLC(frame) (frame->can_dlc)
#define mSetCANDLC(frame, dlc)                                                                     \
  do {                                                                                             \
    frame.can_dlc = dlc;                                                                           \
  } while (0)
#endif

/* ================================ [ TYPES     ] ============================================== */
/**
 * struct can_frame - basic CAN frame structure
 * @can_id:  CAN ID of the frame and CAN_*_FLAG flags, see canid_t definition
 * @can_dlc: frame payload length in byte (0 .. 8) aka data length code
 *           N.B. the DLC field from ISO 11898-1 Chapter 8.4.2.3 has a 1:1
 *           mapping of the 'data length code' to the real payload length
 * @data:    CAN frame payload (up to 8 byte)
 */
struct can_frame {
#if (CAN_FRAME_TYPE == CAN_FRAME_TYPE_RAW)
  uint8_t data[CAN_MAX_DLEN + 5];
#else
  uint32_t can_id; /* 32 bit CAN_ID + EFF/RTR/ERR flags */
  uint8_t can_dlc; /* frame payload length in byte (0 .. CAN_MAX_DLEN) */
  uint8_t data[CAN_MAX_DLEN] __attribute__((aligned(8)));
#endif
};
struct Can_socketHandle_s {
  uint32_t busid;
  uint32_t port;
  can_device_rx_notification_t rx_notification;
  TcpIp_SocketIdType sockRd;
  TcpIp_SocketIdType sockWt;
  STAILQ_ENTRY(Can_socketHandle_s) entry;
};
struct Can_socketHandleList_s {
  bool initialized;
  pthread_t rx_thread;
  volatile bool terminated;
  pthread_mutex_t mutex;
  STAILQ_HEAD(, Can_socketHandle_s) head;
};
/* ================================ [ DECLARES  ] ============================================== */
static bool socket_probe(int busid, uint32_t port, uint32_t baudrate,
                         can_device_rx_notification_t rx_notification);
static bool socket_write(uint32_t port, uint32_t canid, uint8_t dlc, const uint8_t *data);
static void socket_close(uint32_t port);
static void *rx_daemon(void *);
/* ================================ [ DATAS     ] ============================================== */
const Can_DeviceOpsType can_simulator_v2_ops = {
  .name = "simulator_v2",
  .probe = socket_probe,
  .close = socket_close,
  .write = socket_write,
};
static struct Can_socketHandleList_s socketH = {
  .initialized = false,
  .terminated = false,
  .mutex = PTHREAD_MUTEX_INITIALIZER,
};
/* ================================ [ LOCALS    ] ============================================== */
static struct Can_socketHandle_s *getHandle(uint32_t port) {
  struct Can_socketHandle_s *handle, *h;
  handle = NULL;

  pthread_mutex_lock(&socketH.mutex);
  STAILQ_FOREACH(h, &socketH.head, entry) {
    if (h->port == port) {
      handle = h;
      break;
    }
  }
  pthread_mutex_unlock(&socketH.mutex);

  return handle;
}

static bool socket_probe(int busid, uint32_t port, uint32_t baudrate,
                         can_device_rx_notification_t rx_notification) {
  bool rv = true;
  struct Can_socketHandle_s *handle;
  TcpIp_SocketIdType sockRd, sockWt;
  Std_ReturnType ret;
  TcpIp_SockAddrType ipv4Addr;
  uint16_t Port;

  pthread_mutex_lock(&socketH.mutex);
  if (false == socketH.initialized) {
    STAILQ_INIT(&socketH.head);
    TcpIp_Init(NULL);
    socketH.initialized = true;
    socketH.terminated = true;
  }
  pthread_mutex_unlock(&socketH.mutex);

  handle = getHandle(port);

  if (handle) {
    ASLOG(WARN, ("CAN socket port=%d is already on-line, no need to probe it again!\n", port));
    rv = false;
  } else {
    sockRd = TcpIp_Create(TCPIP_IPPROTO_UDP);
    if (sockRd >= 0) {
      Port = CAN_PORT_MIN + port;
      ret = TcpIp_Bind(sockRd, TCPIP_LOCALADDRID_ANY, &Port);
      if (E_OK == ret) {
        TcpIp_SetupAddrFrom(&ipv4Addr, CAN_CAST_IP, Port);
        ret = TcpIp_AddToMulticast(sockRd, &ipv4Addr);
      }
      if (E_OK != ret) {
        TcpIp_Close(sockRd, TRUE);
        rv = FALSE;
        ASLOG(ERROR, ("CAN socket bind to %x:%d failed\n", CAN_CAST_IP, CAN_PORT_MIN + port));
      }
    } else {
      rv = FALSE;
      ASLOG(ERROR, ("CAN socket create read sock failed\n"));
    }
    if (rv) {
      sockWt = TcpIp_Create(TCPIP_IPPROTO_UDP);
      if (sockWt < 0) {
        TcpIp_Close(sockRd, TRUE);
        TcpIp_Close(sockWt, TRUE);
        rv = FALSE;
        ASLOG(ERROR, ("CAN socket create write sock failed\n"));
      }
    }
    if (rv) { /* open port OK */
      handle = malloc(sizeof(struct Can_socketHandle_s));
      assert(handle);
      handle->busid = busid;
      handle->port = port;
      handle->rx_notification = rx_notification;
      handle->sockRd = sockRd;
      handle->sockWt = sockWt;
      pthread_mutex_lock(&socketH.mutex);
      STAILQ_INSERT_TAIL(&socketH.head, handle, entry);
      pthread_mutex_unlock(&socketH.mutex);
    } else {
      rv = false;
    }
  }

  pthread_mutex_lock(&socketH.mutex);
  if ((true == socketH.terminated) && (false == STAILQ_EMPTY(&socketH.head))) {
    if (0 == pthread_create(&(socketH.rx_thread), NULL, rx_daemon, NULL)) {
      socketH.terminated = false;
    } else {
      assert(0);
    }
  }
  pthread_mutex_unlock(&socketH.mutex);

  return rv;
}
static bool socket_write(uint32_t port, uint32_t canid, uint8_t dlc, const uint8_t *data) {
  bool rv = true;
  struct can_frame frame;
  TcpIp_SockAddrType RemoteAddr;
  Std_ReturnType ret;
  struct Can_socketHandle_s *handle = getHandle(port);
  if (handle != NULL) {
    mSetCANID(frame, canid);
    mSetCANDLC(frame, dlc);
    assert(dlc <= CAN_MAX_DLEN);
    memcpy(frame.data, data, dlc);
    TcpIp_SetupAddrFrom(&RemoteAddr, CAN_CAST_IP, CAN_PORT_MIN + handle->port);
    ret = TcpIp_SendTo(handle->sockWt, &RemoteAddr, (const uint8_t *)&frame, CAN_MTU);
    if (E_OK != ret) {
      ASLOG(WARN, ("CAN socket port=%d send message failed!\n", port));
      rv = false;
    }
  } else {
    rv = false;
    ASLOG(WARN, ("CAN socket port=%d is not on-line, not able to send message!\n", port));
  }

  return rv;
}
static void socket_close(uint32_t port) {
  struct Can_socketHandle_s *handle = getHandle(port);

  if (NULL != handle) {
    pthread_mutex_lock(&socketH.mutex);
    STAILQ_REMOVE(&socketH.head, handle, Can_socketHandle_s, entry);
    pthread_mutex_unlock(&socketH.mutex);
    TcpIp_Close(handle->sockRd, TRUE);
    TcpIp_Close(handle->sockWt, TRUE);
    free(handle);

    if (true == STAILQ_EMPTY(&socketH.head)) {
      socketH.terminated = true;
      pthread_join(socketH.rx_thread, NULL);
    }
  }
}

static void rx_notifiy(struct Can_socketHandle_s *handle) {
  struct can_frame frame;
  TcpIp_SockAddrType RemoteAddr;
  uint32_t len = sizeof(frame);
  Std_ReturnType ret;
  do {
    ret = TcpIp_RecvFrom(handle->sockRd, &RemoteAddr, (uint8_t *)&frame, &len);
    if ((E_OK == ret) && (len == sizeof(frame))) {
      handle->rx_notification(handle->busid, mCANID(frame), mCANDLC(frame), frame.data);
    } else {
      ret = E_NOT_OK;
    }
  } while (E_OK == ret);
}

static void *rx_daemon(void *param) {
  (void)param;
  struct Can_socketHandle_s *handle;
  while (false == socketH.terminated) {
    pthread_mutex_lock(&socketH.mutex);
    STAILQ_FOREACH(handle, &socketH.head, entry) {
      rx_notifiy(handle);
    }
    pthread_mutex_unlock(&socketH.mutex);
    usleep(1000);
  }

  return NULL;
}
/* ================================ [ FUNCTIONS ] ============================================== */