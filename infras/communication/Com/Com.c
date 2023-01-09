/**
 * SSAS - Simple Smart Automotive Software
 * Copyright (C) 2021 Parai Wang <parai@foxmail.com>
 *
 * ref: Specification of Communication AUTOSAR CP Release 4.4.0
 */
/* ================================ [ INCLUDES  ] ============================================== */
#include "Com.h"
#include "Com_Cfg.h"
#include "Com_Priv.h"
#include "PduR_Com.h"
#include "Std_Bit.h"
#include <string.h>
#ifdef USE_SHELL
#include "shell.h"
#endif
/* ================================ [ MACROS    ] ============================================== */
#define COM_CONFIG (&Com_Config)
/* ================================ [ TYPES     ] ============================================== */
/* ================================ [ DECLARES  ] ============================================== */
extern const Com_ConfigType Com_Config;
/* ================================ [ DATAS     ] ============================================== */
/* ================================ [ LOCALS    ] ============================================== */
Std_ReturnType comStoreSignalValue(const Com_SignalConfigType *signal, uint32_t sigV,
                                   void *SignalDataPtr) {
  Std_ReturnType ret = E_OK;
  uint32_t mask, signmask;
  mask = 0xFFFFFFFFu >> (32 - signal->BitSize); /* calculate mask for SigVal */
  sigV &= mask;                                 /* clear bit out of range */
  signmask = ~(mask >> 1);
  switch (signal->type) {
  case COM_SINT8:
    if (sigV & signmask) {
      sigV |= signmask; /* add sign bits */
    }
    *(int8_t *)SignalDataPtr = (int8_t)sigV;
    break;
  case COM_UINT8:
    *(uint8_t *)SignalDataPtr = (uint8_t)sigV;
    break;
  case COM_SINT16:
    if (sigV & signmask) {
      sigV |= signmask; /* add sign bits */
    }
    *(int16_t *)SignalDataPtr = (int16_t)sigV;
    break;
  case COM_UINT16:
    *(uint16_t *)SignalDataPtr = (uint16_t)sigV;
    break;
  case COM_SINT32:
    if (sigV & signmask) {
      sigV |= signmask; /* add sign bits */
    }
    *(int32_t *)SignalDataPtr = (int32_t)sigV;
    break;
  case COM_UINT32:
    *(uint32_t *)SignalDataPtr = (uint32_t)sigV;
    break;
  default:
    ret = E_NOT_OK;
    break;
  }
  return ret;
}

Std_ReturnType comGetSignalValue(const Com_SignalConfigType *signal, uint32_t *sigV,
                                 const void *SignalDataPtr) {
  Std_ReturnType ret = E_OK;
  switch (signal->type) {
  case COM_SINT8:
  case COM_UINT8:
    *sigV = *(uint8_t *)SignalDataPtr;
    break;
  case COM_SINT16:
  case COM_UINT16:
    *sigV = *(uint16_t *)SignalDataPtr;
    break;
  case COM_SINT32:
  case COM_UINT32:
    *sigV = *(uint32_t *)SignalDataPtr;
    break;
  default:
    ret = E_NOT_OK;
    break;
  }

  return ret;
}

Std_ReturnType comReceiveSignalBig(const Com_SignalConfigType *signal, void *SignalDataPtr) {
  uint32_t sigV = Std_BitGetBigEndian(signal->ptr, signal->BitPosition, signal->BitSize);
  return comStoreSignalValue(signal, sigV, SignalDataPtr);
}

Std_ReturnType comSendSignalBig(const Com_SignalConfigType *signal, const void *SignalDataPtr) {
  uint32_t sigV;
  Std_ReturnType ret = comGetSignalValue(signal, &sigV, SignalDataPtr);

  if (E_OK == ret) {
    Std_BitSetBigEndian(signal->ptr, sigV, signal->BitPosition, signal->BitSize);
  }

  return ret;
}

Std_ReturnType comReceiveSignalLittle(const Com_SignalConfigType *signal, void *SignalDataPtr) {
  uint32_t sigV = Std_BitGetLittleEndian(signal->ptr, signal->BitPosition, signal->BitSize);
  return comStoreSignalValue(signal, sigV, SignalDataPtr);
}

Std_ReturnType comSendSignalLittle(const Com_SignalConfigType *signal, const void *SignalDataPtr) {
  uint32_t sigV;
  Std_ReturnType ret = comGetSignalValue(signal, &sigV, SignalDataPtr);

  if (E_OK == ret) {
    Std_BitSetLittleEndian(signal->ptr, sigV, signal->BitPosition, signal->BitSize);
  }

  return ret;
}

Std_ReturnType comReceiveSignal(const Com_SignalConfigType *signal, void *SignalDataPtr) {
  Std_ReturnType ret = E_NOT_OK;
#ifdef COM_USE_SIGNAL_UPDATE_BIT
  boolean isUpdated;
  if (signal->UpdateBit != COM_UPDATE_BIT_NOT_USED) {
    isUpdated = Std_BitGet(signal->ptr, signal->UpdateBit);
    if (FALSE == isUpdated) {
      return E_NOT_OK;
    } else {
      Std_BitClear(signal->ptr, signal->UpdateBit);
    }
  }
#endif
  if ((COM_UINT8N == signal->type) || (OPAQUE == signal->Endianness)) {
    /* @SWS_Com_00472 */
    memcpy(SignalDataPtr, signal->ptr, (signal->BitSize >> 3));
    ret = E_OK;
  } else {
    switch (signal->Endianness) {
    case BIG:
      ret = comReceiveSignalBig(signal, SignalDataPtr);
      break;
    case LITTLE:
      ret = comReceiveSignalLittle(signal, SignalDataPtr);
      break;
    default:
      break;
    }
  }
  return ret;
}

Std_ReturnType comSendSignal(const Com_SignalConfigType *signal, const void *SignalDataPtr) {
  Std_ReturnType ret = E_NOT_OK;
  if ((COM_UINT8N == signal->type) || (OPAQUE == signal->Endianness)) {
    /* @SWS_Com_00472 */
    memcpy(signal->ptr, SignalDataPtr, (signal->BitSize >> 3));
    ret = E_OK;
  } else {
    switch (signal->Endianness) {
    case BIG:
      ret = comSendSignalBig(signal, SignalDataPtr);
      break;
    case LITTLE:
      ret = comSendSignalLittle(signal, SignalDataPtr);
      break;
    default:
      break;
    }
  }
#ifdef COM_USE_SIGNAL_UPDATE_BIT
  if (signal->UpdateBit != COM_UPDATE_BIT_NOT_USED) {
    Std_BitSet(signal->ptr, signal->UpdateBit);
  }
#endif
  return ret;
}
void comIPduDataInit(const Com_IPduConfigType *IPduConfig) {
  const Com_SignalConfigType *signal;
  int i;
  for (i = 0; i < IPduConfig->numOfSignals; i++) {
    signal = IPduConfig->signals[i];
    comSendSignal(signal, signal->initPtr);
  }
}
#ifdef COM_USE_SIGNAL_UPDATE_BIT
void comTxClearUpdateBit(const Com_IPduConfigType *IPduConfig) {
  const Com_SignalConfigType *signal;
  int i;
  for (i = 0; i < IPduConfig->numOfSignals; i++) {
    signal = IPduConfig->signals[i];
    if (signal->UpdateBit != COM_UPDATE_BIT_NOT_USED) {
      Std_BitClear(signal->ptr, signal->UpdateBit);
    }
  }
}
#endif

#ifdef USE_SHELL
static int cmdComLsSgFunc(int argc, const char *argv[]) {
  union {
    uint32_t u32V;
    uint16_t u16V;
    uint8_t u8V;
  } uV;
  const Com_IPduConfigType *IPdu;
  const Com_SignalConfigType *signal;
  int i, j;
  for (i = 0; i < COM_CONFIG->numOfIPdus; i++) {
    IPdu = &COM_CONFIG->IPduConfigs[i];
    if (IPdu->rxConfig) {
      for (j = 0; j < IPdu->numOfSignals; j++) {
        signal = IPdu->signals[j];
        if (signal->isGroupSignal) {
          Com_ReceiveSignalGroup(signal->HandleId);
        }
      }
    }
    for (j = 0; j < IPdu->numOfSignals; j++) {
      signal = IPdu->signals[j];

      printf("%s ", (IPdu->txConfig) ? "T" : "R");
      if (signal->isGroupSignal) {
        printf("%s.%s(GID=%d) is group signal\n", IPdu->name, signal->name, signal->HandleId);
        continue;
      }
      switch (signal->type) {
      case COM_UINT8:
      case COM_SINT8:
        (void)Com_ReceiveSignal(signal->HandleId, &uV.u8V);
        printf("%s.%s(SID=%d): V = 0x%02X(%d)\n", IPdu->name, signal->name, signal->HandleId,
               uV.u8V, uV.u8V);
        break;
      case COM_UINT16:
      case COM_SINT16:
        (void)Com_ReceiveSignal(signal->HandleId, &uV.u16V);
        printf("%s.%s(SID=%d): V = 0x%04X(%d)\n", IPdu->name, signal->name, signal->HandleId,
               uV.u16V, uV.u16V);
        break;
      case COM_UINT32:
      case COM_SINT32:
        (void)Com_ReceiveSignal(signal->HandleId, &uV.u32V);
        printf("%s.%s(SID=%d): V = 0x%08X(%d)\n", IPdu->name, signal->name, signal->HandleId,
               uV.u32V, uV.u32V);
        break;
      default:
        printf("%s.%s(SID=%d): unsupported type %d\n", IPdu->name, signal->name, signal->HandleId,
               signal->type);
        break;
      }
    }
  }
  return 0;
}
static int cmdComWrSgFunc(int argc, const char *argv[]) {
  Com_SignalIdType sid, gid = -1;
  uint32_t u32V;
  union {
    uint32_t u32V;
    uint16_t u16V;
    uint8_t u8V;
  } uV;
  const Com_SignalConfigType *signal;

  if (argc < 3) {
    return -1;
  }

  sid = strtoul(argv[1], NULL, 10);
  if (sid >= COM_CONFIG->numOfSignals) {
    return -2;
  }
  if (0 == strncmp("0x", argv[2], 2)) {
    u32V = strtoul(argv[2], NULL, 16);
  } else {
    u32V = strtoul(argv[2], NULL, 10);
  }
  if (argc >= 4) {
    gid = strtoul(argv[3], NULL, 10);
    if (gid >= COM_CONFIG->numOfSignals) {
      return -3;
    }
  }

  signal = &COM_CONFIG->SignalConfigs[sid];
  switch (signal->type) {
  case COM_UINT8:
  case COM_SINT8:
    uV.u8V = (uint8)u32V;
    (void)Com_SendSignal(signal->HandleId, &uV.u8V);
    break;
  case COM_UINT16:
  case COM_SINT16:
    uV.u16V = (uint16)u32V;
    (void)Com_SendSignal(signal->HandleId, &uV.u16V);
    break;
  case COM_UINT32:
  case COM_SINT32:
    uV.u32V = (uint32)u32V;
    (void)Com_SendSignal(signal->HandleId, &uV.u32V);
    break;
  default:
    break;
  }

  if (gid > 0) {
    Com_SendSignalGroup(gid);
  }

  return 0;
}
SHELL_REGISTER(lssg, "list all the value of com signals", cmdComLsSgFunc);
SHELL_REGISTER(wrsg,
               "wrsg sid value [gid]\n"
               "  write signal, if sid is group signals, need the gid",
               cmdComWrSgFunc);
#endif
/* ================================ [ FUNCTIONS ] ==============================================
 */
void Com_Init(const Com_ConfigType *config) {
  COM_CONFIG->context->GroupStatus = 0;
}

void Com_IpduGroupStart(Com_IpduGroupIdType IpduGroupId, boolean initialize) {
  const Com_IPduConfigType *IPduConfig;
  int i;
  if (IpduGroupId < COM_CONFIG->numOfGroups) {
    COM_CONFIG->context->GroupStatus |= (1 << IpduGroupId);
    for (i = 0; i < COM_CONFIG->numOfIPdus; i++) {
      IPduConfig = &COM_CONFIG->IPduConfigs[i];
      if (IPduConfig->GroupRefMask & (1 << IpduGroupId)) {
        if (initialize) {
          comIPduDataInit(IPduConfig);
        }
        if (IPduConfig->rxConfig) {
          if (IPduConfig->rxConfig->FirstTimeout > 0) {
            IPduConfig->rxConfig->context->timer = IPduConfig->rxConfig->FirstTimeout;
          } else {
            IPduConfig->rxConfig->context->timer = IPduConfig->rxConfig->Timeout;
          }
        } else if (IPduConfig->txConfig) {
          if (IPduConfig->txConfig->FirstTime > 0) {
            IPduConfig->txConfig->context->timer = IPduConfig->txConfig->FirstTime;
          } else {
            IPduConfig->txConfig->context->timer = IPduConfig->txConfig->CycleTime;
          }
        } else {
          /* do nothing */
        }
      }
    }
  }
}

void Com_IpduGroupStop(Com_IpduGroupIdType IpduGroupId) {
  if (IpduGroupId < COM_CONFIG->numOfGroups) {
    COM_CONFIG->context->GroupStatus &= ~(1 << IpduGroupId);
  }
}

Std_ReturnType Com_ReceiveSignal(Com_SignalIdType SignalId, void *SignalDataPtr) {
  Std_ReturnType ret = E_NOT_OK;
  const Com_SignalConfigType *signal;

  if ((SignalId < COM_CONFIG->numOfSignals) && (NULL != SignalDataPtr)) {
    signal = &COM_CONFIG->SignalConfigs[SignalId];
    ret = comReceiveSignal(signal, SignalDataPtr);
  }

  return ret;
}

Std_ReturnType Com_SendSignal(Com_SignalIdType SignalId, const void *SignalDataPtr) {
  Std_ReturnType ret = E_NOT_OK;
  const Com_SignalConfigType *signal;

  if (SignalId < COM_CONFIG->numOfSignals) {
    signal = &COM_CONFIG->SignalConfigs[SignalId];

    ret = comSendSignal(signal, SignalDataPtr);
  }

  return ret;
}

Std_ReturnType Com_SendSignalGroup(Com_SignalGroupIdType SignalGroupId) {
  Std_ReturnType ret = E_NOT_OK;
  const Com_SignalConfigType *signal;

  if (SignalGroupId < COM_CONFIG->numOfSignals) {
    signal = &COM_CONFIG->SignalConfigs[SignalGroupId];
    if (COM_UINT8N == signal->type) {
      memcpy(signal->ptr, signal->initPtr, (signal->BitSize >> 3));
      ret = E_OK;
    }
  }

  return ret;
}

Std_ReturnType Com_ReceiveSignalGroup(Com_SignalGroupIdType SignalGroupId) {
  Std_ReturnType ret = E_NOT_OK;
  const Com_SignalConfigType *signal;

  if (SignalGroupId < COM_CONFIG->numOfSignals) {
    signal = &COM_CONFIG->SignalConfigs[SignalGroupId];
    if (COM_UINT8N == signal->type) {
      memcpy((void *)signal->initPtr, signal->ptr, (signal->BitSize >> 3));
      ret = E_OK;
    }
  }

  return ret;
}

#if defined(COM_USE_CAN)
Std_ReturnType Com_TriggerIPDUSend(PduIdType PduId) {
  Std_ReturnType ret = E_NOT_OK;
  const Com_IPduConfigType *IPduConfig;
  PduInfoType PduInfo;

  if (PduId < COM_CONFIG->numOfIPdus) {
    IPduConfig = &COM_CONFIG->IPduConfigs[PduId];
    if ((IPduConfig->txConfig) && (COM_CONFIG->context->GroupStatus & IPduConfig->GroupRefMask)) {
      PduInfo.SduDataPtr = IPduConfig->ptr;
      PduInfo.SduLength = IPduConfig->length;
      ret = PduR_ComTransmit(IPduConfig->txConfig->TxPduId, &PduInfo);
      if (E_OK == ret) {
        IPduConfig->txConfig->context->timer = IPduConfig->txConfig->CycleTime;
      } else {
        IPduConfig->txConfig->context->timer = 1;
        ret = E_OK;
      }
    }
  }

  return ret;
}
#endif

void Com_RxIndication(PduIdType RxPduId, const PduInfoType *PduInfoPtr) {
  const Com_IPduConfigType *IPduConfig;
  if (RxPduId < COM_CONFIG->numOfIPdus) {
    IPduConfig = &COM_CONFIG->IPduConfigs[RxPduId];
    if (IPduConfig->rxConfig && (COM_CONFIG->context->GroupStatus & IPduConfig->GroupRefMask)) {
      if (IPduConfig->length <= PduInfoPtr->SduLength) {
        memcpy(IPduConfig->ptr, PduInfoPtr->SduDataPtr, IPduConfig->length);
        IPduConfig->rxConfig->context->timer = IPduConfig->rxConfig->Timeout;
        if (IPduConfig->rxConfig->RxNotification) {
          IPduConfig->rxConfig->RxNotification();
        }
      }
    }
  }
}

Std_ReturnType Com_TriggerTransmit(PduIdType TxPduId, PduInfoType *PduInfoPtr) {
  Std_ReturnType ret = E_NOT_OK;
  const Com_IPduConfigType *IPduConfig;

  if (TxPduId < COM_CONFIG->numOfIPdus) {
    IPduConfig = &COM_CONFIG->IPduConfigs[TxPduId];
    if (IPduConfig->length <= PduInfoPtr->SduLength) {
      memcpy(PduInfoPtr->SduDataPtr, IPduConfig->ptr, IPduConfig->length);
      ret = E_OK;
    }
  }

  return ret;
}

void Com_TxConfirmation(PduIdType TxPduId, Std_ReturnType result) {
  const Com_IPduConfigType *IPduConfig;
  if (TxPduId < COM_CONFIG->numOfIPdus) {
    IPduConfig = &COM_CONFIG->IPduConfigs[TxPduId];
    if (IPduConfig->txConfig && (COM_CONFIG->context->GroupStatus & IPduConfig->GroupRefMask)) {
      if (E_OK == result) {
        if (IPduConfig->txConfig->TxNotification) {
          IPduConfig->txConfig->TxNotification();
        }
      } else {
        if (IPduConfig->txConfig->ErrorNotification) {
          IPduConfig->txConfig->ErrorNotification();
        }
      }
    }
  }
}

void Com_MainFunctionRx(void) {
  const Com_IPduConfigType *IPduConfig;
  int i;

  for (i = 0; i < COM_CONFIG->numOfIPdus; i++) {
    IPduConfig = &COM_CONFIG->IPduConfigs[i];
    if (IPduConfig->rxConfig && (COM_CONFIG->context->GroupStatus & IPduConfig->GroupRefMask)) {
      if (IPduConfig->rxConfig->context->timer > 0) {
        IPduConfig->rxConfig->context->timer--;
        if (0 == IPduConfig->rxConfig->context->timer) {
          if (IPduConfig->rxConfig->RxTOut) {
            IPduConfig->rxConfig->RxTOut();
          }
        }
      }
    }
  }
}

void Com_MainFunctionTx(void) {
#if defined(COM_USE_CAN)
  const Com_IPduConfigType *IPduConfig;
  Std_ReturnType ret;
  PduInfoType PduInfo;
  int i;

  for (i = 0; i < COM_CONFIG->numOfIPdus; i++) {
    IPduConfig = &COM_CONFIG->IPduConfigs[i];
    if ((IPduConfig->txConfig) && (COM_CONFIG->context->GroupStatus & IPduConfig->GroupRefMask)) {
      if (IPduConfig->txConfig->context->timer > 0) {
        IPduConfig->txConfig->context->timer--;
        if (0 == IPduConfig->txConfig->context->timer) {
          PduInfo.SduDataPtr = IPduConfig->ptr;
          PduInfo.SduLength = IPduConfig->length;
          ret = PduR_ComTransmit(IPduConfig->txConfig->TxPduId, &PduInfo);
          if (E_OK == ret) {
            IPduConfig->txConfig->context->timer = IPduConfig->txConfig->CycleTime;
#ifdef COM_USE_SIGNAL_UPDATE_BIT
            comTxClearUpdateBit(IPduConfig);
#endif
          } else {
            IPduConfig->txConfig->context->timer = 1;
          }
        }
      }
    }
  }
#endif
}

void Com_MainFunction(void) {
  Com_MainFunctionRx();
  Com_MainFunctionTx();
}