#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <ctype.h>
#include <iostream>
#include <dirent.h>
#include <vector>
#include <algorithm>
#include <fstream>
using namespace std;

#define BUFFLEN 512
#define QUIT_MESSAGE "+OK GOODBYE\r\n"
#define UNKNOWN_MESSAGE "-ERR UNKNOWN_MESSAGE\r\n"
#define GREETING_MESSAGE "220 localhost Simple Mail Transfer Service Ready\r\n"
#define EXIT_MESSAGE "- ERR Server shutting down\r\n"
#define NUM_CONNECTIONS 100 // Max possible concurrent Connections
#define MAX_USER_HOST_LENGTH 64

//REPLY CODES

#define DATA_START_GIVING "354 Start Mail input; end with <CRLF>.<CRLF>\r\n"
#define MAIL_BOX_BUSY "450 Requested mail Action Not Taken\r\n" //mail box busy

#define COMMAND_OKAY "250 Requested mail action okay,completed\r\n"
//#define START_CHANNEL "220 localhost Service Ready\n"
#define CLOSE_CHANNEL "221 Service Closing Transmission Channel\r\n" //QUIT
#define FORWARDING "251 User not local; will forward to <forward-path>\r\n"

#define SYNTAX_ERROR_COMMAND "500 Syntax error, command unrecognized\r\n"

#define SYNTAX_ERROR_ARGUMENTS "501 Syntax error in parameters or arguments\r\n"
#define COMMAND_NOT_IMPLEMENTED "502 Command not Implemented\r\n"
#define BAD_SEQUENCE "503 Bad sequence of commands\r\n"
#define PARAMETER_NOT_IMPLEMENTED "504 Command parameter not implemented\r\n"
#define USER_NOT_FOUND "550 Requested Action Not Taken\r\n" //mail box not found , no access ??
#define USER_NOTLOCAL "551 User Not Local;Please try forward Path\r\n"
#define TRANSACTION_FAILED "554 Transaction failed\r\n"



enum SMTP_STATE{ INIT,HELO,MAIL, RCPT, DATA, RSET,QUIT,NOOP,VRFY,EXPN,NONE} ;

int connfdlist[NUM_CONNECTIONS]; // Keeps the list of all connection file descriptors
int num_Connections; // num of connections at any time
int listenfd ;
bool verbose;
vector<string> Mailboxes; // List of Usernames allowed for the current host
vector<string> Receiver_List[NUM_CONNECTIONS];
char Sender[NUM_CONNECTIONS][MAX_USER_HOST_LENGTH];
char Sender_Host[NUM_CONNECTIONS][MAX_USER_HOST_LENGTH];
int num_Users;
char Username[NUM_CONNECTIONS][MAX_USER_HOST_LENGTH];
int Username_Length[NUM_CONNECTIONS]; //Could have made it local variable to thread / service 
char Host_Name[NUM_CONNECTIONS][MAX_USER_HOST_LENGTH]; // Could have made it local variale to thread / service
int Host_Name_Length[NUM_CONNECTIONS]; // Could have made it local variable to thread / service
SMTP_STATE PREV_MODE[NUM_CONNECTIONS]; // captures Previous of Each Connection
string Directory_Path; // Path where mailboxes are kept
bool DATA_STARTED[NUM_CONNECTIONS];
char* Directory_name;
//vector<SMTP_STATE * > PREV_ALLOWED_STATES;

/*
   Below service Reads the files present in the folder
*/


int getdir (string dir, vector<string> &files)
{
    DIR *dp;
    struct dirent *dirp;
    string username;
    num_Users=0;
    if((dp  = opendir(dir.c_str())) == NULL) {
        cout << "Error(" << errno << ") opening " << dir << endl;
        return errno;
    }

    while ((dirp = readdir(dp)) != NULL) {
        username=string(dirp->d_name);
        files.push_back(username);
        num_Users=num_Users+1;
    }
    closedir(dp);
    return 0;
}

/*
Below Service checks if the Username is present in the 
list of files(mailboxes) present in the current server
*/

bool valid_username(char U_name[],int id){

  for(int i=0;i<num_Users;i++){

    if(Mailboxes[i].compare(string(U_name)+string(".mbox"))==0){
      return true;
    }
    else{
      //Comparison Failed
    }
  }


    return false;
}

int Reset_Buffers(int id){

    memset(Sender[id],0,sizeof(Sender[id]));
    memset(Username[id],0,sizeof(Username[id]));
    memset(Host_Name[id],0,sizeof(Host_Name[id]));
    Receiver_List[id].clear();
    Username_Length[id]=0;
    Host_Name_Length[id]=0;
    PREV_MODE[id]=INIT;
}

int write_to_connfd(int id,const char * Input,int Length){
  int x;
  if(verbose){
    printf("[%d] S:%s\n",id,Input);
    }

  x=write(connfdlist[id], Input, Length);

  return x;

}

/*
Extracts Username and Host from the Input String,

Wrong input cases still need to handled.

*/
int Extract_User_Host(char  Input[],int Length,int id){
    
    int i;
    bool UserStarts;
    bool UserExtracted;
    bool HostExtracted;
    bool HostStarts;
    bool Extraction_Ends;

    UserStarts=false;
    UserExtracted=false;
    HostExtracted=false;
    HostStarts=false;
    Extraction_Ends=false;
    i=0;


    while((Input[i]!='\n'||Input[i]!='\r') ){



      if(UserStarts==true){

        Username[id][Username_Length[id]]=Input[i];
        Username_Length[id]++;
      }

      if(HostStarts==true){
        Host_Name[id][Host_Name_Length[id]]=Input[i];
        Host_Name_Length[id]++;
      }

      if(Input[i]=='>'){

        if(HostStarts==true){
          HostExtracted=true;
        }

        UserStarts=false;
        HostStarts=false;
        Host_Name[id][Host_Name_Length[id]-1]=0;


        if(UserExtracted==false || HostExtracted==false
                  ||strlen(Host_Name[id])==0||strlen(Username[id])==0){
          // end of the mail id before even username of host started : Invalid
          return -1;

        }


        break;
      }

      if(Input[i]=='@'){
        if(UserStarts==true){
            UserExtracted=true;
          }
        UserStarts=false;
        HostStarts=true;
        Username[id][Username_Length[id]-1]=0;
        Host_Name_Length[id]=0;
      }

      if(Input[i]=='<'){
        UserStarts=true;
        Username_Length[id]=0;
      }


      i++;

    }

    return 0;
}

int FindEmptySlot(int* List,int Length){

  int i;

  for(i=0;i<100;i++){
    if(List[i]==-1){
      return i;
    }
  }

  return Length;
}
void c_string_to_upper(char * Input,int len){

  for(int i=0;i<len;i++){
    Input[i]=toupper(Input[i]);
  }
}

void Signal_Handler(int signint_sig){

  for(int i=0;i<100;i++){
    if(connfdlist[i]!=-1){
      if(write_to_connfd(i, EXIT_MESSAGE, strlen(EXIT_MESSAGE))!=-1){
        close(connfdlist[i]);
      }
    }
  }
  close(listenfd);
  exit(1);

}


int Process_Mail(char *Input,int Length,int id){

  int connfd=connfdlist[id];
  
  //"Recieved %s\n",Input
  if (strncasecmp(Input,"FROM:",5)==0){
    int r_code;
    memset(Username[id],0,sizeof(Username[id]));
    memset(Host_Name[id],0,sizeof(Host_Name[id]));
    memset(Sender[id],0,sizeof(Sender[id]));
    memset(Sender_Host[id],0,sizeof(Sender_Host[id]));
    Username_Length[id]=0;
    Host_Name_Length[id]=0;
    r_code=Extract_User_Host(Input,strlen(Input),id);
    if(r_code==-1){

       write_to_connfd(id, SYNTAX_ERROR_ARGUMENTS, strlen(SYNTAX_ERROR_ARGUMENTS));
       return -1;

    }

    strcpy(Sender[id],Username[id]);
    strcpy(Sender_Host[id],Host_Name[id]);

    //Username
    //Host_Name
  return 0;
  }
  else{
   
    write_to_connfd(id, SYNTAX_ERROR_ARGUMENTS, strlen(SYNTAX_ERROR_ARGUMENTS));
    return -1;

  }
  return 0;
}



int Process_RCPT(char *Input,int Length,int id){


  int connfd;
  connfd=connfdlist[id];


  if(strncasecmp(Input,"TO:",3)==0){
    int r_code;
    r_code=-1;
    memset(Username[id],0,sizeof(Username[id]));
    memset(Host_Name[id],0,sizeof(Host_Name[id]));
    Username_Length[id]=0;
    Host_Name_Length[id]=0;
    r_code=Extract_User_Host(Input,strlen(Input),id);



    if(r_code!=-1){

    if(strcmp(Host_Name[id],"localhost")==0){

    if(valid_username(Username[id],id)){
      //"Username verified\n"
      Receiver_List[id].push_back(string(Username[id]));
      return 0;
    }
    else{
      //Invalid Username 
      write_to_connfd(id, USER_NOT_FOUND, strlen(USER_NOT_FOUND));
      return -1;

    }

  }
  else{
    //Invalid Host
     write_to_connfd(id, USER_NOTLOCAL, strlen(USER_NOTLOCAL));
    
    return  -1;
  }
  }
  else{
    write_to_connfd(id, SYNTAX_ERROR_ARGUMENTS, strlen(SYNTAX_ERROR_ARGUMENTS));
    return -1;
  }
}
else{
  write_to_connfd(id, SYNTAX_ERROR_ARGUMENTS, strlen(SYNTAX_ERROR_ARGUMENTS));
  return -1;
}
  return 0;
}
int Process_DATA(char *Input,int Length,int id){

    time_t rawtime;
    struct tm * timeinfo;
    char buffer [80];

    time (&rawtime);
    timeinfo = localtime (&rawtime);

    // Mon Aug 22 23:00:00 2016)

    strftime (buffer,80,"%c",timeinfo);
    //puts (buffer);

  if(strcmp(Input,".")==0){
    PREV_MODE[id]=INIT;
    Reset_Buffers(id);
    write_to_connfd(id, COMMAND_OKAY, strlen(COMMAND_OKAY));

    //ResetBuffers??
    return 0;
  }

  if(DATA_STARTED[id]==false){
    DATA_STARTED[id]=true;
    for(int i=0;i<Receiver_List[id].size();i++){
      ofstream file((Directory_Path+string("/")+Receiver_List[id][i]+string(".mbox")).c_str(),ios::out | ios::app);
      if(!(file <<"FROM <"<<Sender[id]<<"@"<<Sender_Host[id]<<">"<<buffer<<endl)){
        write_to_connfd(id, TRANSACTION_FAILED, strlen(TRANSACTION_FAILED));
        file.close();
        //return -1;
      }
    file.close();    
  }

  }

  for(int i=0;i<Receiver_List[id].size();i++){
    ofstream file((Directory_Path+string("/")+Receiver_List[id][i]+string(".mbox")).c_str(),ios::out | ios::app);
    if(!(file << Input<<"\r\n")){
      write_to_connfd(id, TRANSACTION_FAILED, strlen(TRANSACTION_FAILED));
      file.close();
      return -1;
    }
    file.close();    
  }


  return 0;
}
int Process_RSET(char *Input,int Length,int id){

  //Reset Buffers and Mode
  Reset_Buffers(id);

  return 0;
}
int Process_NOOP(char *Input,int Length){

  // Any Mode Change all ways Action okay ?


  return 0;
}

SMTP_STATE Calculate_State(char Input[]){



  if (strncasecmp(Input,"HELO",4)==0 || strncasecmp(Input,"EHLO",4)==0){
     return HELO;
    }

  else if (strncasecmp(Input,"QUIT",4)==0){
   return QUIT;
  }
  else if(strncasecmp(Input,"MAIL",4)==0){
    return MAIL;

  }
  else if(strncasecmp(Input,"RCPT",4)==0){
    return RCPT;

  }

  else if(strncasecmp(Input,"DATA",4)==0){
    return DATA;

  }
  else if(strncasecmp(Input,"RSET",4)==0){
    return RSET;
  }
  else if(strncasecmp(Input,"NOOP",4)==0){
    return NOOP;
  }
  else if(strncasecmp(Input,"VRFY",4)==0){
    return VRFY;
  }
  else if(strncasecmp(Input,"EXPN",4)==0){
    return EXPN;
  }
  else{
    return NONE;
  }
}



void *Client_Management(void * args){
  int connfdid = (int)args;
  int connfd;
  pthread_detach(pthread_self());
  connfd=connfdlist[connfdid];
  int ReturnCode;
  PREV_MODE[connfdid]=INIT;

  while(1)
    {

    size_t buf_idx = 0;
    char Input[BUFFLEN] = { 0 };
    SMTP_STATE STATE;
    bool error_sequence;
    STATE=INIT;
    
    error_sequence=false;
    ReturnCode=0;

    while ((buf_idx < BUFFLEN) && 1 == read(connfd, &Input[buf_idx], 1))
      {
        if ((buf_idx > 0) && ('\n' == Input[buf_idx]) && ('\r' == Input[buf_idx-1]))
          {
            break;
          }
        buf_idx++;
      }
    Input[buf_idx-1]=0;
    Input[buf_idx]=0;

    if(verbose){
      printf("[%d] C: %s\n",connfdid,Input);
      }
    //c_string_to_upper(Input,strlen(Input));

    if(PREV_MODE[connfdid]!=DATA){
    
    STATE=Calculate_State(Input);



    switch(STATE){

    case HELO:
    {
      ReturnCode=-1;
      if(PREV_MODE[connfdid]==INIT||PREV_MODE[connfdid]==HELO){
        if(strlen(Input+4)!=0){
 
          error_sequence=false;
          Reset_Buffers(connfdid);

          
          write_to_connfd(connfdid, "250 localhost\n", strlen("250 localhost\n"));
          PREV_MODE[connfdid]=STATE;
        }
        else{
          
          write_to_connfd(connfdid, SYNTAX_ERROR_ARGUMENTS, strlen(SYNTAX_ERROR_ARGUMENTS));
        }
      }
    else{
       error_sequence=true;
    }
      break;
    }
    case QUIT:
    {
      if(strlen(Input)==4){
 
        write_to_connfd(connfdid, CLOSE_CHANNEL, strlen(CLOSE_CHANNEL));
        close(connfd);
      // Number of Connections decreased by 1 and reclaim the slot in the array.
        connfdlist[connfdid]=-1;
        num_Connections=num_Connections-1;
        pthread_exit(0);
      }
      else{
        //Command is not Just QUIT
          write_to_connfd(connfdid, SYNTAX_ERROR_ARGUMENTS, strlen(SYNTAX_ERROR_ARGUMENTS));
          ReturnCode=-1;
      }
    }
    case MAIL:
    {
      if(PREV_MODE[connfdid]==RSET||PREV_MODE[connfdid]==MAIL||PREV_MODE[connfdid]==HELO||PREV_MODE[connfdid]==INIT)
      {

      ReturnCode=Process_Mail(Input+5,strlen(Input),connfdid);
    }
    else{
      error_sequence=true;
      //Error Sequence
    }
      break;

    }
    case RCPT:
    {
      if(PREV_MODE[connfdid]==RCPT||PREV_MODE[connfdid]==MAIL){
      ReturnCode=Process_RCPT(Input+5,strlen(Input),connfdid);
      error_sequence=false;
    }
    else{
      error_sequence=true;
      //Error Sequence
    }
      break;

    }
    case DATA:{

      if(PREV_MODE[connfdid]==RCPT){
        DATA_STARTED[connfdid]=false;

        write_to_connfd(connfdid, DATA_START_GIVING, strlen(DATA_START_GIVING));

      }
      else{
        error_sequence=true;
        //"Error Sequence
      }

      //ReturnCode=Process_DATA(Input+4,strlen(Input));
      break;

    }
    case RSET:
    {
      ReturnCode=Process_RSET(Input+5,strlen(Input),connfdid);

      break;
    }
    case NOOP:
    {
      //Do Nothing
      STATE=PREV_MODE[connfdid];
      ReturnCode=Process_NOOP(Input+5,strlen(Input));
      break;
    }
    case VRFY:
    case EXPN:
    {
      STATE=PREV_MODE[connfdid];
      ReturnCode=-1;
      write_to_connfd(connfdid, COMMAND_NOT_IMPLEMENTED, strlen(COMMAND_NOT_IMPLEMENTED));
    }
    default:
    //case NONE:
    {

      write_to_connfd(connfdid, UNKNOWN_MESSAGE, strlen(UNKNOWN_MESSAGE));
    }
 
    }
    if(!error_sequence){
      //"Not Error Sequence\n"
      if(ReturnCode!=-1){

        PREV_MODE[connfdid]=STATE;
        //write_to_connfd(connfdid, COMMAND_OKAY, strlen(COMMAND_OKAY));
        if(STATE!=DATA){

          write_to_connfd(connfdid, COMMAND_OKAY, strlen(COMMAND_OKAY));
        }
      }
    }
    else if(error_sequence){
      write_to_connfd(connfdid, BAD_SEQUENCE, strlen(BAD_SEQUENCE));
    }
  }
  else{
    ReturnCode=Process_DATA(Input,strlen(Input),connfdid);
  }
  }
}


int main(int argc, char *argv[])
{
    int listenfd = 0;
    
    char * num=NULL;
    
    pthread_t thd[100];
    struct sockaddr_in serv_addr; 

    char sendBuff[1025];
    int port_number;

    int Connection_Slot;

    char ch;

    Mailboxes=vector<string>();
    verbose=false;
    port_number=2500;


    while((ch=getopt(argc,argv,"p:av"))!=-1){
      switch(ch){
      case 'p':
      {
           num=optarg;
           break;
         }
      case 'v':
      {
           verbose=true;
           break;
         }
      case 'a':
      {
           fprintf(stderr, "FULL NAME: Methuku Karthik,Penn Login:kmeth\n");
           return 0;
         }
       }
    }
    if(num!=NULL){

      port_number=atoi(num);

    }

    
    
    Directory_name = argv[argc-1];
    Directory_Path=string(Directory_name);

    int error_read_dir;

    error_read_dir=getdir(Directory_Path,Mailboxes);
 
    for(int i =0;i<100;i++){

      connfdlist[i]=-1;

    }

    signal(SIGINT,Signal_Handler);

    num_Connections=0;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);

    memset(&serv_addr, '0', sizeof(serv_addr));
    memset(sendBuff, '0', sizeof(sendBuff)); 

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port_number); 

    bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)); 

    listen(listenfd, 100);

    while(true){

      time_t rawtime;
      struct tm * timeinfo;
      char buffer [80];

      time (&rawtime);
      timeinfo = localtime (&rawtime);

      // Mon Aug 22 23:00:00 2016)

      strftime (buffer,80,"%c",timeinfo);


      while(num_Connections<100){


        Connection_Slot=FindEmptySlot(connfdlist,num_Connections);

        connfdlist[Connection_Slot] = accept(listenfd, (struct sockaddr*)NULL, NULL);

        if(verbose){

          printf("S: New connection with id %d\n",num_Connections);

        }

        write_to_connfd(Connection_Slot, GREETING_MESSAGE, strlen(GREETING_MESSAGE));
        pthread_create(&thd[num_Connections],NULL,Client_Management,(void * )Connection_Slot);
        num_Connections++; 

      }
    }
    
    return 0;
}