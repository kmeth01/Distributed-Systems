#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fstream>
#include <sys/types.h>
#include <sys/wait.h>
#include <vector>
#include <string>
#include <algorithm>
#include <sstream>
#include <iostream>
#include <stdlib.h>
#include <cstring>
#include <limits.h>


using namespace std;

#define NUM_CHILDS 30
void make_pipe_pair(int * pair1, int * pair2);
void close_pair(const int rfd, const int wfd);
bool Acc_All_NotInf(vector<int >Array,int length);
int Return_Min_Index(vector<long long > Array,int length);
int Merge_Arrays(std::vector<std::vector<long long> > Array_of_Arrays,std::vector<long long> & Merged_Output);


vector<string> Array;
std::vector<long long> Input_To_Child;
std::vector<std::vector<long long> > Data_From_Children;

int main(){

	int p_to_c[NUM_CHILDS][2];
	int c_to_p[NUM_CHILDS][2];
	pid_t pid[NUM_CHILDS];
    string lineT;
	int i,j;
	int Total_length;
	Total_length=0;
	pid_t Parent_PID;
	Parent_PID=getpid();
	int child_id;
	child_id=NUM_CHILDS;




    for( j=0;j<NUM_CHILDS;j++){
    	make_pipe_pair(p_to_c[j],c_to_p[j]);
    }

    for(j=0;j<NUM_CHILDS;j++){

        if(Parent_PID==getpid()){
           	pid[j]=fork();
           }

    	if(pid[j]==-1){
    		perror("error calling fork()");
    		return EXIT_FAILURE;
    	}
    	else if(pid[j]==0){
    		child_id=j;
    		break;
    	}
    	else{
    		if(Parent_PID==getpid()){
    			 //printf("I AM PARENT,%d\n",getpid());
    		}

    	}
    }

    /*
    Below Steps 
    close the read end of parent to child in parent and write end of the child to parent in parent
               cause parent writes from parent to child channel no use for read
               cause parent reads from child to parent channel no use for write
    close the "read=0" end of child to parent in child and "write=1" end of parent to child in children
               cause child uses only read parent to child of pipe
               cause child uses only write end of child to parent pipe
    */
    if(Parent_PID==getpid()){
      ifstream input("numbersmany13.txt");
      if(input.is_open()){
        while(getline(input,lineT)){
          Array.push_back(lineT);
      //printf("%d\n", lineT.size());
        }
      input.close();
      }
        //printf("I AM PARENT \n");
        for(int i=0;i<NUM_CHILDS;i++){
            close_pair(p_to_c[i][0],c_to_p[i][1]);
            std::vector<long long> Input;
            Data_From_Children.push_back(Input);
        }
    }
    else{
    	//printf("I AM CHILD %d\n",child_id);
    	close_pair(c_to_p[child_id][0],p_to_c[child_id][1]);
    	for(int p=0;p<NUM_CHILDS;p++){
    		if(p!=child_id){
    			close_pair(c_to_p[p][0],p_to_c[p][0]);
    			close_pair(c_to_p[p][1],p_to_c[p][1]);
    		}
    	}
    }

    /*Sending Data to Children*/

    if(Parent_PID==getpid()){
    	bool writing_data=true;
        string str;
        int lk;
        lk=0;
        FILE *stream;
    	while(writing_data==true){
    	for(int i=0;i<NUM_CHILDS;i++){
			stream=fdopen(p_to_c[i][1],"w");
			lk=0;
			for(int k=0;k<Array.size();k++){
				lk++;
				//fprintf(stream, Array[k].c_str()+"/n");
				if((k%NUM_CHILDS)==i){
				fprintf(stream, "%s\n", Array[k].c_str());//Array[k].c_str()
			}
			}
			fclose(stream);
				
		}
		//cout<<"Finished Writing Data to Children" <<lk<<endl;
		writing_data=false;

		for(int i=0;i<NUM_CHILDS;i++){
            close(p_to_c[i][1]);
            //cout<<"parent closing the write end of parent to child"<<endl;
        }

	}

		//writing_data=false;	
    }/*Recieving Data from Parent*/
	else{
		bool read_data=true;
		char line[50];
		ssize_t bytes_read;
		FILE * stream;
		stream = fdopen(p_to_c[child_id][0],"r");
		while(read_data){
            if (fgets(line,50,stream)==NULL){
				read_data=false;
				//cout<<"Parent closed the Pipe"<<endl;

				}
			else{
				//cout<<bytes_read<<endl;
				//str=line;
				//cout<<atof(line)<<"child Recieving"<<child_id<<endl;
            	Input_To_Child.push_back(atof(line));
		}
		

		}
		//cout<<Input_To_Child.size()<<"FGETS GOT THIS MUCH"<<endl;

	}

    /*Sorting Data in child*/
    if(child_id<NUM_CHILDS){
    	//cout<<"Sorting Data"<<endl;
    	sort(Input_To_Child.begin(),Input_To_Child.end());
    }

    /*Children sending Data to Parent*/
    if(child_id<NUM_CHILDS){
    	bool writing_data=true;
    	int index;
    	FILE *stream;
    	string str;
    	//cout<<"child writing"<<child_id<<endl;
		while(writing_data){
            stream=fdopen(c_to_p[child_id][1],"w");
			for(i=0;i<Input_To_Child.size();i++){
        ostringstream ss;
				ss<<Input_To_Child[i];
				//fprintf(stream, Array[k].c_str()+"/n");
				fprintf(stream, "%s\n", (ss.str()).c_str());
			}
			fclose(stream);
			writing_data=false;
			//cout<<"Child closed the pipe"<<":"<<Input_To_Child.size()<<child_id<<endl;
		}
		close(c_to_p[child_id][1]);

    }else{ /*Recieving Data in Parent*/
    	bool Reading_Data;
		//vector<long long> From_Child;
		ssize_t bytes_read;
		for(int childno=0;childno<NUM_CHILDS;childno++){
			bool read_data=true;
			char line[50];
			ssize_t bytes_read;
			FILE * stream;
			stream = fdopen(c_to_p[childno][0],"r");
			while(read_data){
            	if (fgets(line,50,stream)==NULL){
					read_data=false;
					//cout<<"child closed the Pipe"<<endl;

				}
			else{
				//cout<<bytes_read<<endl;
				//str=line;
				//cout<<atof(line)<<"child Recieving"<<child_id<<endl;
            	Data_From_Children[childno].push_back(atof(line));
				}

			}
		}
		for (int idex=0;idex<Data_From_Children.size();idex++){
			//cout<<idex<<endl;
			for(int ch=0;ch<Data_From_Children[idex].size();ch++){
				//cout<<Data_From_Children[idex][ch]<<endl;
			}
		}

		for(int childno=0;childno<NUM_CHILDS;childno++){
			close(p_to_c[childno][0]);
		}
	}


	//cout<<Array.size()<<endl;

    /*Merging Data in Parent*/
    if(Parent_PID==getpid()){
    vector<long long> Mergerd_Output(Array.size());
    int number=Merge_Arrays(Data_From_Children,Mergerd_Output);

    for(int index=0;index<Mergerd_Output.size();index++){
    	if(Mergerd_Output[index]!=0){
    	cout<<Mergerd_Output[index]<<endl;
        }
    }
    }
}


void make_pipe_pair(int * pair1, int * pair2)
{
    if ( pipe(pair1) == -1 || pipe(pair2) == -1 ) {
        perror("couldn't create pipe");
        exit(EXIT_FAILURE);
    }
}

void close_pair(const int rfd, const int wfd)
{
    if ( close(rfd) == -1 || close(wfd) == -1 ) {
        perror("couldn't close file");
        exit(EXIT_FAILURE);
    }
}

int Merge_Arrays(std::vector<std::vector<long long> > Array_of_Arrays,std::vector<long long> & Merged_Output){
	/*
      Take element from each array , append it to vector.
	*/
      vector <long long> Round(Array_of_Arrays.size());
      std::vector<int> Accountant;
      int i;
      int j;
      int index;
      int nOfarrays=Array_of_Arrays.size();
      j=0;
      for(i=0;i<Array_of_Arrays.size();i++){
      	Accountant.push_back(0);
      }
      while(Acc_All_NotInf(Accountant,nOfarrays)){
      	for(i=0;i<nOfarrays;i++){
      	    if(Accountant[i]<Array_of_Arrays[i].size()){
      	    Round[i]=Array_of_Arrays[i][Accountant[i]];
      	    }
      	else{
      		Round[i]=LLONG_MAX;
      	}

        }
        index=Return_Min_Index(Round,nOfarrays);
      	Merged_Output[j]=Array_of_Arrays[index][Accountant[index]];
      	j=j+1;
      	if(Accountant[index]+1<Array_of_Arrays[index].size()){
      		Accountant[index]=Accountant[index]+1;
      	}
      	else{
      		Accountant[index]=INT_MAX;
            
      	}

      }
      return j;
}

bool Acc_All_NotInf(vector<int > Array,int length){
      int i;
      for(i=0;i<length;i++){
      	if(Array[i]!=INT_MAX){
      		return true;
      	}
      }
      return false;
}

int Return_Min_Index(vector<long long> Array,int length){
	int i;
	int index;
	index=0;
	long long smallest;
	smallest=Array[0];//Let Smallest be first element in Array
	for(i=1;i<length;i++){
		if(Array[i]<smallest){
			smallest=Array[i];
			index=i;
		}

	}
	return index;
}

