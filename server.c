/*
    Central Server.
    This server has to be run before any peer.c files.
    Job of server to Handle Join, Publish and Search request comes from the peer.
*/
#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<sys/types.h>
#include<unistd.h>
#include<arpa/inet.h>
#include<sys/socket.h>
#include <netinet/in.h> // for sockaddr_in
#include <netinet/ip.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>
#include <sys/time.h>

#define BACKLOG 100
#define MAXPEER 100
#define MAXBUFFER 512
#define FILENAMEMAX 100
#define STRINGSIZE 100
#define HEARTBEATRATE 15
#define JOIN 1
#define PUBLISH 2
#define FETCH 3

/*******************[ Structure]**********************************/
typedef struct server_socket {

    int sock_descripter;
    int port;
    char ip[STRINGSIZE];
}serv_sock;

typedef struct published_file_info
{
    char file_name[FILENAMEMAX];
    char ip[STRINGSIZE];
    char id[STRINGSIZE];
    int port;
}published_file_info;

/*
    Joined Peer structure
*/
typedef struct peer_info
{
    char id[STRINGSIZE];
    char ip[STRINGSIZE];
    int port;
    unsigned long long last_ping_time; //MillisecondsSinceEpoch
}peer_info;

typedef struct joined_peer
{
    peer_info *peer_list[2];
    int peer_count;
}joined_peer;

/****************[Constants]*******************************/

char *default_folder="p2p-server";
char *publish_db="published_file.db";
joined_peer *live_peers;
char server_ip[STRINGSIZE];
int server_port;
/******************[Methds decl]****************************/

int startServer(int port);
serv_sock getTcpServerSocket(int port);
void *threadWaitForPeerConnect(void *arg_sfd);
void *threadProcessPeer(void *param);

void handleJoinRequest(int sfd,char *data);
int addPeerToList(peer_info *peer);
int isPeerLive(char *id);
peer_info* getLivePeerInfo(char *id);

void handlePublishRequest(int sfd,char *data);
int savePublishFile(published_file_info p_file);

void handleFetchRequest(int sfd,char *data);

void showAllPublishedFileInfo();
void showAllActivePeer();

void menu();
void menu_metdata();
/*********************[Common method]**************************/
/*
    Clear the input stream
    param:
        data and size of array
*/
int clearStream(char *data,int size)
{
    fflush(stdout);
    memset(data, 0, size);
}
/*
    Generate global unique Identifier.
    return: global unique Id
*/
char *getGUID()
{
    char *id=(char *)malloc(sizeof(char)*100);
    struct timeval time;
    gettimeofday(&time, NULL);
    unsigned long long millisecondsSinceEpoch =(unsigned long long)(time.tv_sec) * 1000 +(unsigned long long)(time.tv_usec) / 1000;
    sprintf(id,"%llu",millisecondsSinceEpoch);
    return id;
}

/************************[Methods]***************************************/

/*
    Start the server.
    param:
        port number on which server has to be started
    return
        -1 if failed else socket descriptor
*/
int startServer(int port)
{
    int *sfd=(int *)malloc(sizeof(int));
    int nsfd;
    int sin_size=sizeof(struct sockaddr_in);
    char data[MAXBUFFER];
    struct sockaddr_in from_addr;

    serv_sock s_sock;
    s_sock=getTcpServerSocket(port);
    port=s_sock.port;
    *sfd=s_sock.sock_descripter;

    if(sfd<0)
        return -1;

    server_port=port;
    printf("Server started on IP %s and Port %d.\n",server_ip,server_port);
    pthread_t sthread_id;
    pthread_create(&sthread_id, NULL, threadWaitForPeerConnect, sfd);
    return *sfd;

    /*
    if(!fork())
    {
        threadWaitForPeerConnect(&sfd);
        exit(0);
    }*/
    /*
	while((nsfd=accept(sfd,(struct sockaddr*)&from_addr,&sin_size))!=-1)
	{
        pthread_t sthread_id;
        pthread_create(&sthread_id, NULL, threadProcessPeer, &nsfd);
	}
    close(sfd);
    */
}

/*
    Server Thread which will wait for other peer to connect for file fetch.
    Param:
        socket descriptor of the server.
    return
        NULL Pointer;
*/
void *threadWaitForPeerConnect(void *arg_sfd)
{
    struct sockaddr_in from_addr;
    int sfd,nsfd;
    int sin_size=sizeof(struct sockaddr_in);
    sfd=*(int *)arg_sfd;
    while((nsfd=accept(sfd,(struct sockaddr*)&from_addr,&sin_size))!=-1)
    {
        //printf("Peer connected...\n");
        pthread_t sthread_id;
        pthread_create(&sthread_id, NULL, threadProcessPeer, &nsfd);
    }
    printf("Server STOPPED. Please restart the application.\n");
    close(sfd);
    return NULL;
}
/*
    Creates the Tcp socket
    param:
        port number
    return
        socket discriptor
*/
serv_sock getTcpServerSocket(int port)
{
    struct sockaddr_in my_addr;
    int sfd=0,nsfd;
    serv_sock s_sock;
	s_sock.port=port;
    s_sock.sock_descripter=0;

	sfd=socket(PF_INET,SOCK_STREAM,0);
	my_addr.sin_family=AF_INET;
	my_addr.sin_port=htons(port);
    my_addr.sin_addr.s_addr=inet_addr(server_ip);
    memset(&(my_addr.sin_zero),'\0',8);
	// bind
	int status=bind(sfd,(struct sockaddr*)&my_addr,sizeof(struct sockaddr));
	if(status==-1)
	{
		printf("Error: Bind failure with port %d.\n",port);
		return s_sock;
	}

    // fetching port number on which server has started
	socklen_t len = sizeof(my_addr);
    if (getsockname(sfd, (struct sockaddr *)&my_addr, &len) == -1) {
        printf("Error: in getting Socket Name");
        return s_sock;
    }
	s_sock.port=ntohs(my_addr.sin_port);
    s_sock.sock_descripter=sfd;
    listen(sfd,BACKLOG);
	return s_sock;
}

/*
    Handles Peer's action like
        1. Join
        2. Publish
        3. Search
*/
void* threadProcessPeer(void *param)
{
    int nsfd=*((int *)param);
    unsigned char data[MAXBUFFER];
    char src_file[MAXBUFFER];
    long unsigned file_size;
    struct stat st;
    int chunk_count=0;

    // Waiting for "filename" from another peer.
    int r_byte=recv(nsfd,data,MAXBUFFER,0);
    data[r_byte]='\0';
    if(r_byte==0)
    {
        printf("Connection terminated !!!\n");
        close(nsfd);
        return NULL;
    }
    else if(r_byte==-1)
    {
         printf("Error: Failed in receving data. \n");
         close(nsfd);
         return NULL;
    }
    //printf("Received Data: %s; Data length:%d\n",data,r_byte);

    if(r_byte>1)
    {
        int type_id=data[0]-48;
        switch(type_id)
        {
            case JOIN:  handleJoinRequest(nsfd,data);
                        break;
            case PUBLISH: handlePublishRequest(nsfd,data);
                        break;
            case FETCH: handleFetchRequest(nsfd,data);
                        break;

        }
    }
    close(nsfd);
    return NULL;
}
/*
    Handle Join Request from the peer.
*/
void handleJoinRequest(int sfd,char *data)
{
    int type_id,port;
    char peer_ip[STRINGSIZE];
    char peer_id[STRINGSIZE];
    peer_info *peer= (peer_info *)malloc(sizeof(peer_info));

    sscanf(data,"%d %s %d %s",&type_id,peer_ip,&port,peer_id);
    if(strlen(peer_id) <= 1)
    {
        sprintf(peer_id,"%s:%s",peer_ip,getGUID());
    }
    send(sfd,peer_id,strlen(peer_id),0);
    strcpy(peer->id,peer_id);
    strcpy(peer->ip,peer_ip);
    peer->port=port;
    sscanf(getGUID(),"%llu",&(peer->last_ping_time));
    addPeerToList(peer);
}


/*
    Add peer to live peer list.
    param:
        peer information.
    return
        1 is able to successfully else 0
*/
int addPeerToList(peer_info *peer)
{
    int p=0;
    for(p=0;p<live_peers->peer_count;p++)
    {
        if(strcmp(peer->id,live_peers->peer_list[p]->id)==0)
        {
            live_peers->peer_list[p]=peer;
            return 1;
        }
    }
    if(live_peers->peer_count<MAXPEER)
    {
        live_peers->peer_list[live_peers->peer_count]=peer;
        live_peers->peer_count+=1;
        return 1;
    }
    return 0;
}
/*
    Check peer is live or not.
    Param:
        id: peer id.
    return:
        1 if it is live else 0
*/
int isPeerLive(char *id)
{
    int p=0;
    for(p=0;p<live_peers->peer_count;p++)
    {
        if(strcmp(id,live_peers->peer_list[p]->id)==0)
        {
            return 1;
        }
    }
    return 0;
}
/*
    Get Peer Info.
    Para:
        Peer id
    return
        peer info it is live else null

*/
peer_info* getLivePeerInfo(char *id)
{
    int p=0;
    for(p=0;p<live_peers->peer_count;p++)
    {
        int a=strcmp(id,live_peers->peer_list[p]->id);
        //printf("A:%d\tI1:%s\tI2:%s\n",a,id,live_peers->peer_list[p]->id);
        if(strcmp(id,live_peers->peer_list[p]->id)==0)
        {
            return live_peers->peer_list[p];
        }
    }
    return NULL;
}
/*
    Handle Publish Request from the peer;
*/
void handlePublishRequest(int sfd,char *data)
{
    int type_id,port;
    published_file_info pfile_info;
    sscanf(data,"%d %s %d  %s %s",&type_id,pfile_info.ip,&pfile_info.port,pfile_info.id,pfile_info.file_name);
    int status=savePublishFile(pfile_info);
}

/*
    Save Published file on the database.
    return
        1 on success and 0 on faliure;
*/
int savePublishFile(published_file_info p_file)
{
    char src_file[FILENAMEMAX];
    sprintf(src_file,"%s/%s",default_folder,publish_db);
    FILE *sfile;
    sfile=fopen(src_file,"a");
    if(!sfile)
    {
        printf("Error: File %s not found.\n",src_file);
        return 0;
    }
    fwrite(&p_file,sizeof(p_file),1,sfile);
    fclose(sfile);
    return 1;

}

/*
    Handle Fetch request from the server.
*/
void handleFetchRequest(int sfd,char *data)
{
    struct sockaddr_in p_addr;
    int type_id,p_port;
    char fetch_name[FILENAMEMAX];
    char sdata[MAXBUFFER];
    unsigned long long cur_time;
    sscanf(data,"%d %s",&type_id,fetch_name);
    //printf("File to be searched:%s\n",fetch_name);

    char src_file[FILENAMEMAX];
    published_file_info p_file;
    int r_byte,i=0;
    sprintf(src_file,"%s/%s",default_folder,publish_db);
    FILE *sfile;
    sfile=fopen(src_file,"r");
    if(!sfile)
    {
        printf("\nError: File %s not found.\n", src_file);
        fclose(sfile);
        return;
    }
    // fetching port number on which server has started

    while((r_byte=fread(&p_file,sizeof(published_file_info),1,sfile))>0)
    {
        if(strstr(p_file.file_name,fetch_name)!=NULL)
        {
            peer_info *lp=getLivePeerInfo(p_file.id);
            sscanf(getGUID(),"%llu",&cur_time);
            int is_peer_live= (lp!=NULL && (cur_time - lp->last_ping_time) <= HEARTBEATRATE*1000 ) ? 1:0;
            if(is_peer_live)
            {
                strcpy(p_file.ip,lp->ip);
                p_file.port=lp->port;
                sprintf(sdata,"%s %s %d %s",p_file.id,p_file.ip,p_file.port,p_file.file_name);
                send(sfd,sdata,strlen(sdata),0);
                recv(sfd,&data,MAXBUFFER,0);
            }
        }
    }
    fclose(sfile);
}

/*
    Display All published file
*/
void showAllPublishedFileInfo()
{
    char src_file[FILENAMEMAX];
    published_file_info p_file;
    int r_byte,i=0;
    sprintf(src_file,"%s/%s",default_folder,publish_db);
    FILE *sfile;
    sfile=fopen(src_file,"r");
    if(!sfile)
    {
        //printf("Error: File %s not found.\n", src_file);
        printf("\n-------------------------[Published Files]---------------------------------\n\n");
        printf("\n\tStatus: No file is published anyone peer.");
        printf("\n---------------------------------------------------------------------------\n");
        return;
    }
    printf("\n-------------------------[Published Files]---------------------------------\n\n");
    while((r_byte=fread(&p_file,sizeof(published_file_info),1,sfile))>0)
    {
        peer_info *lp=getLivePeerInfo(p_file.id);
        if(lp!=NULL)
                printf("%d) Ip: %s\tPort: %d\tFilename: %s\n",++i,lp->ip,lp->port,p_file.file_name);
        else
                printf("%d) Ip: %s\tPort: %d\tFilename: %s\n",++i,p_file.ip,p_file.port,p_file.file_name);

    }
    printf("\n---------------------------------------------------------------------------\n");
    fclose(sfile);
}
/*
    Dipaly all peers that are connected to this server.
*/
void showAllActivePeer()
{
    int p=0;
    unsigned long long cur_time;
    printf("\n-------------------------[Live Peers]--------------------------------------\n\n");
    sscanf(getGUID(),"%llu",&cur_time);
    for(p=0;p<live_peers->peer_count;p++)
    {
        int is_peer_live= (cur_time - live_peers->peer_list[p]->last_ping_time) <= HEARTBEATRATE*1000? 1:0;
        if(is_peer_live)
        {
            printf("%d)Ip:%s\tPort:%d\tId:%s\n",p+1,live_peers->peer_list[p]->ip,live_peers->peer_list[p]->port,live_peers->peer_list[p]->id);
        }
    }
    printf("\n---------------------------------------------------------------------------\n");
}
void menu_metdata()
{
    printf("\n------------------------[Server Actions]-----------------------------------\n");
    printf("\tServer Actions:\n");
    printf("\t\t1) View Peers\n");
    printf("\t\t2) View Published Files\n");
    printf("\t\t3) My Info\n");
    printf("\tNote: Enter 0 to Shutdown Server and <any number except above> to re-populate MENU\n");
    printf("-------------------------------------------------------------------------\n");

}
void myInfo()
{

    printf("\n------------------------[My Info]---------------------------------------\n");
    printf("\n\tIp\t:\t%s\n",server_ip);
    printf("\n\tPort\t:\t%d\n",server_port);
    printf("-------------------------------------------------------------------------\n");
}
/*
    Menu
*/
void menu()
{
    int act_id;
    menu_metdata();
    while(1)
    {
        printf("Your Action:");
        scanf("%d",&act_id);
        switch(act_id)
        {
            case 0: exit(1);
                    break;
            case 1: showAllActivePeer();
                    break;
            case 2: showAllPublishedFileInfo();
                    break;
            case 3: myInfo();
                    break;
            default:
                    menu_metdata();
                    break;
        }
    }
}
int main(int argc, char *argv[] )
{
    /*********************[INIT]********************************/
    live_peers=(joined_peer *)malloc(sizeof(joined_peer));
    live_peers->peer_count=0;
    strcpy(server_ip,"127.0.0.1");
    server_port=0;
    if(argc>1)
    {
        if(argc >= 2 && strlen(argv[1])>1)
        {
            strcpy(server_ip,argv[1]);
        }
        if(argc >= 3 && strlen(argv[2])>1)
        {
            server_port=atoi(argv[2]);
        }
    }
    // creating default folder
    struct stat st = {0};
    if (stat(default_folder, &st) == -1) {
        mkdir(default_folder, 0777);
    }
    /***********************************************************/
    int stat=startServer(server_port);
    if(stat!=-1)
    {
        menu();
    }
    printf("\n");
	return 0;
}
