
#include <cstdio>
#include <cunistd>

void usage()
{
    printf("redheads -l LISTEN_PORT -a PUB_MULTICAST_ADDR_A:PORT -b PUB_MULTICAST_ADDR_B:PORT");
    exit();
}

int main (int argc, char **argv)
{
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

    Engine engine;
    engine.Init(1000, 100000, 500);

    while(true)
    {
        // epoll udp
        engine.HandleMsg();
        if(periodicIdleJob)
        {
            engine.ImmediateCleanup();
        }
    }
}
