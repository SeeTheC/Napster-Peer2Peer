/*
    Peer Application.
    Peer can download files from other server. Also It uploads files for the other server.
    It will communicate with other peers with "server.c" (central server) for joining the
    n/w, publish and fetching files from other server.
    Job of server to Handle Join, Publish and Search request comes form the peer.
*/
#include<stdio.h>
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
#include <math.h>
#include <pthread.h>
#include <string.h>
#include <libgen.h> // for basename and dirname
#include <sys/time.h>

#define BACKLOG 100
#define SEARCHLISTSIZE 100
#define MAXBUFFER 512
#define FILENAMEMAX 100
#define STRINGSIZE 100
#define HEARTBEATRATE 15 // In sec. Used for pining ther central server
#define JOIN 1
#define PUBLISH 2
#define FETCH 3

/*******************[Structure]****************************/

typedef struct server_socket {

    int sock_descripter;
    int port;
    char ip[STRINGSIZE];
    /*
        Every peer is given a unique id to identity among distinguish among differrent peer.
        If peers are different machine then using ip address we can distinquished them. But
        if peers or same machine then this id will be usefull.
    */
    char id[STRINGSIZE];

}serv_sock;

typedef struct file_info
{
    char file_path[256];
    char file_name[100];
}file_info;

typedef struct published_file_info
{
    char file_name[FILENAMEMAX];
    char ip[STRINGSIZE];
    char id[STRINGSIZE];
    int port;
}published_file_info;

/****************[Constants]*******************************/

char *default_folder="p2p-file";
char *file_metadata="fmetadata.b";
char *peer_metadata="pmetadata.b";
serv_sock central_server, my_server;

/******************[Methds decl]****************************/

void startServer(int port);
serv_sock getTcpServerSocket(int port);
void *threadWaitForPeerConnect(void *arg_sfd);
void* threadSendFileTo(void* arg_sfd);

int joinOverlayNet();
char *getPeerId();
int savePeerId(char *id);
void* threadHeartBeat(void *argv);

int publishFile();
char* getFileInfo(char *filename);

int fetchFile();
void* threadGetFileFrom(void *published_file_info);
int connectPeer(char *peer_ip,int peer_port);
char* getFileInfo(char *file_name);

void menu();
void menu_metdata();

/*********************[Common method]**************************/

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
    Start Peer as Server.
    It will allow other peer to connect with it and
    download file from this peer.
*/
void startServer(int port)
{
    int *sfd=(int *)malloc(sizeof(int));
    serv_sock s_sock;
    s_sock=getTcpServerSocket(port);
    port=s_sock.port;
    *sfd=s_sock.sock_descripter;

    if(sfd<0)
        return;

    my_server.port=port;
    my_server.sock_descripter=*sfd;

    /*
        Creating a thread which will wait for other peer to connect.
        So that file transfer can be done.
    */
    printf("Peer started on IP %s and Port %d.\n",my_server.ip,my_server.port);
    pthread_t sthread_id;
    pthread_create(&sthread_id, NULL, threadWaitForPeerConnect, sfd);

    /*
    if(!fork())
    {
        threadWaitForPeerConnect(&sfd);
        exit(0);
    }
    */


}

/*
    Creates the TCP socket for acting as the Server.
    Param:
        port: server port number
    return:
        obj of serv_sock containg socket discriptor and port number on which this peer
        has started.
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
	my_addr.sin_addr.s_addr=inet_addr(my_server.ip);
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
    //printf("#####sfd:%d\n",sfd);
    while((nsfd=accept(sfd,(struct sockaddr*)&from_addr,&sin_size))!=-1)
    {
        int *d_nsfd=(int *)malloc(sizeof(int));
        *d_nsfd=nsfd;
        pthread_t thread_sendto;
        pthread_create(&thread_sendto, NULL, threadSendFileTo, d_nsfd);
        /*
        if(!fork())
        {
            close(sfd);
            threadSendFileTo(nsfd);
            exit(0);
        }
        close(nsfd);
        */
    }
    close(sfd);
    printf("Server STOPPED. Please restart the application.\n");
    return NULL;
}

/*
    Send file to the Peer asked by it.
    Param:
        nfsd: new peer socket descriptor to whom file is being sent.
    return:
        -1 on error and > 0 on success

*/
void* threadSendFileTo(void* arg_sfd)
{
    //printf("Connected for download....\n");
    int nsfd=*(int *)arg_sfd;
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
        return NULL;
    }
    else if(r_byte==-1)
    {
         printf("Error: Failed in receving data. \n");
         return NULL;
    }
    //printf("Received Data: %s;\n Data length:%d\n",data,r_byte);
    strcpy(src_file,getFileInfo(data));
    if(src_file==NULL)
    {
        printf("\nError: File not found.\n");
        close(nsfd);
        return NULL;
    }
    // checking wether file exists or not.
    FILE *sfile;
    sfile=fopen(src_file,"rb");
    if(!sfile)
    {
        printf("Error: File not found.\n");
        close(nsfd);
        return NULL;
    }

    // sending file size to the peer
    stat(src_file,&st);
    file_size=st.st_size;
    sprintf(data, "%ld", file_size);
    int s_byte=send(nsfd,data,strlen(data),0);
    if(s_byte==-1)
    {
        printf("Error: Failed in sending\n");
        return NULL;
    }

    // sending file data to the peer
    int total_s_byte=0;
    while((r_byte=fread(data,1,MAXBUFFER,sfile))>0)
    {
        s_byte=send(nsfd,data,r_byte,0);
        if(s_byte==-1)
        {
            printf("Error: Failed in sending\n");
            break;
        }
        total_s_byte+=s_byte;
    }
    printf("Total byte sent:%d\n",total_s_byte);
    recv(nsfd,data,MAXBUFFER,0);
    fclose(sfile);
    close(nsfd);
    return NULL;
}


/*
    Connects to the peer for having ip as peer_ip and port as peer_port via TCP
    Param:
            peer_id: other peer ip with whom this peer will connect;
            peer_port: other peer port with whom this peer will connect;

    return:
        socket descriptor after connection else 0 on error

*/
int connectPeer(char *peer_ip,int peer_port)
{
    int sfd=0;
	struct sockaddr_in addr;
	sfd=socket(PF_INET,SOCK_STREAM,0);

	addr.sin_family=AF_INET;
	addr.sin_port=htons(peer_port);
	addr.sin_addr.s_addr=inet_addr(peer_ip);
	memset(&(addr.sin_zero),'\0',8);

    int c_status=connect(sfd,(struct sockaddr*)&addr,sizeof(struct sockaddr));
    if(c_status==-1)
    {
        //printf("\nError: Failed in connecting with peer %s:%d\n",peer_ip,peer_port);
        return -1;
    }
    return sfd;
}
/*
    Trys to fetchs specified file from other specfied peer.
    Param:
        file_name: name of file to fetch;
        peer_ip: peer ip address
        peer_port: peer port number
    return:
        0 on error and 1 on success.

*/
void* threadGetFileFrom(void *file_info)
{
    sleep(1);
    published_file_info *dfile_info=(published_file_info *)file_info;
    char file_name[FILENAMEMAX];
    char peer_ip[STRINGSIZE];
    int peer_port;
    int *op_stat=(int *)malloc(sizeof(int));

    *op_stat=0;
    strcpy(file_name,dfile_info->file_name);
    strcpy(peer_ip,dfile_info->ip);
    peer_port=dfile_info->port;

    unsigned char data[MAXBUFFER];
    int r_byte=0,s_byte=0;
    int sfd=connectPeer(peer_ip,peer_port);
    size_t file_size;
    if(sfd==-1)
    {
        printf("\tError: Failed to connect with the peer %s:%d\n",peer_ip,peer_port);
        return op_stat;
    }
    //printf("\tConnected successfully !!\n");
    clearStream(data,MAXBUFFER);

    // sending filename
    strcpy(data,file_name);
	s_byte=send(sfd,data,strlen(data),0);
    if(s_byte==-1)
	{
		printf("\tError: Failed in initiating file download(code:S). \n");
        return op_stat;
	}

    // clearing of data stream
    clearStream(data,MAXBUFFER);

    // getting file info
	r_byte=recv(sfd,data,MAXBUFFER,0);
    if(r_byte==0)
    {
        printf("\n\tError: File not found on server.\n");
        return op_stat;
    }
    else if(r_byte==-1)
    {
         printf("\tError: Failed in initiating file download(code:R). \n");
        return op_stat;
    }
    file_size=atol(data);

    // fetching whole file
    FILE *dfile;
    char dstname[FILENAMEMAX];
    sprintf(dstname,"%s/d%s_%s",default_folder,getGUID(),file_name);
    dfile=fopen(dstname,"wb");
    size_t total_r_byte=0;
    //printf("\n\tStarting File download ....\n\t  File name: %s \n\t  File size: %ld\n",file_name,file_size);
    while ((r_byte=recv(sfd,data,MAXBUFFER,0)) > 0)
    {
         fwrite(data, 1, r_byte , dfile);
         total_r_byte+=r_byte;
         if(file_size == total_r_byte)
         {
            send(sfd,"ack",4,0);
            break;
         }
    }
    printf("\n------------------------#[Download Status]#-----------------------------------\n");
    if(file_size == total_r_byte)
    {
        printf("\n\tFile '%s' downloaded Successfully !!!\n",file_name);
    }
    else
    {
        printf("\n\tError: Failed in downloading file %s \n",file_name);
    }
    printf("\n------------------------------------------------------------------------------\n");

    fclose(dfile);
    close(sfd);
    *op_stat=1;
    return op_stat;
}

/*
    Peer telling server its presence in the network
    return
        0 in failed to connect the server else 1 on success.
*/
int joinOverlayNet()
{
    unsigned char data[MAXBUFFER];
    char id[STRINGSIZE];
    int r_byte=0,s_byte=-1;
    // connect to central server.
    int sfd=connectPeer(central_server.ip,central_server.port);
    strcpy(id,getPeerId());

    //registering its info on central server.
    sprintf(data,"%d %s %d %s",JOIN,my_server.ip,my_server.port,id);
    s_byte=send(sfd,data,strlen(data),0);
    if(s_byte==-1)
    {
        printf("\nError: Failed in connecting with the central server %s:%d \n",central_server.ip,central_server.port);
        return 0;
    }
    clearStream(data,sizeof(data));

    // wating for ack from server
    r_byte=recv(sfd,data,MAXBUFFER,0);
    data[r_byte]='\0';
    if(r_byte==0)
    {
        printf("Connection terminated !!!\n");
        return 0;
    }
    else if(r_byte==-1)
    {
         printf("\nError: Failed in receving data. \n");
         return 0;
    }
    strcpy(my_server.id,data);
    close(sfd);

    // saving id into the file
    if(!savePeerId(my_server.id))
    {
        return 0;
    }
    return 1;
}

/*
    Trys to fetch peer id.
    return:
        Id of peer is old peer of the n/w else 0 is case of new.
*/
char *getPeerId()
{
    char src_file[FILENAMEMAX];
    char *data=(char *)malloc(sizeof(char)*MAXBUFFER);
    int r_byte=0;
    sprintf(src_file,"%s/%s",default_folder,peer_metadata);
    FILE *sfile;
    sfile=fopen(src_file,"r");
    if(!sfile)
    {
        return "0";
    }
    data=fgets(data,MAXBUFFER,sfile);
    fclose(sfile);
    return data;
}
/*
    Saves peer id into the file.
    return
        1 on success else 0.
*/
int savePeerId(char *id)
{
    char src_file[FILENAMEMAX];
    int s_byte=0;
    sprintf(src_file,"%s/%s",default_folder,peer_metadata);
    FILE *sfile;
    sfile=fopen(src_file,"w");
    if(!sfile)
    {
        return 0;
    }
    s_byte=fputs(id,sfile);
    fclose(sfile);
    return 1;
}


/*
    Fetchs File from the another peer.
    return
        1 on sucess else 0 on failure
*/
int fetchFile()
{
    char file_name[FILENAMEMAX];
    char data[MAXBUFFER];
    published_file_info list[SEARCHLISTSIZE];
    int op_port;
    int is_download_success=0;
    printf("\nEnter file name to be fetched(going back--> 0):");
    scanf("%s",file_name);
    if(strcmp(file_name,"0")==0)// going back
        return 1;

    int r_byte=0,s_byte=-1,i=0;
    // connect to central server.
    int sfd=connectPeer(central_server.ip,central_server.port);

    //seaching peer for the given file on the central server
    sprintf(data,"%d %s",FETCH,file_name);
    s_byte=send(sfd,data,strlen(data),0);
    if(s_byte==-1)
    {
        printf("\nError: Failed in contacting the central server %s:%d \n",central_server.ip,central_server.port);
        return 0;
    }
    clearStream(data,sizeof(data));

    // getting peer info who has the file the server.
    printf("\n");
    int no_files=0;
    while((r_byte=recv(sfd,&data,MAXBUFFER,0))>0)
    {
        published_file_info *p_file=(published_file_info *)malloc(sizeof(published_file_info));
        sscanf(data,"%s %s %d %s",(*p_file).id,(*p_file).ip,&(*p_file).port,(*p_file).file_name);
        printf("%d) IP:%s\t Port:%d\t Filename:%s\n",no_files+1,(*p_file).ip,(*p_file).port,(*p_file).file_name);
        send(sfd,"ack",4,0);
        list[no_files]=*p_file;
        no_files++;
    }

    // fetch file from peer
    if(no_files>0)
    {
        do
        {
            is_download_success=0;
            int peer_no;
            printf("\nNote: Enter 0 to exit and go to Menu.\n");
            printf("Enter peer(1 to %d) from which you to download:",no_files);
            scanf("%d",&peer_no);
            if(peer_no <=0)
            {
               break;
            }
            else
            if(peer_no<=no_files)
            {
                published_file_info *dfile_info=(published_file_info *)malloc(sizeof(published_file_info));
                strcpy(dfile_info->file_name,list[peer_no-1].file_name);
                strcpy(dfile_info->ip,list[peer_no-1].ip);
                dfile_info->port=list[peer_no-1].port;
                pthread_t thread;
                pthread_create(&thread, NULL, threadGetFileFrom, dfile_info);
                //getFileFrom(dfile_info);
                break;
            }
        }while(is_download_success!=1);

    }
    else{
        printf("\nStatus: NO file with such name found.\n");
    }
    close(sfd);
    return 1;
}
/*
    Publish file that has to be shared among different peers.
*/
int publishFile()
{
    char full_filename[2*FILENAMEMAX];
    char dir_path[2*FILENAMEMAX];
    printf("\nNote: Give complete file name with path or only file name if is at default path \"%s\"\n",default_folder);
    printf("Enter file (going back --> 0 ):");
    scanf(" %[^\n]s",full_filename);
    if(strcmp(full_filename,"0")==0) // back to menu
        return 1;

    file_info p_file;
    strcpy(dir_path,full_filename);
    char *dir_name=dirname(dir_path);
    if(strcmp(dir_name,".")==0 || dir_name==NULL || strlen(dir_name)==0)
    {
        dir_name=default_folder;
    }
    else
    {
        sprintf(dir_name,"%s/",dir_name);
    }
    //printf("dir_name:%s\n",dir_name);
    strcpy(p_file.file_name,basename(full_filename));
    strcpy(p_file.file_path,dir_name);

    // saving published file info locally
    char src_file[FILENAMEMAX];
    sprintf(src_file,"%s/%s",default_folder,file_metadata);
    FILE *sfile;
    sfile=fopen(src_file,"a");
    if(!sfile)
    {
        printf("Error: File not found.\n");
        return 0;
    }
    fwrite(&p_file,sizeof(p_file),1,sfile);
    fclose(sfile);

    //publishing file on server
    unsigned char data[MAXBUFFER];
    int s_byte=-1;
    int sfd=connectPeer(central_server.ip,central_server.port);
    sprintf(data,"%d %s %d %s %s",PUBLISH,my_server.ip,my_server.port,my_server.id,p_file.file_name);
    s_byte=send(sfd,data,strlen(data),0);
    if(s_byte==-1)
    {
        printf("Error: Failed in publishing the file");
        return 0;
    }
    close(sfd);
    printf("\nStatus: File %s published successfully.\n",p_file.file_name);
    return 1;
}
char* getFileInfo(char *filename)
{

    char src_file[FILENAMEMAX];
    file_info p_file;
    int r_byte,i=0;
    sprintf(src_file,"%s/%s",default_folder,file_metadata);
    FILE *sfile;
    sfile=fopen(src_file,"r");
    if(!sfile)
    {
        printf("\nError: File %s not found.\n",src_file);
        return NULL;
    }
    while((r_byte=fread(&p_file,sizeof(file_info),1,sfile))>0)
    {
        //printf("%d) Path:%s\t filename:%s\n",++i,p_file.file_path,p_file.file_name);
        if(strcmp(filename,p_file.file_name)==0)
        {
            char *full_name=(char *)malloc(sizeof(char)*FILENAMEMAX);
            sprintf(full_name,"%s/%s",p_file.file_path,p_file.file_name);
            fclose(sfile);
            return full_name;
        }
    }
    fclose(sfile);
    return NULL;
}
void* threadHeartBeat(void *argv)
{
    int stat=1;
    while(stat)
    {
        sleep(HEARTBEATRATE);
        stat = joinOverlayNet();
    }
    //printf("\nError: Central Server is DOWN. Please try to reconnect to server by restart your application.");
}

void myInfo()
{
    printf("\n------------------------[Peer Info]-------------------------------------\n");
    printf("\n\tIp\t:\t%s\n",my_server.ip);
    printf("\n\tPort\t:\t%d\n",my_server.port);
    printf("\n\tId\t:\t%s\n",my_server.id);
    printf("\n-------------------------------------------------------------------------\n");
}

/*
    Prints Menu Actions list
*/
void menu_metdata()
{
    printf("\n------------------------[Peer Actions]-----------------------------------\n");
    printf("\tPeer Actions:\n");
    printf("\t\t1) Publish\n");
    printf("\t\t2) Fetch file\n");
    printf("\t\t3) My Info\n");
    printf("\tNote: Enter 0 to EXIT and <any number except above> to re-populate MENU\n");
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
            case 1: printf("\n----------------------------[Publish File]------------------------------\n");
                    publishFile();
                    printf("\n-------------------------------------------------------------------------\n");
                    break;
            case 2: printf("\n----------------------------[Fetch File]---------------------------------\n");
                    fetchFile();
                    printf("\n-------------------------------------------------------------------------\n");
                    break;
            case 3: myInfo();
                    break;
            default:menu_metdata();
                    break;
        }
    }
}
int main(int argc, char *argv[] )
{
    /***********INIT******************/
    strcpy(central_server.ip,argv[1]);
    central_server.port=atoi(argv[2]);
    strcpy(my_server.ip,"127.0.0.1");
    if(argc>3)
    {
        if(argc>=4 && strlen(argv[3])>1)
        {
            strcpy(my_server.ip,argv[3]);
        }
    }

    // creating default folder
    struct stat st = {0};
    if (stat(default_folder, &st) == -1) {
        mkdir(default_folder, 0777);
    }
    /***********************************/
    startServer(0);
    int stat = joinOverlayNet();
    if(stat)
    {
        pthread_t thread_hb;
        pthread_create(&thread_hb, NULL, threadHeartBeat, NULL);
        menu();
    }
    printf("\n");
	return 0;
}
