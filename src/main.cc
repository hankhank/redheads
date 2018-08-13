
#include <cstdio>

#include <stdio.h>          // for printf() and fprintf()
#include <sys/socket.h>     // for socket(), bind(), and connect()
#include <arpa/inet.h>      // for sockaddr_in and inet_ntoa()
#include <stdlib.h>         // for atoi() and exit()
#include <string.h>         // for memset()
#include <unistd.h>         // for close()
#include <fcntl.h>          // for fcntl()
#include <errno.h>
#include <sys/epoll.h>

#include "../lib/engine.h"

#define MAX_EVENTS 100

#define BUFFSIZE 2042

#define LOG_ERROR(__X) \
    do {fprintf(stderr, __X); fprintf(stderr, "\n"); } while(0);

unsigned char IN_BUF[BUFFSIZE];

void usage()
{
    printf("redheads -l LISTEN_PORT -a PUB_MULTICAST_ADDR_A:PORT -b PUB_MULTICAST_ADDR_B:PORT");
    exit();
}

void make_socket_non_blocking(int sockFd)
{
    int getFlag, setFlag;

    getFlag = fcntl(sockFd, F_GETFL, 0);

    if(getFlag == -1)
    {
        LOG_ERROR("Cannot read socket settings");
        exit();
    }

    /* Set the Flag as Non Blocking Socket */
    getFlag |= O_NONBLOCK;

    setFlag = fcntl(sockFd, F_SETFL, getFlag);

    if(setFlag == -1)
    {
        LOG_ERROR("Cannot set socket settings");
        exit();
    }
}

int main()
{
    int length;

    int sockFdRecv, sockFdBrdA, sockFdBrdB;
    int optval = 1; 

    struct sockaddr_in recvAddr, brdAddrA, brdAddrB;

    int epollFd;      
    struct epoll_event ev;                  
    struct epoll_event events[MAX_EVENTS];               

    int listenPort = 0;
    int publishAddrA = 0;
    int publishAddrB = 0;
    int publishPortA = 0;
    int publishPortB = 0;

    opterr = 0;

    while ((c = getopt (argc, argv, "l:a:b:")) != -1)
    {
        switch (c)
        {
            case 'l':
            {
                int a,b,c,d,port;
                sscanf(optarg, "%d.%d.%d.%d:%d", a,b,c,d,port);
                listenPort = atoi(optarg);
            }
            break;
            case 'a':
            {
                publishPortA = atoi(optarg);
            }
            break;
            case 'b':
            {
                publishPortB = atoi(optarg);
            }
            break;
            default: usage();
        }
    }

    sockFdRecv = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockFdBrdA = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockFdBrdB = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

      /* Check socket is successful or not */
    if (sockFdRecv == -1 || sockFdBrdA == -1 || sockFdBrdB == -1)
    {
        LOG_ERROR(" Creating sockets failed");
        exit();
    }

    make_socket_non_blocking(sockRecv);
    make_socket_non_blocking(sockBrdA);
    make_socket_non_blocking(sockBrdB);

    if(setsockopt(sockBrdA, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval))== -1)
    {
        LOG_ERROR("setsockopt failed");
        return -1;
    }
    if(setsockopt(sockBrdB, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval))== -1)
    {
        LOG_ERROR("setsockopt failed");
        return -1;
    }

    memset(&recvAddr, 0, sizeof(recvAddr));
    recvAddr.sin_family = AF_INET;
    recvAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    recvAddr.sin_port = htons(listenPort);

    memset(&brdAddrA, 0, sizeof(brdAddrA));
    brdAddrA.sin_family = AF_INET;
    brdAddrA.sin_addr.s_addr = htonl(publishAddrA);
    brdAddrA.sin_port = htons(publishPortA);

    memset(&brdAddrB, 0, sizeof(brdAddrB));
    brdAddrB.sin_family = AF_INET;
    brdAddrB.sin_addr.s_addr = htonl(publishAddrB);
    brdAddrB.sin_port = htons(publishPortB);

    if(bind(sockRecv, (struct sockaddr*) &recvAddr, sizeof(recvAddr)) < 0)
    {
        LOG_ERROR("binding to listen addr failed");
        exit();
    }

    epollFd = epoll_create(3);
    if(epollFd == -1)
    {
        LOG_ERROR("creating epoll failed");
        exit();
    }

    ev.data.fd = sockFdRecv;
    ev.events = EPOLLIN | EPOLLET;
    if(epoll_ctl(epollFd, EPOLL_CTL_ADD, sockFdRecv, &ev) == -1)
    {
        LOG_ERROR("epoll_ctl");
        exit();
    }

    ev.data.fd = sockFdBrdA;
    ev.events = EPOLLOUT | EPOLLET;
    if(epoll_ctl(epollFd, EPOLL_CTL_ADD, sockFdBrdA, &ev) == -1)
    {
        LOG_ERROR("epoll_ctl");
        exit();
    }

    ev.data.fd = sockFdBrdB;
    ev.events = EPOLLOUT | EPOLLET;
    if(epoll_ctl(epollFd, EPOLL_CTL_ADD, sockFdBrdB, &ev) == -1)
    {
        LOG_ERROR("epoll_ctl");
        exit();
    }

    Engine engine;
    engine.Init(1000, 100000, 500);

    while(true)
    {
        int numEvents = epoll_wait(epollFd, events, MAX_EVENTS, 0);
        for (int i = 0; i < numEvents; i++)
        {
            if ((events[i].events & EPOLLERR) ||
               (events[i].events & EPOLLHUP) ||
               (!(events[i].events & EPOLLIN)))
            {
               /* An error has occured on this fd, or  the socket is not
                * ready for reading (why were we notified then?)
                */
               LOG_ERROR("epoll error");
               close(events[i].data.fd);
               continue;
            }
            /* We have data on the fd waiting to be read. Read and
            * display it. We must read whatever data is available
            * completely, as we are running in edge-triggered mode
            * and won't get a notification again for the same data.
            */
            else if ((events[i].events & EPOLLIN) && (sockFdRecv == events[i].data.fd))
            {
                while (1)
                {
                    /* Recieve the Data from Other system */
                    if ((length = recvfrom(sockFdRecv, IN_BUF, BUFFSIZE, 0, NULL, NULL)) < 0)
                    {
                       LOG_ERROR("recvfrom");
                       return -1;
                    }
                    else if(length == 0)
                    {
                       break;
                    }
                    else
                    {
                        // epoll udp
                        //engine.HandleMsg();
                        //if(periodicIdleJob)
                        //{
                        //    engine.ImmediateCleanup();
                        //}
                        /* Print The data */
                        //printf("Recvd Byte length : %d",  length);
                        //dumpData(IN_BUF, length);
                    }
                }
            }
        }
    }

    close(sockFdRecv);
    close(sockFdBrdA);
    close(sockFdBrdB);

    return 0;
}

