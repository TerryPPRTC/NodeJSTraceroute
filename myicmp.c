#include "myicmp.h"
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/udp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/fcntl.h>
#include <poll.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>


#define MAX_TEST_TTL 30

struct myicmp{
    int sock_icmp;
    const char* fill_data;
    int len_fill_data;
    
    struct	in_addr local_addr;//save from reply of icmp
    
    struct	in_addr udp_dest_addr;
    in_port_t udp_dest_port;
    
    int ttl_min;//min unreach packet index
    int ttl_max;
    unsigned short port_map_index[MAX_TEST_TTL];
    struct	in_addr router[MAX_TEST_TTL];
    double t1[MAX_TEST_TTL];
    double t2[MAX_TEST_TTL];
    int socks[MAX_TEST_TTL];
    
};

//struct myicmp myicmp_ctx;

const char* fill_data(len){
    if(len >32)
        return NULL;
    static char buf[33]={0};
    int i;
    if(buf[0] == 0){
        for (i = 0; i < len; i++)
            buf[i] = 0x40 + (i & 0x3f);
    }
    return buf;
}
int cmpare_fill_data(const char* data, const char* fill_data, int len)
{
    int i;
    for(i=0; i<len && i<=32; i++){
        if(*(data+i) != *(fill_data+i))
            return i;
    }
    return 0;
}

int map_port_to_index(struct myicmp* myicmp_ctx, unsigned short port)
{
    int i;
    for(i=0; i<MAX_TEST_TTL; i++){
        if(myicmp_ctx->port_map_index[i] == port && port != 0)
            return i;
    }
    return -1;
}


int send_package_udp(struct sockaddr_in* dest, int ttl, int count, struct myicmp* myicmp_ctx)
{
    int i;
    int fd;
    struct sockaddr_in addr;
    socklen_t lenaddr = sizeof(struct sockaddr_in);
    unsigned short port;
    
    if(count <= 0 || dest == NULL || !myicmp_ctx)
        return -1;
    ttl = 1;//currently we only send one test udp for a hop
    
    for(i=0; i<count && i<MAX_TEST_TTL; i++){
        //create socket
        fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if(fd <= 0)
            return -2;
        
        //set ttl
        if(setsockopt (fd, IPPROTO_IP, IP_TTL, &ttl, sizeof (ttl)) < 0){
		   close(fd);
           return -3;
        }
        //set socket non block
        fcntl (fd, F_SETFL, O_NONBLOCK);

        
        //send udp data
        ssize_t sz = sendto(fd, myicmp_ctx->fill_data, myicmp_ctx->len_fill_data,
                                0, (const struct sockaddr *)dest, sizeof(*dest));
        if (sz < 0) {
            //printf("ERROR: sendto : %s %s\n", strerror(errno), inet_ntoa(dest->sin_addr));
            //(errno == ENOBUFS || errno == EAGAIN)
            //here, for simple, we just ignore the errno. If not send close it, and skip the test.
            close(fd);
            return -4;
        }

           // printf("Success: sendto : %s\n", strerror(errno));
        //save the port
        getsockname(fd, (struct sockaddr *)&addr, &lenaddr);
        port = ntohs(addr.sin_port);
        myicmp_ctx->port_map_index[i] = port;
        //printf("err:%d\t%d=>%d\n", errno, port, i);//to test port search
        //save time
        myicmp_ctx->t1[i] = get_time();

        //at last, add to the poll-fd list
        add_poll(fd, POLLIN);
        myicmp_ctx->socks[i] = fd;
        
        ttl ++;
    }
    
    return i;//succeeded count
}

void poll_callback_icmp(const char* buf, ssize_t len, struct myicmp* myicmp_ctx)
{
//printf("poll_callback_icmp\n");

//the length of the sent testing udp package is 60 (IP header+UDP header+32 char byte data)
#define UDP_IP_PACK_LENGTH 28 //60-32
    int a = sizeof(struct ip)+ICMP_MINLEN + UDP_IP_PACK_LENGTH;
    if(!buf || len < sizeof(struct ip)+ICMP_MINLEN + UDP_IP_PACK_LENGTH || !myicmp_ctx)
        return;
 
    //this buf including the ip head. at least on my mac.
    struct ip* pIp = (struct ip*)buf;
    char* rfrom = inet_ntoa(pIp->ip_src);
    //printf("icmp reply: %s\n", rfrom);
    
    struct icmp* pIcmp=(struct icmp*)(buf+sizeof(struct ip));
    //ttl exceeded icmp package
    struct ip* pIp_inner=(struct ip*)(buf+sizeof(struct ip) + 8);//8 byte is this icmp package header length
    if(pIp_inner->ip_p == IPPROTO_UDP &&
        pIp_inner->ip_dst.s_addr == myicmp_ctx->udp_dest_addr.s_addr)
    {
        struct udphdr * pUdp_inner = (struct udphdr*)(buf+sizeof(struct ip) + 8 + sizeof(struct ip));
        if(pUdp_inner->uh_dport == myicmp_ctx->udp_dest_port)
        {
            //const char* data = buf+sizeof(struct ip) + 8+sizeof(struct ip) + sizeof(struct udphdr);
            //if(cmpare_fill_data(data, myicmp_ctx->fill_data, myicmp_ctx->len_fill_data) == 0)
            {
                int index = map_port_to_index(myicmp_ctx, ntohs(pUdp_inner->uh_sport));
                if(index >= MAX_TEST_TTL || index < 0)
                    return ;//we only allow 30 test
                //printf("searching port:%d  => %d\n", ntohs(pUdp_inner->uh_sport), index);//to test port search
                
                //the udp sock can be closed
                if(myicmp_ctx->socks[index] != 0){
                    del_poll(myicmp_ctx->socks[index]);
                    close(myicmp_ctx->socks[index]);
                }
                
                //this must be the icmp we expected.
                if(pIcmp->icmp_type == ICMP_TIMXCEED )//&& pIcmp->icmp_code == ICMP_TIMXCEED_INTRANS
                {
                    myicmp_ctx->router[index] = pIp->ip_src;
                    myicmp_ctx->t2[index] = get_time();
                    
                    if(index+1 > myicmp_ctx->ttl_max)
                        myicmp_ctx->ttl_max = index+1;
                    
                    if(myicmp_ctx->local_addr.s_addr == 0)
                        myicmp_ctx->local_addr = pIp->ip_dst;//if not get yet, copy the local ip
                    
                }
                else if(pIcmp->icmp_type == ICMP_UNREACH && pIcmp->icmp_code == ICMP_UNREACH_PORT)
                {
                    //port unreachable icmp package, this may happen when test udp package reached the last host point
                    myicmp_ctx->router[index] = pIp->ip_src;
                    myicmp_ctx->t2[index] = get_time();
                    
                    if(myicmp_ctx->ttl_min == 0 || index+1 < myicmp_ctx->ttl_min)
                        myicmp_ctx->ttl_min = index+1;
                    
                    myicmp_ctx->local_addr = pIp->ip_dst;//copy the local ip
                }
            }
        }
    }

    return;
}

void poll_callback_udp(const char* buf, ssize_t len, struct myicmp* myicmp_ctx)
{
    //printf("ERROR: This should not happen.");
}

int open_icmp_socket(){
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);//SOCK_RAW SOCK_DGRAM 需要root权限
    if(sock == -1){
        //printf("Open ICMP socket failed. %s\n",  strerror(errno));
	return -1;
    }
    
    fcntl (sock, F_SETFL, O_NONBLOCK);
    
    add_poll(sock, POLLIN);
    return sock;
}

void poll_callback(int fd, int revents, void* data)
{
    if (!(revents & (POLLIN | POLLERR)) || !data)
    		return;
    struct myicmp* p_myicmp_ctx = (struct myicmp*)data;
    int sock_icmp = p_myicmp_ctx->sock_icmp;
    if(sock_icmp == -1)
        return;
    
    struct msghdr msg={0};
	struct sockaddr_in from;
	struct iovec iov;
	char buf[1280];		/*  min mtu for ipv6 ( >= 576 for ipv4)  */
	char control[1024];

	msg.msg_name = &from;
	msg.msg_namelen = sizeof (from);
	msg.msg_control = control;
	msg.msg_controllen = sizeof (control);
	iov.iov_base = buf;
	iov.iov_len = sizeof (buf);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

    ssize_t ret;
	ret = recvmsg (fd, &msg, 0);//err ? MSG_ERRQUEUE : 0
	if (ret < 0){
        //printf("recvmsg : %s\n", strerror(errno));//here, we just ignored eagain message for simple. maybe BUGS
    }else{
        //process the received data
        if(fd == sock_icmp){
            poll_callback_icmp(buf, ret, p_myicmp_ctx);
        }else{
            poll_callback_udp(buf, ret, p_myicmp_ctx);//this should not happen as designed
        }
    }
    
    if(fd != sock_icmp)
    {
        //not icmp socket, closing it
        del_poll(fd);
        close(fd);
    }

}

#define	UDP_TEST_PORT		50001
#define UDP_SERVER_IP 		"183.136.187.34"

void print_result(struct myicmp* myicmp_ctx)
{
    printf("From: %s\t\t", inet_ntoa(myicmp_ctx->local_addr));
    printf("To: %s\n", inet_ntoa(myicmp_ctx->udp_dest_addr));
    int i;
    int last = myicmp_ctx->ttl_min;
    if(last == 0){
        printf("target unreachable, may disabled ICMP reply.\n");
        last = myicmp_ctx->ttl_max;
    }
    for(i =0; i<last; i++)
    {
        if(myicmp_ctx->t2[i] > 1.0f)
            printf("%d:\t%s \t\t%.3f\n", i, inet_ntoa(myicmp_ctx->router[i]), myicmp_ctx->t2[i]-myicmp_ctx->t1[i]);
        else
            printf("* \t*\t\t*\n");
    }
}

char* print_result_as_json(struct myicmp* myicmp_ctx)
{
    char* p;
    int i, n;
    p = (char*)malloc(2048);
    *p = '[';
    n = 1;
    int last = myicmp_ctx->ttl_min;
    if(last == 0){
        last = myicmp_ctx->ttl_max;
    }
    for(i =0; i<last && n<2000; i++)
    {
        if(myicmp_ctx->t2[i] > 1.0f){
            n += sprintf(p+n, "{\"i\":\"%d\",\"ip\":\"%s\",\"t\":\"%d\"},", 
                i, inet_ntoa(myicmp_ctx->router[i]), 
                (int)(1000*(myicmp_ctx->t2[i]-myicmp_ctx->t1[i])) );
        }
    }
    *(p+n-1) = ']';
    *(p+n) = '\0';
   
    return p;
}

const char* get_target_ip(const char* ptr);

char* main_start(const char* dest_ip){
//printf("to %s\n", dest_ip);
    if(dest_ip == NULL || *dest_ip == 0)
        return NULL;
    const char* ip_target = get_target_ip(dest_ip);
    if(ip_target == NULL)
	return NULL;

    int sock_icmp = -1;
    
    struct myicmp myicmp_ctx;
    memset(&myicmp_ctx,0, sizeof(struct myicmp));//rest the routes and times

    struct sockaddr_in sockaddr = {0};
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_addr.s_addr = inet_addr(ip_target);
    sockaddr.sin_port = htons(UDP_TEST_PORT);

    int buf_len = 32;
    const char* buf = fill_data(buf_len);
    if(buf == NULL)
        return NULL;
    
    int test_count = 30;
    int ttl = 1;
    
    sock_icmp = open_icmp_socket();//the icmp socket will receive all the icmp packets
    if(sock_icmp == -1)
        return NULL; 
    //init ctx
    myicmp_ctx.sock_icmp = sock_icmp;
    myicmp_ctx.udp_dest_addr.s_addr = sockaddr.sin_addr.s_addr;
    myicmp_ctx.udp_dest_port = sockaddr.sin_port;
    myicmp_ctx.fill_data = buf;
    myicmp_ctx.len_fill_data = buf_len;
    
    int ret = send_package_udp(&sockaddr, ttl, test_count, &myicmp_ctx);
    //just ignore the ret
    if(ret >= 0){
        do_poll (1, poll_callback, (void*)&myicmp_ctx);//10 sec
    }
    
    //close all opened socket, and just fail
    close_polls();
   
    //now out put the result
    //print_result(&myicmp_ctx);
    return print_result_as_json(&myicmp_ctx);
}

const char* get_target_ip(const char* ptr)
{
    //char   *ptr, **pptr;
    struct hostent *hptr;
    static char   str[32];
    
    if(!ptr)
        return NULL;
    
    if((hptr = gethostbyname(ptr)) == NULL)
        return NULL;

    if(hptr->h_addrtype == AF_INET)
        return inet_ntop(hptr->h_addrtype, hptr->h_addr, str, sizeof(str));

    return NULL;
}

int main_(int argc, char *argv[])//myicmp_
{
    if(argc != 2){
        printf("Must have an address");
        return -1;
    }
    
    //double t1 = get_time();
    //const char* ip_target = get_target_ip(  argv[1] );
    //double t2 = get_time();
    
    //printf("DNS: %s (%s)\t %.3f\n", argv[1], ip_target, t2-t1);
    
    char* p = main_start( argv[1] ); //ip_target);
    if(p){
        printf("%s\n", p);
        free(p);
    }
    
    return 0;
}



