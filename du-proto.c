#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/un.h>
#include <time.h>
#include <sys/socket.h>
//#include <sys/time.h>

#include "du-proto.h"

static char _dpBuffer[DP_MAX_DGRAM_SZ];
static int  _debugMode = 1;

/*
 * The protocol connection will be initialized with default values and returned.
 * First, the initialization for the addresses for the in and out sockets
   will be set to false, meaning they have not been initialized yet.
 * Next, the length of the in and out sockets will be set to whatever
   the size of a socket structure is, ensuring appropriate storage size.
 * Then, the initial sequence number will be set to 0, the connection property
   will be set to false (as we have not connected yet), and debug mode will be
   set to true.
 * The only properties of the session that have not been initialized here are
   the udp socket file descriptor (udp_sock) and the address of the in and out
   sockets (each of which are their own socket structures with additional properties).
*/
static dp_connp dpinit(){
    dp_connp dpsession = malloc(sizeof(dp_connection));
    bzero(dpsession, sizeof(dp_connection));
    dpsession->outSockAddr.isAddrInit = false;
    dpsession->inSockAddr.isAddrInit = false;
    dpsession->outSockAddr.len = sizeof(struct sockaddr_in);
    dpsession->inSockAddr.len = sizeof(struct sockaddr_in);
    dpsession->seqNum = 0;
    dpsession->isConnected = false;
    dpsession->dbgMode = true;
    return dpsession;
}

/*
 * This method will close a protocol session
   by freeing the structure that holds the
   connection details
*/
void dpclose(dp_connp dpsession) {
    free(dpsession);
}

/*
 * Maximum datagram
 * Supports 512 bytes
*/
int  dpmaxdgram(){
    return DP_MAX_BUFF_SZ;
}

/*
 * Will initialize the server protocol connection.
 * A protocol connection will be initalized with default values.
 * Next, a variable for the udp socket and in socket address
   will be initialized by reference from the protocol connection object.
 * Then, the udp socket endpoint will be created.
 * After that, the server information will be filled in,
   including the port passed to this method (the IP address will be
   automatically assigned to accept any incoming messages).
 * Then, the socket options will be set for the port and address so that we do not
   have to wait for ports and addresses held by the OS.
 * Lastly, the in socket will be bound to the address of the in socket.
*/
dp_connp dpServerInit(int port) {
    struct sockaddr_in *servaddr;
    int *sock;
    int rc;

    dp_connp dpc = dpinit();
    if (dpc == NULL) {
        perror("drexel protocol create failure"); 
        return NULL;
    }

    sock = &(dpc->udp_sock);
    servaddr = &(dpc->inSockAddr.addr);
        

    // Creating socket file descriptor 
    if ( (*sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { // DGRAM = UDP
        perror("socket creation failed"); 
        return NULL;
    } 

    // Filling server information 
    servaddr->sin_family    = AF_INET; // IPv4 
    servaddr->sin_addr.s_addr = INADDR_ANY; 
    servaddr->sin_port = htons(port); 

    // Set socket options so that we dont have to wait for ports held by OS
    if (setsockopt(*sock, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int)) < 0){
        perror("setsockopt(SO_REUSEADDR) failed");
        close(*sock);
        return NULL;
    }
    if (setsockopt(*sock, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0){
        perror("setsockopt(SO_REUSEADDR) failed");
        close(*sock);
        return NULL;
    }

    // struct timeval tv;
    // tv.tv_sec = 10;
    // tv.tv_usec = 0;
    // if (setsockopt(*sock, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) {
    //     perror("RECV TIME ERROR");
    // }
    // if (setsockopt(*sock, SOL_SOCKET, SO_SNDTIMEO,&tv,sizeof(tv)) < 0) {
    //     perror("SEND TIME ERROR");
    // }


    if ( (rc = bind(*sock, (const struct sockaddr *)servaddr,  
            dpc->inSockAddr.len)) < 0 ) 
    { 
        perror("bind failed"); 
        close (*sock);
        return NULL;
    } 

    dpc->inSockAddr.isAddrInit = true;
    dpc->outSockAddr.len = sizeof(struct sockaddr_in);
    return dpc;
}

/*
 * Will initialize the client protocol connection.
 * A protocol connection will be initalized with default values.
 * Next, a variable for the udp socket and out socket address
   will be initialized by reference from the protocol connection object.
 * Then, the udp socket endpoint will be created.
 * After that, the server information will be filled in,
   including the server IP address and port passed to this method.
 * Finally, the in socket will be set to the same attributes as the
   out socket, since the inbound address is also the outbound address.
*/
dp_connp dpClientInit(char *addr, int port) {
    struct sockaddr_in *servaddr;
    int *sock;

    dp_connp dpc = dpinit();
    if (dpc == NULL) {
        perror("drexel protocol create failure"); 
        return NULL;
    }

    sock = &(dpc->udp_sock);
    servaddr = &(dpc->outSockAddr.addr);

    // Creating socket file descriptor 
    if ( (*sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { // DGRAM = UDP
        perror("socket creation failed"); 
        return NULL;
    } 

    // Filling server information 
    servaddr->sin_family = AF_INET; 
    servaddr->sin_port = htons(port); 
    servaddr->sin_addr.s_addr = inet_addr(addr);
    dpc->outSockAddr.len = sizeof(struct sockaddr_in); 
    dpc->outSockAddr.isAddrInit = true;

    // The inbound address is the same as the outbound address
    memcpy(&dpc->inSockAddr, &dpc->outSockAddr, sizeof(dpc->outSockAddr));

    return dpc;
}

/*
 * Initiate the recieve process.
 * Will loop until the entire file is recieved.
 * The loop will be broken when either a connection closed
   is recieved or fragments are done being sent.
 * In the loop, dpsenddgram() will be called which will
   recieve file data and build an ACK PDU.
 * Next, if a connection close was recieved, the method
   will return said close connection number.
 * Then, if not a connection close, the file data will be
   extracted from the recieve buffer.
 * After the loop is finished, return the total bytes recieved.
*/
int dprecv(dp_connp dp, void *buff, int buff_sz){

    dp_pdu *inPdu;
    void *rptr = buff;
    int totalRecieved = 0;
    int amount_recieved = 0;
    bool isFragment = false;

    // Loop to read the entire file
    do {

        amount_recieved = dprecvdgram(dp, _dpBuffer, sizeof(_dpBuffer));
        if(amount_recieved == DP_CONNECTION_CLOSED)
            return DP_CONNECTION_CLOSED;

        inPdu = (dp_pdu *)_dpBuffer;
        if(amount_recieved > sizeof(dp_pdu))
            memcpy(rptr, (_dpBuffer+sizeof(dp_pdu)), inPdu->dgram_sz); // ignores the PDU
        
        rptr += (amount_recieved - sizeof(dp_pdu)); // don't want to increment an additional 20 bytes for the pdu
        totalRecieved += (amount_recieved - sizeof(dp_pdu)); // don't want to increment an additional 20 bytes for the pdu
        isFragment = IS_MT_FRAGMENT(inPdu->mtype);

    } while(isFragment);

    return totalRecieved;
    
}

/*
 * Recieve raw data and build an ACK PDU based on the recieved data.
 * First, raw data will be recieved from the socket.
 * Next, the recieved (in) PDU will be stored.
 * After this, the PDU will be updated for an acknowlegement
   (as long as there is no error) by updating the
   sequence number and incrementing it.
 * Then, the out PDU will be constructed with the ACK
   appropriate info and will determine whether this is
   a send ACK or a close ACK based on the message type recieved.
 * Finally, the out PDU with the ACK info is sent.
   If the message type is send or fragment, the bytes recieved will be returned.
   If the message type is close, the protocol strucutre will be freed
   and the connection close number will be returned.
*/
static int dprecvdgram(dp_connp dp, void *buff, int buff_sz){
    int bytesIn = 0;
    int errCode = DP_NO_ERROR;
    bool isFragment = false;

    if(buff_sz > DP_MAX_DGRAM_SZ)
        return DP_BUFF_OVERSIZED;

    bytesIn = dprecvraw(dp, buff, buff_sz);

    //check for some sort of error and just return it
    if (bytesIn < sizeof(dp_pdu))
        errCode = DP_ERROR_BAD_DGRAM;

    dp_pdu inPdu;
    memcpy(&inPdu, buff, sizeof(dp_pdu));
    if (inPdu.dgram_sz > buff_sz)
        errCode = DP_BUFF_UNDERSIZED;
    
    // Determine if the in mtype is a fragment
    isFragment = IS_MT_FRAGMENT(inPdu.mtype);
    
    //UDPATE SEQ NUMBER AND PREPARE ACK
    if (errCode == DP_NO_ERROR){
        if(inPdu.dgram_sz == 0)
            //Update Seq Number to just ack a control message - just got PDU
            dp->seqNum ++;
        else
            //Update Seq Number to increas by the inbound PDU dgram_sz
            dp->seqNum += inPdu.dgram_sz;
    } else {
        //Update seq number to ACK error
        dp->seqNum++;
    }

    dp_pdu outPdu;
    outPdu.proto_ver = DP_PROTO_VER_1;
    outPdu.dgram_sz = 0;
    outPdu.seqnum = dp->seqNum;
    outPdu.err_num = errCode;

    int actSndSz = 0;
    //HANDLE ERROR SITUATION
    if(errCode != DP_NO_ERROR) {
        outPdu.mtype = DP_MT_ERROR;
        actSndSz = dpsendraw(dp, &outPdu, sizeof(dp_pdu));
        if (actSndSz != sizeof(dp_pdu))
            return DP_ERROR_PROTOCOL;
    }


    switch(inPdu.mtype){
        // Case for both send and fragment
        case DP_MT_SND: // need to send back a send ACK
        case DP_MT_SNDFRAG:

            // If the in mtype is a fragment, then the out mtype is a fragment
            // else, the out mtype is a regular send
            if(isFragment) {
                outPdu.mtype = DP_MT_SNDFRAGACK;
            } else {
                outPdu.mtype = DP_MT_SNDACK;
            }
            
            actSndSz = dpsendraw(dp, &outPdu, sizeof(dp_pdu));
            if (actSndSz != sizeof(dp_pdu))
                return DP_ERROR_PROTOCOL;
            break;
        case DP_MT_CLOSE: // need to send back a close ACK
            outPdu.mtype = DP_MT_CLOSEACK;
            actSndSz = dpsendraw(dp, &outPdu, sizeof(dp_pdu));
            if (actSndSz != sizeof(dp_pdu))
                return DP_ERROR_PROTOCOL;
            dpclose(dp);
            return DP_CONNECTION_CLOSED;
        default:
        {
            printf("ERROR: Unexpected or bad mtype in header %d\n", inPdu.mtype);
            return DP_ERROR_PROTOCOL;
        }
    }

    return bytesIn;
}

/*
 * Recieve raw data from a socket.
 * Will connect to the udp socket and write the data to the buffer.
 * recvfrom() will wait until a connection is established and data is recieved.
 * Once the connection is established and the data is recieved, the data will be stored
   in a PDU, then the PDU will be printed (if in debug mode),
   and the number of bytes recieved is returned.
*/
static int dprecvraw(dp_connp dp, void *buff, int buff_sz){
    int bytes = 0;

    if(!dp->inSockAddr.isAddrInit) {
        perror("dprecv: dp connection not setup properly - cli struct not init");
        return -1;
    }

    bytes = recvfrom(dp->udp_sock, (char *)buff, buff_sz,  
                MSG_WAITALL, ( struct sockaddr *) &(dp->outSockAddr.addr), 
                &(dp->outSockAddr.len)); 

    if (bytes < 0) {
        perror("dprecv: received error from recvfrom()");
        return -1;
    }
    dp->outSockAddr.isAddrInit = true;

    //some helper code if you want to do debugging
    if (bytes > sizeof(dp_pdu)){
        if(false) {                         //just diabling for now
            dp_pdu *inPdu = buff;
            char * payload = (char *)buff + sizeof(dp_pdu);
            printf("DATA : %.*s\n", inPdu->dgram_sz , payload); 
        }
    }

    dp_pdu *inPdu = buff;
    print_in_pdu(inPdu);

    //return the number of bytes received 
    return bytes;
}

/*
 * Initiate the send process.
 * Since the entire file is now in the buffer,
   the buffer will be looped 512 bytes at a time.
 * A pointer to the file buffer will increment 512
   bytes at a time until there is less than that left.
 * dpsenddgram() will recieve that file pointer and
   enforce the 512 byte cap for the datagram.
 * After that, a check will be done to make sure that
   the amount sent matches the size of the file.
 * Finally, the amount sent will be returned.
*/
int dpsend(dp_connp dp, void *sbuff, int sbuff_sz){

    void *sptr = sbuff;
    int totalToSend = sbuff_sz;
    int amountSent = 0;
    int curSend = 0;

    // Loop until the entire file is sent
    while(totalToSend > 0) {

        curSend = dpsenddgram(dp, sptr, totalToSend);
        if(curSend < 0) {
            return curSend;
        }
        
        amountSent += curSend;
        totalToSend -= curSend;
        sptr += curSend;

    }

    // Ensure that the amount sent is the same as the buffer size
    if(amountSent != sbuff_sz) {
        printf("Error: sent %d bytes, should have sent %d bytes\n", amountSent, sbuff_sz);
    }

    return amountSent;
}

/*
 * Send PDU and raw data, recieve an ACK PDU back.
 * First, the method will ensure that if the buffer is larger
   than 512 bytes, it will only send the first 512 bytes.
 * Next, the out PDU will be built and attached to the raw data
   while also determining if the mtype is a fragment or regular send.
 * Then, the PDU + data will be sent out.
 * After this, the sequence number will be approrpiately updated.
 * Finally, an in PDU will be recieved with the ACK information
   and the number of bytes of data (not including the PDU) will be returned.
*/
static int dpsenddgram(dp_connp dp, void *sbuff, int sbuff_sz){
    int bytesOut = 0;
    bool isFragment = false;
    int totalToSend = sbuff_sz;
    int mTypeExpected = 0;

    if(!dp->outSockAddr.isAddrInit) {
        perror("dpsend:dp connection not setup properly");
        return DP_ERROR_GENERAL;
    }

    // Only send 512 bytes at a time
    if(sbuff_sz > DP_MAX_BUFF_SZ) {
        isFragment = true;
        totalToSend = DP_MAX_BUFF_SZ;
    }

    //Build the PDU and out buffer
    dp_pdu *outPdu = (dp_pdu *)_dpBuffer;
    outPdu->proto_ver = DP_PROTO_VER_1;
    outPdu->dgram_sz = totalToSend;
    outPdu->seqnum = dp->seqNum;

    // If fragment, out and in mtypes should be fragments
    // else, out and in mtypes should be regular sends
    if(isFragment) {
        outPdu->mtype = DP_MT_SNDFRAG;
        mTypeExpected = DP_MT_SNDFRAGACK;
    } else {
        outPdu->mtype = DP_MT_SND;
        mTypeExpected = DP_MT_SNDACK;
    }

    memcpy((_dpBuffer + sizeof(dp_pdu)), sbuff, totalToSend);

    int totalSendSz = outPdu->dgram_sz + sizeof(dp_pdu); // will add 20 (size of dp_pdu) to the file size
    bytesOut = dpsendraw(dp, _dpBuffer, totalSendSz);

    if(bytesOut != totalSendSz){
        printf("Warning send %d, but expected %d!\n", bytesOut, totalSendSz);
    }

    //update seq number after send
    if(outPdu->dgram_sz == 0)
        dp->seqNum++;
    else
        dp->seqNum += outPdu->dgram_sz;

    //need to get an ack
    dp_pdu inPdu = {0};
    int bytesIn = dprecvraw(dp, &inPdu, sizeof(dp_pdu)); // where it will get the ACK
    if ((bytesIn < sizeof(dp_pdu)) && (inPdu.mtype != mTypeExpected)){
        printf("Expected SND/ACK but got a different mtype %d\n", inPdu.mtype);
    }

    return bytesOut - sizeof(dp_pdu);
}


/*
 * Send raw data to a socket.
 * Will connect to the udp socket and send the data from the buffer.
 * sendto() will wait until a connection is established and data is sent.
 * Once the connection is established and the data is send, the current PDU
   will be printed (if in debug mode), and the number of bytes sent is returned.
*/
static int dpsendraw(dp_connp dp, void *sbuff, int sbuff_sz){
    int bytesOut = 0;

    if(!dp->outSockAddr.isAddrInit) {
        perror("dpsendraw:dp connection not setup properly");
        return -1;
    }

    dp_pdu *outPdu = sbuff;
    bytesOut = sendto(dp->udp_sock, (const char *)sbuff, sbuff_sz, 
        0, (const struct sockaddr *) &(dp->outSockAddr.addr), 
            dp->outSockAddr.len); 

    
    print_out_pdu(outPdu);

    return bytesOut;
}

/*
 * Starts the server, waits for a request from the client,
   and sends an acknowlegement to the client.
 * First, the server will call the dprecvraw() method and wait for a connection
   Once the client connects, the server recieves data and stores it in a PDU
   The number of bytes recieved will be stored and checked.
 * Next, the message type of the server PDU is set to connect ACK
   and the sequence number is incremented by 1 for both the PDU
   and the server protocol connection.
 * Then, the server will call the dpsendraw() to send the updated PDU to the client.
   The number of bytes sent will be stored and checked.
 * Finally, if the connection ACK is recieved by the client, then a connection
   is established and the method will return true to acknowledge this.
*/
int dplisten(dp_connp dp) {
    int sndSz, rcvSz;

    if(!dp->inSockAddr.isAddrInit) {
        perror("dplisten:dp connection not setup properly - cli struct not init");
        return DP_ERROR_GENERAL;
    }

    dp_pdu pdu = {0};

    printf("Waiting for a connection...\n");
    rcvSz = dprecvraw(dp, &pdu, sizeof(pdu));
    if (rcvSz != sizeof(pdu)) {
        perror("dplisten:The wrong number of bytes were received");
        return DP_ERROR_GENERAL;
    }

    pdu.mtype = DP_MT_CNTACK;
    dp->seqNum = pdu.seqnum + 1;
    pdu.seqnum = dp->seqNum;
    
    sndSz = dpsendraw(dp, &pdu, sizeof(pdu));
    
    if (sndSz != sizeof(pdu)) {
        perror("dplisten:The wrong number of bytes were sent");
        return DP_ERROR_GENERAL;
    }
    dp->isConnected = true; 
    //For non data transmissions, ACK of just control data increase seq # by one
    printf("Connection established OK!\n");

    return true;
}

/*
 * Connect the client to the server
   and recieve an acknowlegement from the server.
 * First, an initial PDU will be constructred and sent
   to the server via dpsendraw() to establish a connection.
 * Next, the client will call dprecvraw() to recieve an
   ACK from the server that the connection was established.
 * Then, the sequence number in the protocol structure will be
   incremented by 1 to confirm the ACK recieved.
 * Finally, the method will return true to acknowledge the connection.
*/
int dpconnect(dp_connp dp) {

    int sndSz, rcvSz;

    if(!dp->outSockAddr.isAddrInit) {
        perror("dpconnect:dp connection not setup properly - svr struct not init");
        return DP_ERROR_GENERAL;
    }

    dp_pdu pdu = {0};
    pdu.mtype = DP_MT_CONNECT;
    pdu.seqnum = dp->seqNum;
    pdu.dgram_sz = 0;

    sndSz = dpsendraw(dp, &pdu, sizeof(pdu));
    if (sndSz != sizeof(dp_pdu)) {
        perror("dpconnect:Wrong about of connection data sent");
        return -1;
    }
    
    rcvSz = dprecvraw(dp, &pdu, sizeof(pdu));
    if (rcvSz != sizeof(dp_pdu)) {
        perror("dpconnect:Wrong about of connection data received");
        return -1;
    }
    if (pdu.mtype != DP_MT_CNTACK) {
        perror("dpconnect:Expected CNTACT Message but didnt get it");
        return -1;
    }

    //For non data transmissions, ACK of just control data increase seq # by one
    dp->seqNum++;
    dp->isConnected = true;
    printf("Connection established OK!\n");

    return true;
}

/*
 * Builds a close PDU and sends it.
 * Once the close PDU is sent, a PDU will be
   recieved containing the close ACK.
 * Then, the protocol structure will be freed.
 * Finally, the close connection number will be returned.
*/
int dpdisconnect(dp_connp dp) {

    int sndSz, rcvSz;

    dp_pdu pdu = {0};
    pdu.proto_ver = DP_PROTO_VER_1;
    pdu.mtype = DP_MT_CLOSE;
    pdu.seqnum = dp->seqNum;
    pdu.dgram_sz = 0;

    sndSz = dpsendraw(dp, &pdu, sizeof(pdu));
    if (sndSz != sizeof(dp_pdu)) {
        perror("dpdisconnect:Wrong about of connection data sent");
        return DP_ERROR_GENERAL;
    }
    
    rcvSz = dprecvraw(dp, &pdu, sizeof(pdu));
    if (rcvSz != sizeof(dp_pdu)) {
        perror("dpdisconnect:Wrong about of connection data received");
        return DP_ERROR_GENERAL;
    }
    if (pdu.mtype != DP_MT_CLOSEACK) {
        perror("dpdisconnect:Expected CNTACT Message but didnt get it"); 
        return DP_ERROR_GENERAL;
    }
    //For non data transmissions, ACK of just control data increase seq # by one
    dpclose(dp);

    return DP_CONNECTION_CLOSED;
}

/*
 * Will copy a PDU to a buffer
   and return the location where the
   data portion of the buffer starts
*/
void * dp_prepare_send(dp_pdu *pdu_ptr, void *buff, int buff_sz) {
    if (buff_sz < sizeof(dp_pdu)) {
        perror("Expected CNTACT Message but didnt get it");
        return NULL;
    }
    bzero(buff, buff_sz);
    memcpy(buff, pdu_ptr, sizeof(dp_pdu));

    return buff + sizeof(dp_pdu);
}


//// MISC HELPERS
/*
 * Calls the PDU print method to
   print outgoing PDU attributes
   (if in debug mode)
*/
void print_out_pdu(dp_pdu *pdu) {
    if (_debugMode != 1)
        return;
    printf("PDU DETAILS ===>  [OUT]\n");
    print_pdu_details(pdu);
}
/*
 * Calls the PDU print method to
   print incoming PDU attributes
   (if in debug mode)
*/
void print_in_pdu(dp_pdu *pdu) {
    if (_debugMode != 1)
        return;
    printf("===> PDU DETAILS  [IN]\n");
    print_pdu_details(pdu);
}
/*
 * Prints PDU attributes for both
   the outgoing and incoming PDUs
*/
static void print_pdu_details(dp_pdu *pdu){
    
    printf("\tVersion:  %d\n", pdu->proto_ver);
    printf("\tMsg Type: %s\n", pdu_msg_to_string(pdu));
    printf("\tMsg Size: %d\n", pdu->dgram_sz);
    printf("\tSeq Numb: %d\n", pdu->seqnum);
    printf("\n");
}

/*
 * Associates each PDU message Integer type
   with a String for printing purposes
*/
static char * pdu_msg_to_string(dp_pdu *pdu) {
    switch(pdu->mtype){
        case DP_MT_ACK:
            return "ACK";     
        case DP_MT_SND:
            return "SEND";      
        case DP_MT_CONNECT:
            return "CONNECT";   
        case DP_MT_CLOSE:
            return "CLOSE";     
        case DP_MT_NACK:
            return "NACK";      
        case DP_MT_SNDACK:
            return "SEND/ACK";    
        case DP_MT_CNTACK:
            return "CONNECT/ACK";    
        case DP_MT_CLOSEACK:
            return "CLOSE/ACK";
        // Now includes fragment mtypes
        case DP_MT_SNDFRAG:
            return "SENDFRAG";
        case DP_MT_SNDFRAGACK:
            return "SENDFRAG/ACK";
        default:
            return "***UNKNOWN***";  
    }
}

/*
 *  This is a helper for testing if you want to inject random errors from
 *  time to time. It take a threshold number as a paramter and behaves as
 *  follows:
 *      if threshold is < 1 it always returns FALSE or zero
 *      if threshold is > 99 it always returns TRUE or 1
 *      if (1 <= threshold <= 99) it generates a random number between
 *          1..100 and if the random number is less than the threshold
 *          it returns TRUE, else it returns false
 * 
 *  Example: dprand(50) is a coin flip
 *              dprand(25) will return true 25% of the time
 *              dprand(99) will return true 99% of the time
 */
int dprand(int threshold){

    if (threshold < 1)
        return 0;
    if (threshold > 99)
        return 1;
    //initialize randome number seed
    srand(time(0));

    int rndInRange = (rand() % (100-1+1)) + 1;
    if (threshold < rndInRange)
        return 1;
    else
        return 0;
}

