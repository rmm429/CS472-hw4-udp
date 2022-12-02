#pragma once

#define PROG_MD_CLI     0
#define PROG_MD_SVR     1
#define DEF_PORT_NO     2080
#define FNAME_SZ        128
#define PROG_DEF_FNAME  "test.c"
#define PROG_DEF_SVR_ADDR   "127.0.0.1"

typedef struct prog_config{
    int     prog_mode;
    int     port_number;
    char    svr_ip_addr[16];
    char    file_name[128];
} prog_config;

/*
 * PDU for the file transfer protocol
 * Can store file name, data transfer status
   (for both sends and recieves),
   error information (mainly for the file),
   and the file descriptor of the file.
 * NOTE: fd can be stored by calling fileno()
   on the FILE object, this was used as an alternative
   to storing the entire FILE object in the PDU.
   This way, operations on the file (such as close) can
   be done without needing the entire FILE object.
 * NOTE TO GRADER: the README for this homework said to
   define the PDU but nothing about implementing it.
   Thus, this structure is not used in du-ftp.c
 */
typedef struct ftp_pdu {
    char    file_name[128];
    int     data_transfer_status;
    int     err_num;
    int     fd;
} ftp_pdu;
