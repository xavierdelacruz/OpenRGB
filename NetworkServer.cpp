#include "NetworkServer.h"
#include <cstring>

//Include thread libraries for Windows or Linux
#ifdef WIN32
#include <process.h>
#else
#include "pthread.h"
#include "unistd.h"
#endif

//Thread functions have different types in Windows and Linux
#ifdef WIN32
#define THREAD static void
#define THREADRETURN
#else
#define THREAD static void*
#define THREADRETURN return(NULL);
#endif

#ifdef WIN32
#include <Windows.h>
#else
#include <unistd.h>

static void Sleep(unsigned int milliseconds)
{
    usleep(1000 * milliseconds);
}
#endif

typedef struct listen_thread_param_type
{
    NetworkServer * this_ptr;
    SOCKET *        sock_ptr;
};

THREAD connection_thread(void *param)
{
    NetworkServer* server = static_cast<NetworkServer*>(param);
    server->ConnectionThread();
    THREADRETURN
}

THREAD listen_thread(void *param)
{
    NetworkServer* server = static_cast<listen_thread_param_type*>(param)->this_ptr;
    SOCKET*        sock   = static_cast<listen_thread_param_type*>(param)->sock_ptr;
    server->ListenThread(sock);
    THREADRETURN
}

NetworkServer::NetworkServer(std::vector<RGBController *>& control) : controllers(control)
{
    //Start a TCP server and launch threads
    port.tcp_server("1337");

    //Start the connection thread
#ifdef WIN32
    _beginthread(connection_thread, 0, this);
#else
    pthread_t thread;
    pthread_create(&thread, NULL, &connection_thread, this);
#endif
}

void NetworkServer::ConnectionThread()
{
    //This thread handles client connections

    printf("Network connection thread started\n");
    while(1)
    {
        SOCKET * client_sock = port.tcp_server_listen();
        //Start a listener thread for the new client socket
#ifdef WIN32
        listen_thread_param_type new_thread_param;
        new_thread_param.sock_ptr = client_sock;
        new_thread_param.this_ptr = this;
        _beginthread(listen_thread, 0, &new_thread_param);
#else
        pthread_t thread;

        listen_thread_param_type new_thread_param;
        new_thread_param.sock_ptr = client_sock;
        new_thread_param.this_ptr = this;
        pthread_create(&thread, NULL, &listen_thread, &new_thread_param);
#endif
    }
}

void NetworkServer::ListenThread(SOCKET * client_sock)
{
    printf("Network server started\n");
    //This thread handles messages received from clients
    while(1)
    {
        NetPacketHeader header;
        int             bytes_read  = 0;
        char *          data        = NULL;

        //Read first byte of magic
        bytes_read = recv(*client_sock, &header.pkt_magic[0], 1, 0);

        if(bytes_read == 0)
        {
            break;
        }

        //Test first character of magic - 'O'
        if(header.pkt_magic[0] != 'O')
        {
            continue;
        }

        //Read second byte of magic
        bytes_read = recv(*client_sock, &header.pkt_magic[1], 1, 0);

        if(bytes_read == 0)
        {
            break;
        }

        //Test second character of magic - 'R'
        if(header.pkt_magic[1] != 'R')
        {
            continue;
        }

        //Read third byte of magic
        bytes_read = recv(*client_sock, &header.pkt_magic[2], 1, 0);

        if(bytes_read == 0)
        {
            break;
        }

        //Test third character of magic - 'G'
        if(header.pkt_magic[2] != 'G')
        {
            continue;
        }

        //Read fourth byte of magic
        bytes_read = recv(*client_sock, &header.pkt_magic[3], 1, 0);

        if(bytes_read == 0)
        {
            break;
        }

        //Test fourth character of magic - 'B'
        if(header.pkt_magic[3] != 'B')
        {
            continue;
        }

        //If we get to this point, the magic is correct.  Read the rest of the header
        bytes_read = 0;
        do
        {
            bytes_read += recv(*client_sock, (char *)&header.pkt_dev_idx + bytes_read, sizeof(header) - sizeof(header.pkt_magic) - bytes_read, 0);

            if(bytes_read == 0)
            {
                break;
            }
        } while(bytes_read != sizeof(header) - sizeof(header.pkt_magic));

        //printf( "Server: Received header, now receiving data of size %d\r\n", header.pkt_size);

        //Header received, now receive the data
        if(header.pkt_size > 0)
        {
            unsigned int bytes_read = 0;

            data = new char[header.pkt_size];

            do
            {
                bytes_read += recv(*client_sock, &data[bytes_read], header.pkt_size - bytes_read, 0);
            } while (bytes_read < header.pkt_size);
        }

        //printf( "Server: Received header and data\r\n" );
        //printf( "Server: Packet ID: %d \r\n", header.pkt_id);

        //Entire request received, select functionality based on request ID
        switch(header.pkt_id)
        {
            case NET_PACKET_ID_REQUEST_CONTROLLER_COUNT:
                //printf( "NET_PACKET_ID_REQUEST_CONTROLLER_COUNT\r\n" );
                SendReply_ControllerCount(client_sock);
                break;

            case NET_PACKET_ID_REQUEST_CONTROLLER_DATA:
                //printf( "NET_PACKET_ID_REQUEST_CONTROLLER_DATA\r\n" );
                SendReply_ControllerData(client_sock, header.pkt_dev_idx);
                break;

            case NET_PACKET_ID_RGBCONTROLLER_RESIZEZONE:
                //printf( "NET_PACKET_ID_RGBCONTROLLER_RESIZEZONE\r\n" );

                if((header.pkt_dev_idx < controllers.size()) && (header.pkt_size == (2 * sizeof(int))))
                {
                    int zone;
                    int new_size;

                    memcpy(&zone, data, sizeof(int));
                    memcpy(&new_size, data + sizeof(int), sizeof(int));

                    controllers[header.pkt_dev_idx]->ResizeZone(zone, new_size);
                }
                break;

            case NET_PACKET_ID_RGBCONTROLLER_UPDATELEDS:
                //printf( "NET_PACKET_ID_RGBCONTROLLER_UPDATELEDS\r\n" );

                if(header.pkt_dev_idx < controllers.size())
                {
                    controllers[header.pkt_dev_idx]->SetColorDescription((unsigned char *)data);
                    controllers[header.pkt_dev_idx]->UpdateLEDs();
                }
                break;

            case NET_PACKET_ID_RGBCONTROLLER_UPDATEZONELEDS:
                //printf( "NET_PACKET_ID_RGBCONTROLLER_UPDATEZONELEDS\r\n" );

                if(header.pkt_dev_idx < controllers.size())
                {
                    int zone;

                    memcpy(&zone, &data[sizeof(unsigned int)], sizeof(int));

                    controllers[header.pkt_dev_idx]->SetZoneColorDescription((unsigned char *)data);
                    controllers[header.pkt_dev_idx]->UpdateZoneLEDs(zone);
                }
                break;

            case NET_PACKET_ID_RGBCONTROLLER_UPDATESINGLELED:
                //printf( "NET_PACKET_ID_RGBCONTROLLER_UPDATESINGLELED\r\n" );

                if(header.pkt_dev_idx < controllers.size())
                {
                    int led;

                    memcpy(&led, data, sizeof(int));

                    controllers[header.pkt_dev_idx]->SetSingleLEDColorDescription((unsigned char *)data);
                    controllers[header.pkt_dev_idx]->UpdateSingleLED(led);
                }
                break;

            case NET_PACKET_ID_RGBCONTROLLER_SETCUSTOMMODE:
                //printf( "NET_PACKET_ID_RGBCONTROLLER_SETCUSTOMMODE\r\n" );

                if(header.pkt_dev_idx < controllers.size())
                {
                    controllers[header.pkt_dev_idx]->SetCustomMode();
                }
                break;

            case NET_PACKET_ID_RGBCONTROLLER_UPDATEMODE:
                //printf( "NET_PACKET_ID_RGBCONTROLLER_UPDATEMODE\r\n" );

                if(header.pkt_dev_idx < controllers.size())
                {
                    controllers[header.pkt_dev_idx]->SetModeDescription((unsigned char *)data);
                    controllers[header.pkt_dev_idx]->UpdateMode();
                }
                break;
        }

        delete[] data;
    }

    printf("Server connection closed\r\n");
}

void NetworkServer::SendReply_ControllerCount(SOCKET * client_sock)
{
    NetPacketHeader reply_hdr;
    unsigned int    reply_data;

    reply_hdr.pkt_magic[0] = 'O';
    reply_hdr.pkt_magic[1] = 'R';
    reply_hdr.pkt_magic[2] = 'G';
    reply_hdr.pkt_magic[3] = 'B';

    reply_hdr.pkt_dev_idx  = 0;
    reply_hdr.pkt_id       = NET_PACKET_ID_REQUEST_CONTROLLER_COUNT;
    reply_hdr.pkt_size     = sizeof(unsigned int);

    reply_data             = controllers.size();

    send(*client_sock, (const char *)&reply_hdr, sizeof(NetPacketHeader), 0);
    send(*client_sock, (const char *)&reply_data, sizeof(unsigned int), 0);
}

void NetworkServer::SendReply_ControllerData(SOCKET * client_sock, unsigned int dev_idx)
{
    if(dev_idx < controllers.size())
    {
        NetPacketHeader reply_hdr;
        unsigned char *reply_data = controllers[dev_idx]->GetDeviceDescription();
        unsigned int   reply_size;

        memcpy(&reply_size, reply_data, sizeof(reply_size));
        
        reply_hdr.pkt_magic[0] = 'O';
        reply_hdr.pkt_magic[1] = 'R';
        reply_hdr.pkt_magic[2] = 'G';
        reply_hdr.pkt_magic[3] = 'B';

        reply_hdr.pkt_dev_idx  = dev_idx;
        reply_hdr.pkt_id       = NET_PACKET_ID_REQUEST_CONTROLLER_DATA;
        reply_hdr.pkt_size     = reply_size;

        send(*client_sock, (const char *)&reply_hdr, sizeof(NetPacketHeader), 0);
        send(*client_sock, (const char *)reply_data, reply_size, 0);
    }
}
