#pragma once

#include <sys/socket.h>
#include <arpa/inet.h>


struct dp_sock{
    socklen_t          len;
    _Bool              isAddrInit;
    struct sockaddr_in addr;
};

typedef struct dp_connection{
    unsigned int       seqNum;
    int                udp_sock;
    _Bool              isConnected;
    struct dp_sock     outSockAddr;
    struct dp_sock     inSockAddr;
    int                dbgMode;
} dp_connection;

typedef struct dp_connection *dp_connp;


/*
 * Drexel Protocol (dp) PDU
 */
#define DP_PROTO_VER_1   1

//THIS IS HOW YOU DO A BIT FIELD
//
//   64  32  16  8   4   2   1
// |---+---+---+---+---+---+---|
//   E   F   N   C   C   S   A
//   R   R   A   L   O   E   C
//   R   A   C   O   N   N   K
//   O   G   K   S   C   D
//   R           E   T
//-------------------------------
#define DP_MT_ACK        1              //ACK MSG                                   00000001
#define DP_MT_SND        2              //SND MSG                                   00000010
#define DP_MT_CONNECT    4              //Connect MSG                               00000100
#define DP_MT_CLOSE      8              //CLOSE MSG                                 00001000
#define DP_MT_NACK       16             //NEG ACK                                   00010000
#define DP_MT_FRAGMENT   32             //DGRAM IS A FRAGMENT:                      00100000
#define DP_MT_ERROR      64             //SIMULATE ERROR:                           01000000

#define DP_MT_SNDFRAG (DP_MT_SND | DP_MT_FRAGMENT) // SEND FRAGMENT = 34            00100010

//Message ACKS, ACK OR'ed with Message Type
#define DP_MT_SNDACK    (DP_MT_SND     | DP_MT_ACK) // SEND ACK = 3                 00000011
#define DP_MT_CNTACK    (DP_MT_CONNECT | DP_MT_ACK) // CONNECT ACK = 5              00000101
#define DP_MT_CLOSEACK  (DP_MT_CLOSE   | DP_MT_ACK) // CLOSE ACK = 9                00001001

#define DP_MT_SNDFRAGACK (DP_MT_SNDACK | DP_MT_FRAGMENT) // SEND FRAGMENT ACK = 35  00100011

// determines if the current message type is a fragment
// will return true as long as the bit in the 6th position is on (1)
#define IS_MT_FRAGMENT(x) ((x & DP_MT_FRAGMENT) == DP_MT_FRAGMENT)

typedef struct dp_pdu {
    int     proto_ver;
    int     mtype;
    int     seqnum;
    int     dgram_sz;
    int     err_num;
} dp_pdu;

#define     DP_MAX_BUFF_SZ          512
#define     DP_MAX_DGRAM_SZ         (DP_MAX_BUFF_SZ + sizeof(dp_pdu)) // 512 + 20 (five 4-byte ints) = 532

#define     DP_NO_ERROR             0
#define     DP_ERROR_GENERAL        -1
#define     DP_ERROR_PROTOCOL       -2
#define     DP_BUFF_UNDERSIZED      -4
#define     DP_BUFF_OVERSIZED       -8
#define     DP_CONNECTION_CLOSED    -16
#define     DP_ERROR_BAD_DGRAM      -32
//#define     DP_ERROR_TIMEOUT        -64

//PROTOTYPES - INTERNAL HELPERS
static dp_connp dpinit();

dp_connp dpServerInit(int port);
dp_connp dpClientInit(char *addr, int port);
static char * pdu_msg_to_string(dp_pdu *pdu);

//API Interface
void * dp_prepare_send(dp_pdu *pdu_ptr, void *buff, int buff_sz);
int dprecv(dp_connp dp, void *buff, int buff_sz);
int dpsend(dp_connp dp, void *sbuff, int sbuff_sz);
int dplisten(dp_connp dp);
int dpconnect(dp_connp dp);
int dpdisconnect(dp_connp dp);

void dpclose(dp_connp dpsession);
void print_out_pdu(dp_pdu *pdu);
void print_in_pdu(dp_pdu *pdu);
int  dpmaxdgram();
static void print_pdu_details(dp_pdu *pdu);
static int dpsendraw(dp_connp dp, void *sbuff, int sbuff_sz);
static int dprecvraw(dp_connp dp, void *buff, int buff_sz);
static int dprecvdgram(dp_connp dp, void *buff, int buff_sz);
static int dpsenddgram(dp_connp dp, void *sbuff, int sbuff_sz);
