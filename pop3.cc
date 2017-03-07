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
#include <fstream>
#include <stdlib.h>
#include <sstream>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <string>
#include <vector>
#include <openssl/md5.h>


using namespace std;

#define BUFFLEN 1024
#define NUM_CONNECTIONS 100
#define QUIT_MESSAGE "+OK User Signing off\n"
#define UNKNOWN_MESSAGE "-ERR UNKNOWN_MESSAGE\n"
#define GREETING_MESSAGE "+OK  POP3 SERVER READY\n"
#define EXIT_MESSAGE "-ERR Server shutting down \n"
#define I_KNOW_YOU "+OK Username can be displayed, enter Password with syntax PASS <SP> cis505\n"
#define WHO_ARE_YOU "-ERR Unknown User\n"
#define SERVER_IN_BAD_MOOD "-ERR UnAuthorized Command in Mode\n"
#define SHOW_MAIL_BOX "+OK Mailbox ready\n"
#define GIVE_CORRECT_PASSWORD "-ERR Wrong Password\n"
#define GIVE_ME_YOUR_NAME "-ERR Enter Username before Password\n"
#define SYNTAX_ERROR "-ERR SYNTAX ERROR\n"
#define OUT_OF_BOUND "-ERR Argument out of bounds\n"
#define MESSAGE_ALREADY_DELETED "-ERR Message already Deleted\n"
#define I_DONT_UNDERSTAND "-ERR Command is not recognized\n"

int connfdlist[NUM_CONNECTIONS];
int num_Connections;
int listenfd ;
bool verbose;
bool user_name_given[NUM_CONNECTIONS];
int num_Users;
char USER_NAME[NUM_CONNECTIONS][64];
char* Directory_name;


vector<string> UID_MSG[NUM_CONNECTIONS];
vector<int> Mail_Size[NUM_CONNECTIONS];
vector<string> MAIL_IN_BOX[NUM_CONNECTIONS];
vector<bool> IS_MAIL_DEL[NUM_CONNECTIONS];
vector<string> Mailboxes; // List of Usernames allowed 
string Directory_Path; // Path where mailboxes are kept

enum POP3_COMMANDS{ USER,PASS,STAT,UIDL,RETR,DELE,QUIT,LIST,RSET,NOOP,NONE} ;

enum POP3_STATE {AUTH,TRANSAC,UPDATE,INIT};

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



bool valid_username(char U_name[]){

  for(int i=0;i<Mailboxes.size();i++){

    if(Mailboxes[i].compare(string(U_name)+string(".mbox"))==0){
      return true;
    }
    else{
      //Comparison Failed
    }
  }


    return false;
}

int write_to_connfd(int id,const char * Input,int Length){
  int x;
  if(verbose){
    printf("[%d] S:%s\n",id,Input);
    }

  x=write(connfdlist[id], Input, Length);

  return x;

}

void computeDigest(char *data, int dataLengthBytes, unsigned char digestBuffer[])
{
  /* The digest will be written to digestBuffer, which must be at least MD5_DIGEST_LENGTH bytes long */

  MD5_CTX c;
  MD5_Init(&c);
  MD5_Update(&c, data, dataLengthBytes);
  MD5_Final(digestBuffer, &c);
}



int GENERATE_MAIL_BOX_STATS(char FILENAME[],int id)
{
  //cout<<FILENAME<<endl;
  ifstream file(FILENAME);

  unsigned char digest[MD5_DIGEST_LENGTH];

  int prev_line;
  bool Start;

  ostringstream ss;
  ofstream temp;
  prev_line=0;
  Start=false;

  long int size;

  string line;
  string mail;

  while(getline(file,line)){

    if(strncmp(line.c_str(),"FROM <",6)==0){

      if(Start){
          memset(&digest, '0', sizeof(digest));
          computeDigest(mail.c_str(),mail.size(),digest);

          ostringstream uidss;
          for(unsigned short int i = 0; i < sizeof(digest); i++){
            uidss << std::hex << (int) digest[i] << std::dec;
        }
        Mail_Size[id].push_back(mail.size());
        UID_MSG[id].push_back(uidss.str());
        IS_MAIL_DEL[id].push_back(false);

        MAIL_IN_BOX[id].push_back(ss.str());
        uidss.clear();
          mail.clear();
          ss.str("");
          ss.clear();
      }
      
      ss<<line;
      mail = mail+line;
      Start=true;
    }
    else{
      if(Start){
        int temp;
      temp=line.size();
      if(temp>0){
        mail = mail+line;
      }
          ss<<line;
          }
        }
  }

  if(Start){
    memset(&digest, '0', sizeof(digest));
    computeDigest(mail.c_str(),mail.size(),digest);
    ostringstream uidss;
    for(unsigned short int i = 0; i < sizeof(digest); i++){
        uidss << std::hex << (int) digest[i] << std::dec;
      }
    Mail_Size[id].push_back(mail.size());
    UID_MSG[id].push_back(uidss.str());
    MAIL_IN_BOX[id].push_back(ss.str());
    IS_MAIL_DEL[id].push_back(false);
    uidss.clear();
    mail.clear();
    ss.str("");
    ss.clear();
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

void STAT_OUTPUT(int * count,int * FullSize,int id){

   int count_mails=0;
   int sizeofmails=0;

   for(int i=0;i<IS_MAIL_DEL[id].size();i++){
    if(IS_MAIL_DEL[id][i]==false){
      count_mails=count_mails+1;
      sizeofmails=Mail_Size[id][i]+sizeofmails;
    }
   }

   *count=count_mails;
   *FullSize=sizeofmails;

   //return count_mails;
}

POP3_COMMANDS Calculate_Command(char Input[]){



  if (strncasecmp(Input,"USER",4)==0){
     return USER;
    }
  else if(strncasecmp(Input,"PASS",4)==0){
    return PASS;
  }
  else if (strncasecmp(Input,"QUIT",4)==0){
   return QUIT;
  }
  else if(strncasecmp(Input,"STAT",4)==0){
    return STAT;

  }
  else if(strncasecmp(Input,"UIDL",4)==0){
    return UIDL;

  }

  else if(strncasecmp(Input,"RETR",4)==0){
    return RETR;

  }
  else if(strncasecmp(Input,"RSET",4)==0){
    return RSET;
  }
  else if(strncasecmp(Input,"NOOP",4)==0){
    return NOOP;
  }
  else if(strncasecmp(Input,"DELE",4)==0){
    return DELE;
  }
  else if(strncasecmp(Input,"LIST",4)==0){
    return LIST;
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
  POP3_STATE STATE;

  STATE=AUTH;
  user_name_given[connfdid]=false;
  

  while(1)
    {
        
        //printf("Give Input:\n");
        //scanf("%s",Input);
    size_t buf_idx = 0;
    char Input[BUFFLEN] = { 0 };

    POP3_COMMANDS COMMAND;
    COMMAND=NONE;

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

    
        //Input_Processing(buf,connfd); 
    
    COMMAND=Calculate_Command(Input);

    switch(COMMAND){

      case USER:{
        if(STATE==AUTH){
          

          if(valid_username(Input+5)){
            memset(USER_NAME[connfdid], 0, sizeof(USER_NAME));
            strcpy(USER_NAME[connfdid],Input+5);
            //cout<<USER_NAME[connfdid]<<endl;
            user_name_given[connfdid]=true;
            write_to_connfd(connfdid, I_KNOW_YOU, strlen(I_KNOW_YOU));
          }
          else{
            write_to_connfd(connfdid,WHO_ARE_YOU, strlen(WHO_ARE_YOU));
          }

        }
        else if(STATE==TRANSAC){

          write_to_connfd(connfdid,SERVER_IN_BAD_MOOD, strlen(SERVER_IN_BAD_MOOD));

        }
       break;
      } 
      case QUIT:{

          write_to_connfd(connfdid, QUIT_MESSAGE, strlen(QUIT_MESSAGE));
          // Delete all the messages to be deleted if mode is Transaction and change mode to AUTH
          // Reset all the buffers and vectors
          if(STATE=TRANSAC){
            user_name_given[connfdid]=false;
            STATE=AUTH;
            string filepath;
            stringstream name;
            name<<"quitting_pop_"<<connfdid;
            ofstream temp((name.str()).c_str());

            for(int i=0;i<IS_MAIL_DEL[connfdid].size();i++){
              if(IS_MAIL_DEL[connfdid][i]==false){
                temp<<MAIL_IN_BOX[connfdid][i];
                temp<<"\n"<<endl;
              }
            }
            temp.close();
            filepath=Directory_Path+string("/")+string(USER_NAME[connfdid])+string(".mbox");
            remove(filepath.c_str());
            rename((name.str()).c_str(),filepath.c_str());
            connfdlist[connfdid]=-1;
            MAIL_IN_BOX[connfdid].clear();
            IS_MAIL_DEL[connfdid].clear();
            UID_MSG[connfdid].clear();
            memset(USER_NAME[connfdid],0,sizeof(USER_NAME[connfdid]));
            Mail_Size[connfdid].clear();
          }
          close(connfd);
          pthread_exit(0);
        break;
      }
      case PASS:{

        if(STATE==AUTH){

          if(user_name_given[connfdid]){
            //cout<<"PASS"<<endl;

            if(strncmp(Input+5,"cis505",6)==0){

              //write +OK
              write_to_connfd(connfdid, SHOW_MAIL_BOX, strlen(SHOW_MAIL_BOX));

              string filepath=Directory_Path+string("/")+string(USER_NAME[connfdid])+string(".mbox");

              int error_code;

              error_code=GENERATE_MAIL_BOX_STATS(filepath.c_str(),connfdid);

              STATE=TRANSAC;


            }
            else{

              write_to_connfd(connfdid,GIVE_CORRECT_PASSWORD, strlen(GIVE_CORRECT_PASSWORD));

            }

          }
          else{
            write_to_connfd(connfdid, GIVE_ME_YOUR_NAME, strlen(GIVE_ME_YOUR_NAME));

          }

        }
        else if(STATE==TRANSAC){

          write_to_connfd(connfdid,SERVER_IN_BAD_MOOD, strlen(SERVER_IN_BAD_MOOD));

        }
        break;
      }
      case STAT:{
        if(STATE==AUTH){
          write_to_connfd(connfdid,SERVER_IN_BAD_MOOD, strlen(SERVER_IN_BAD_MOOD));

        }
        else if(STATE==TRANSAC){

          if(strncmp(Input,"STAT",4)==0){

            // +OK number of messages and size of the messages
            ostringstream outstring;
            int count;
            int size;

            STAT_OUTPUT(&count,&size,connfdid);

            outstring<<"+OK "<<count<<" "<<size<<endl;;

            write_to_connfd(connfdid, (outstring.str()).c_str(), strlen((outstring.str()).c_str()));

          }
          else{

             write_to_connfd(connfdid, SYNTAX_ERROR, strlen(SYNTAX_ERROR));
          }

        }
        break;
      }
      case UIDL:{
        if(STATE==AUTH){

          //-ERR Command Not valid in this mode
          write_to_connfd(connfdid,SERVER_IN_BAD_MOOD, strlen(SERVER_IN_BAD_MOOD));

        }
        else if(STATE==TRANSAC){

          if(strlen(Input)>4){

            int a=atoi(Input+5);
            int count;
            int size;
            STAT_OUTPUT(&count,&size,connfdid);
            if(a!=0){
              // Return UIDL Of a-1 index
              if(a<=IS_MAIL_DEL[connfdid].size()){
                // Return UIDL of a-1 index
                ostringstream outstring;                

                if(IS_MAIL_DEL[connfdid][a-1]==false){
                  outstring<<"+OK "<<a<<" "<<UID_MSG[connfdid][a-1]<<endl;
                  write_to_connfd(connfdid,(outstring.str()).c_str(), strlen((outstring.str()).c_str()));
                }
                else{
                  write_to_connfd(connfdid,MESSAGE_ALREADY_DELETED, strlen(MESSAGE_ALREADY_DELETED));
                }
              }
              else{
                //-ERR Message Not present.
                write_to_connfd(connfdid,OUT_OF_BOUND, strlen(OUT_OF_BOUND));
              }
            }
            else{
              // Syntax error , Argument is not a number
              write_to_connfd(connfdid, SYNTAX_ERROR, strlen(SYNTAX_ERROR));
            }
          }
          else if(strlen(Input)==4){

            //Printing all the UIDs

            for(int i=0;i<IS_MAIL_DEL[connfdid].size();i++){
              if(IS_MAIL_DEL[connfdid][i]==false){
                ostringstream outstring;
                outstring<<i<<" "<<UID_MSG[connfdid][i]<<endl;
                write_to_connfd(connfdid,(outstring.str()).c_str(), strlen((outstring.str()).c_str()));
              }
            }
          }

        }
        break;
      }
      case RETR:{
        if(STATE==AUTH){

          //-ERR Message Not allowed in this mode
          write_to_connfd(connfdid,SERVER_IN_BAD_MOOD, strlen(SERVER_IN_BAD_MOOD));

        }
        else if(STATE==TRANSAC){

          if(strlen(Input)>4){

            int a = atoi(Input+5);
            if(a!=0){
              // Return UIDL Of a-1 index
              if(a<=IS_MAIL_DEL[connfdid].size()){
                // Return UIDL of a-1 index

                

                if(IS_MAIL_DEL[connfdid][a-1]==false){
                  //outstring<<"+OK "<<" "<<UIDL_A<<endl;
                  //write(connfd,(outstring.str()).c_str(), strlen((outstring.str()).c_str()));
                 ofstream temp("temp");
                 temp<<MAIL_IN_BOX[connfdid][a-1];
                 //cout<<MAIL_IN_BOX[connfdid][a-1];
                 temp.close();
                 ifstream temp1("temp");
                 string line;
                 ostringstream outstring;
                 outstring<<"+OK ("<<Mail_Size[connfdid][a-1]<<" octets)"<<endl;
                 write_to_connfd(connfdid,(outstring.str()).c_str(), strlen((outstring.str()).c_str()));

                 while(getline(temp1,line)){

                  write_to_connfd(connfdid,line.c_str(), strlen(line.c_str()));
                  write_to_connfd(connfdid,"\r\n", strlen("\r\n"));
                  line.clear();
                  //cout<<"IN While"<<endl;

                 }
                 write_to_connfd(connfdid,".\r\n", strlen(".\r\n"));                 

                }
                else{
                  write_to_connfd(connfdid,MESSAGE_ALREADY_DELETED, strlen(MESSAGE_ALREADY_DELETED));
                }
              }
              else{
                //-ERR Message Not present.
                write_to_connfd(connfdid,OUT_OF_BOUND, strlen(OUT_OF_BOUND));
              }
            }
            else{
              // Syntax error , Argument is not a number
              write_to_connfd(connfdid, SYNTAX_ERROR, strlen(SYNTAX_ERROR));
            }

          }
          else{
            //-ERR Syntax Error
            write_to_connfd(connfdid, SYNTAX_ERROR, strlen(SYNTAX_ERROR));
          }

        }
        break;
      }
      case RSET:{
        if(STATE==AUTH){

          // -ERR command Not authorized in this mode
          write_to_connfd(connfdid,SERVER_IN_BAD_MOOD, strlen(SERVER_IN_BAD_MOOD));

        }
        else if(STATE==TRANSAC){

          if(strlen(Input)==4){

            // Remove all the elements marked as delete
            // Print Total number of messages
            for(int i=0;i<IS_MAIL_DEL[connfdid].size();i++){

              IS_MAIL_DEL[connfdid][i]=false;
            }
        int count;
        int size;
        STAT_OUTPUT(&count,&size,connfdid);
        ostringstream outstring;
        outstring<<"+OK "<<count<<" "<<size<<endl;
        write_to_connfd(connfdid,(outstring.str()).c_str(), strlen((outstring.str()).c_str()));

          }
          else{

            //-ERR Syntax Error

             write_to_connfd(connfdid, SYNTAX_ERROR, strlen(SYNTAX_ERROR));
          }

        }
        break;
      }
      case DELE:{
        if(STATE==AUTH){

          // -ERR command Not authorized in this mode
          write(connfd,SERVER_IN_BAD_MOOD, strlen(SERVER_IN_BAD_MOOD));

        }
        else if(STATE==TRANSAC){
          if(strlen(Input)>4){

            int a = atoi(Input+5);
            if(a!=0){
              // Return UIDL Of a-1 index
              if(a<=IS_MAIL_DEL[connfdid].size()){
                // Return UIDL of a-1 index
              

                if(IS_MAIL_DEL[connfdid][a-1]==false){
                  IS_MAIL_DEL[connfdid][a-1]=true;
                  ostringstream outstring;
                  outstring<<"+OK "<<"Message "<<a<<" deleted"<<endl;
                  write_to_connfd(connfdid,(outstring.str()).c_str(), strlen((outstring.str()).c_str()));
                }
                else{
                  write_to_connfd(connfdid,MESSAGE_ALREADY_DELETED, strlen(MESSAGE_ALREADY_DELETED));
                }
              }
              else{
                //-ERR Message Not present.
                write_to_connfd(connfdid,OUT_OF_BOUND, strlen(OUT_OF_BOUND));
              }
            }
            else{
              // Syntax error , Argument is not a number
              write_to_connfd(connfdid, SYNTAX_ERROR, strlen(SYNTAX_ERROR));
            }
          }
        }
        break;
      }
      case LIST:{
         if(STATE==AUTH){
          //ERROR
          write_to_connfd(connfdid,SERVER_IN_BAD_MOOD, strlen(SERVER_IN_BAD_MOOD));

        }
        else if(STATE==TRANSAC){

          if(strlen(Input)==4){
            int count;
            int size;
            STAT_OUTPUT(&count,&size,connfdid);
            ostringstream outstring;
            outstring<<"+OK "<<count<<" "<<size<<endl;
            write_to_connfd(connfdid,(outstring.str()).c_str(), strlen((outstring.str()).c_str()));
            for(int i=0;i<IS_MAIL_DEL[connfdid].size();i++){
              if(IS_MAIL_DEL[connfdid][i]==false){
                ostringstream outstr;
                outstr<<i+1<<" "<<Mail_Size[connfdid][i]<<endl;
                write_to_connfd(connfdid,(outstr.str()).c_str(), strlen((outstr.str()).c_str()));

              }
            }
          }
          else if(strlen(Input)>4){

            int a = atoi(Input+5);
            if(a!=0){
              // Return UIDL Of a-1 index
              if(a<=IS_MAIL_DEL[connfdid].size()){
                // Return UIDL of a-1 index
              

                if(IS_MAIL_DEL[connfdid][a-1]==false){
                  ostringstream outstring;
                  outstring<<"+OK "<<a<<" "<<Mail_Size[connfdid][a-1]<<endl;
                  write_to_connfd(connfdid,(outstring.str()).c_str(), strlen((outstring.str()).c_str()));
                }
                else{
                  write_to_connfd(connfdid,MESSAGE_ALREADY_DELETED, strlen(MESSAGE_ALREADY_DELETED));
                }
              }
              else{
                //-ERR Message Not present.
                write_to_connfd(connfdid,OUT_OF_BOUND, strlen(OUT_OF_BOUND));
              }
            }
            else{
              // Syntax error , Argument is not a number
              write_to_connfd(connfdid, SYNTAX_ERROR, strlen(SYNTAX_ERROR));
            }

          }
        }
        break;
      }
      case NOOP:{
        if(STATE==AUTH){

          //-ERR Command NOt authorized
          write_to_connfd(connfdid,SERVER_IN_BAD_MOOD, strlen(SERVER_IN_BAD_MOOD));

        }
        else if(STATE==TRANSAC){

          // +OK
          write_to_connfd(connfdid,"+OK\n", strlen("+OK\n"));

        }
        break;
      }
      case NONE:{
        write_to_connfd(connfdid,I_DONT_UNDERSTAND, strlen(I_DONT_UNDERSTAND));
        break;
      }
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

    char ch;
     verbose=false;
     port_number=11000;
      int Connection_Slot;

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
      while(num_Connections<100){


        Connection_Slot=FindEmptySlot(connfdlist,num_Connections);

        connfdlist[Connection_Slot] = accept(listenfd, (struct sockaddr*)NULL, NULL);

        if(verbose){

          printf("S: New connection with id %d\n",num_Connections);

        }

        write(connfdlist[Connection_Slot], GREETING_MESSAGE, strlen(GREETING_MESSAGE));
        pthread_create(&thd[num_Connections],NULL,Client_Management,(void * )Connection_Slot);
        num_Connections++; 

      }
    }

    printf("%d\n",num_Connections );

    
    return 0;
}