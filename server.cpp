/*
** server.c -- a stream socket server demo
*/

#include <fstream>
#include <queue>
#include <vector>
#include <ifaddrs.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>      // std::pair, std::make_pair
#include <sys/utsname.h>
#include <thread>         // std::this_thread::sleep_for
#include <chrono> 
#include <time.h>       /* time */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <iostream>
#include <string>
#include <map>
#include <algorithm>    // std::max
//#include <atomic> 


#include <pthread.h>

#define LOGIN 1
#define UPDATELIST 2
#define UPDATEPORTINFO 3
#define SINGOUT 4
#define CLEANLIST 5
#define STARTCONNECT 6
#define STOPCONNECT 7
#define MODESWITCH 8
#define ACKDECLINE 9
#define HIGHMODE 1000
#define LOWMODE 500
#define MUTEMODE 200
#define AUTOMODE -1
#define NODEMAXBW 1000
#define S2CPORT "3590"
#define C2SPORT "3690"  // the port users will be connecting to

#define BACKLOG 10	 // how many pending connections queue will hold
#define MAXDATASIZE 1024
#define BUFSIZE 1024

using namespace std;

struct Message{
	int type;
	char UserName[20];//MessageP trade as owe IP address
	char IPAddress[20];
	int CommPort;
};

//username===> userIP
map<string, string> AllUserMap;
//userip===> [otheruserip, connectport]
map<string, map<string,int> > AllUserIPPortMap;
//username===> abailable Bandwidth
//ABAMap is how many Bindwidth this node left
map<string, int> ABAMap;
//username===> [username, roudebandwidth]
map<string, map<string,int> > RouteStateMap;
//RouteAUTOHLMap store weather this line/ route is AUTO?HIGH?LOW
//username===> [username, roudebandwidth]
map<string, map<string,int> > RouteAUTOHLMap;
//map for HU  <IP address, username>
map<string, string> IPUNmap;


//static volatile bool UPDateSig;

volatile bool UPDateSig;
string tempUserName, tempIPAddress;
string IP127="127.0.0.1";
static int Portindex=5000;

void *ListenMsg(void *arg);
void *ControlCenter(void *arg);

void sigchld_handler(int s){
	while(waitpid(-1, NULL, WNOHANG) > 0);
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa){
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

string getLocalIP(){
  char Lip[] = "192.0.0.1";
  struct ifaddrs * ifAddrStruct=NULL;
  void * tmpAddrPtr=NULL;

  getifaddrs(&ifAddrStruct);

  while (ifAddrStruct!=NULL) {
      if (ifAddrStruct->ifa_addr->sa_family==AF_INET) { // check it is IP4
          // is a valid IP4 Address
          tmpAddrPtr=&((struct sockaddr_in *)ifAddrStruct->ifa_addr)->sin_addr;
          char addressBuffer[INET_ADDRSTRLEN];
          inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
          //printf("%s IP Address %s/n", ifAddrStruct->ifa_name, addressBuffer); 
           
          if (strncmp(addressBuffer,Lip,3)==0){
            string returnIP=addressBuffer;
            return returnIP;
          }
      }
      ifAddrStruct=ifAddrStruct->ifa_next;
  }
}

int sendMsg(string IP, string PORT, Message tempMsg){
	int sockfd, numbytes;  
	char buf[MAXDATASIZE];
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(IP.c_str(), PORT.c_str(), &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and connect to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("client: socket");
			continue;
		}

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("client: connect");
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		return 2;
	}

	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
			s, sizeof s);
	//printf("client: connecting to %s\n", s);

	freeaddrinfo(servinfo); // all done with this structure

	if (send(sockfd, (char *) &tempMsg, sizeof(tempMsg), 0) == -1)
		perror("send");

	close(sockfd);
	//cout <<endl<<" sent"<<endl;
	return 0;
}

void showABAMapInfo(void){
	cout <<"################### ABA MAP ###################"<<endl;
  	for (auto it=ABAMap.begin(); it!=ABAMap.end(); ++it)
    	cout << "[" << it->first << "]: [" <<  it->second << "]"<<endl;
	cout <<"################### ABA MAP ###################"<<endl;
}

void ShowNameIPMapInfo(void){
	cout <<"=========== NAME IP MAP ============"<<endl;
  	for (auto it=AllUserMap.begin(); it!=AllUserMap.end(); ++it)
    	cout << "[" << it->first << "]: [" <<  it->second << "]"<<endl;
    cout <<"=========== NAME IP MAP =============="<<endl;
}


void ShowIPNAMEMapInfo(void){
	cout <<"@@@@@@@@@@@@@@@@@@ NAME IP MAP @@@@@@@@@@@@@@@@@@ "<<endl;
  	for (auto it=IPUNmap.begin(); it!=IPUNmap.end(); ++it)
    	cout << "[" << it->first << "]: [" <<  it->second << "]"<<endl;
    cout <<"@@@@@@@@@@@@@@@@@@ NAME IP MAP @@@@@@@@@@@@@@@@@@ "<<endl;
}


void ShowIPPORTMAPInfo(void){
	cout <<"=========== IP PORT MAP ============"<<endl;
  	for (auto it=AllUserIPPortMap.begin(); it!=AllUserIPPortMap.end(); ++it){
  		cout <<"[" << it->first << "]" <<endl;
  		for (auto itt=it->second.begin(); itt!=it->second.end(); ++itt)
	    	cout << "[" << itt->first << "]: [" <<  itt->second << "]"<<endl;
    }
    cout <<"=========== IP PORT MAP =============="<<endl;
}


void ShowRouteStateMapInfo(void){
	cout <<"***************** Route State Map *****************"<<endl;
  	for (auto it=RouteStateMap.begin(); it!=RouteStateMap.end(); ++it){
  		cout <<"[" << it->first << "]" <<endl;
  		for (auto itt=it->second.begin(); itt!=it->second.end(); ++itt)
	    	cout << "[" << itt->first << "]: [" <<  itt->second << "]"<<endl;
    }
	cout <<"***************** Route State Map *****************"<<endl;
}


void ShowRouteTypeMapInfo(void){
	cout <<"~~~~~~~~~~~~~~~~~~ Route Type Map ~~~~~~~~~~~~~~~~"<<endl;
  	for (auto it=RouteAUTOHLMap.begin(); it!=RouteAUTOHLMap.end(); ++it){
  		cout <<"[" << it->first << "]" <<endl;
  		for (auto itt=it->second.begin(); itt!=it->second.end(); ++itt)
	    	cout << "[" << itt->first << "]: [" <<  itt->second << "]"<<endl;
    }
	cout <<"~~~~~~~~~~~~~~~~~~ Route Type Map ~~~~~~~~~~~~~~~~"<<endl;
}



void ADDUser2MAP(string tempUserName, string tempIPAddress){
	IPUNmap[tempIPAddress]= tempUserName;
	AllUserMap[tempUserName] = tempIPAddress;
	ABAMap[tempUserName]=NODEMAXBW;

	for (auto itt = AllUserIPPortMap.begin(); itt != AllUserIPPortMap.end(); ++itt){
		itt->second[tempIPAddress] = Portindex;
		Portindex += 10;
	}


	for (auto itt = AllUserMap.begin(); itt != AllUserMap.end(); ++itt){
		if (tempIPAddress == itt->second){
			AllUserIPPortMap[tempIPAddress][itt->second] = -1;
		}
		else
			AllUserIPPortMap[tempIPAddress][itt->second] = AllUserIPPortMap[itt->second][tempIPAddress];
	}
}

void DELUser2MAP(string tempUserName){
	string tempIPAddress= AllUserMap[tempUserName];
	IPUNmap.erase(tempIPAddress);
	AllUserMap.erase(tempUserName);
	ABAMap.erase(tempUserName);
	AllUserIPPortMap.erase(tempIPAddress);

	for (auto itt = AllUserIPPortMap.begin(); itt != AllUserIPPortMap.end(); ++itt)
		itt->second.erase(tempIPAddress);

	RouteStateMap.erase(tempUserName);
	RouteAUTOHLMap.erase(tempUserName);
	//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@lots of work below, send the logoff neighbor to shut down
	// RouteStateMap.erase(tempUserName);
	// for (auto itt =RouteStateMap.begin();  itt != RouteStateMap.end(); ++itt)


	// RouteStateMap[tempUserName].erase(totalker);
	// if (RouteStateMap[initalker].size()==0)
	// 	RouteStateMap.erase(initalker);
	// RouteStateMap[totalker].erase(initalker);
	// if (RouteStateMap[totalker].size()==0)
	// 	RouteStateMap.erase(totalker);

	// RouteAUTOHLMap[initalker].erase(totalker);
	// if (RouteAUTOHLMap[initalker].size()==0)
	// 	RouteAUTOHLMap.erase(initalker);
	// RouteAUTOHLMap[totalker].erase(totalker);
	// if (RouteAUTOHLMap[totalker].size()==0)
	// 	RouteAUTOHLMap.erase(totalker);

	//more future operation
}


void OptimizeUser(string username, int desareBA){
	cout <<"OOOOOOOOptimization for user: "<< username<<endl;
	cout <<"desareBA: "<< desareBA<<endl;

	if (RouteStateMap.find(username)==RouteStateMap.end()){
		cout <<"No neighbor to optimize (no neighbor)"<<endl;
		return;
	}
	
	int NodeleftAB=ABAMap[username];
	cout <<"ABAMap[username]; " <<ABAMap[username] <<endl;
	cout <<"============================+++================="<<endl;
	ShowRouteStateMapInfo();
	ShowRouteTypeMapInfo();
	cout <<"============================---================"<<endl;

	for (auto it=RouteStateMap[username].begin(); it!=RouteStateMap[username].end(); ++it){
		//now [it->first]: username,, [it->second]: route between username and [it->first]
		//ABAMap[it->first] one user it->first's ABA
		if (RouteAUTOHLMap[username][it->first]!=1)// this path is not AUTO
			continue;

		int SelfLeftBA=ABAMap[username];
		int LineBA=it->second;
		int DestLeftBA=ABAMap[it->first];
		//int DestLineBA=it->second;

		//permit change of two users

		cout <<"ABAMap[username] "<<SelfLeftBA<<endl;
		cout <<"Line : "<< it->first<< "  LineBA "<<LineBA<<endl;
		cout <<"DestLeftBA : "<< DestLeftBA<<endl;


		if ( (LineBA<desareBA) && (LineBA+SelfLeftBA>=desareBA) && (LineBA+DestLeftBA>=desareBA) ){
			cout <<"can upgrade path ["<< username <<"] to ["<< it->first <<"]"<<endl;
			//first send msg tell two user to change
			Message tempmsg;
			tempmsg.type=MODESWITCH;
			//tempmsg.CommPort store the change to desareBA
			tempmsg.CommPort=desareBA;
			//tell the first user
			cout <<"111OOOOOOOOOOOOO to: "<< username <<endl;
			cout <<"link to : "<< it->first<<endl;
			strcpy(tempmsg.UserName,it->first.c_str());
			strcpy(tempmsg.IPAddress,AllUserMap[it->first].c_str());
			sendMsg(AllUserMap[username],S2CPORT,tempmsg);


			cout <<"222OOOOOOOOOOOOO to: "<< it->first <<endl;
			cout <<"link to : "<< username<<endl;
			strcpy(tempmsg.UserName,username.c_str());
			strcpy(tempmsg.IPAddress,AllUserMap[username].c_str());
			sendMsg(AllUserMap[it->first],S2CPORT,tempmsg);

			//second is that server change self RouteStateMap table
			ABAMap[username]=  SelfLeftBA+LineBA-desareBA;
			ABAMap[it->first]= DestLeftBA+LineBA-desareBA;
			RouteStateMap[username][it->first]=desareBA;
			RouteStateMap[it->first][username]=desareBA;
		}
	}
	
}



//downgrade, username is the user that need to be degrade, desareBA(space is the amount of space)
void DOWNOptimizeUser(string username, int desareSpacediff){

	int tempsaving(0);
	if (RouteStateMap.find(username)==RouteStateMap.end()){
		cout <<"No neighbor to optimize (no neighbor)"<<endl;
		return;
	}
	
	int NodeleftAB=ABAMap[username];
	cout <<"desareSpacediff"<< desareSpacediff << endl;
	cout <<"NodeleftAB  username:  "<< username << NodeleftAB<<endl;
	for (auto it=RouteStateMap[username].begin(); it!=RouteStateMap[username].end(); ++it){
		if (RouteAUTOHLMap[username][it->first]==1 && RouteStateMap[username][it->first] == HIGHMODE) {// this path can be downgrade
			//IP1==> username  , IP2==> it->first
			Message tempmsg;
			tempmsg.type=MODESWITCH;
			//tempmsg.CommPort destination mode/desire mode
			tempmsg.CommPort=LOWMODE;
			//tell the first user
			cout <<"#########send msg1#######"<<endl;
			cout <<"to: "<<username<<endl;
			cout <<"link to: " <<it->first <<endl;

			strcpy(tempmsg.UserName,username.c_str());
			strcpy(tempmsg.IPAddress,it->first.c_str());
			sendMsg(username,S2CPORT,tempmsg);

			//tell the second user

			cout <<"#########send msg2#######"<<endl;
			cout <<"to: "<<it->first<<endl;
			cout <<"link to: " <<username <<endl;
			strcpy(tempmsg.UserName,it->first.c_str());
			strcpy(tempmsg.IPAddress,username.c_str());
			sendMsg(it->first,S2CPORT,tempmsg);

			ABAMap[username]  += HIGHMODE-LOWMODE;
			ABAMap[it->first] += HIGHMODE-LOWMODE;
			RouteStateMap[username][it->first]=LOWMODE;
			RouteStateMap[it->first][username]=LOWMODE;

			tempsaving +=HIGHMODE-LOWMODE;
			if (tempsaving>= desareSpacediff)
				break;
			else
				continue;
		}
	}
	cout <<"finish downgrade for user: "<< username<<endl;
}


// NOWMin = ABAMap[userName]

int NOWMax(string userName){
	int templeft=0;
	if ( RouteAUTOHLMap.find(userName) != RouteAUTOHLMap.end() ) {
		for(auto it = RouteAUTOHLMap[userName].begin() ; it != RouteAUTOHLMap[userName].end(); it++){
			if(it->second==1){
				templeft += RouteStateMap[userName][it->first] - LOWMODE;
			}
		}
	}
	return templeft+ABAMap[userName];
}


void *ListenMsg(void *arg){
	//cout <<"ithread 1@@@@@@@@"<<endl;
	Message recvMsg;

	int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address information
	socklen_t sin_size;
	struct sigaction sa;
	int yes=1;
	char s[INET6_ADDRSTRLEN];
	int rv;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, C2SPORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		exit(1);
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
				sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}

		break;
	}

	if (p == NULL)  {
		fprintf(stderr, "server: failed to bind\n");
		exit(1);
	}

	freeaddrinfo(servinfo); // all done with this structure

	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}

	printf("server: waiting for connections...\n");

	while(1) {  // main accept() loop
		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
		if (new_fd == -1) {
			perror("accept");
			continue;
		}
		char * fromUser=(char *) inet_ntop(their_addr.ss_family,
			get_in_addr((struct sockaddr *)&their_addr),
			s, sizeof s);
		printf("server: got connection from %s\n", s);

		int numbytes; 
		char buf[MAXDATASIZE];


		if ((numbytes = recv(new_fd, buf, sizeof(recvMsg), 0)) == -1) {
			perror("recv");
			exit(1);
		}

		memcpy(& recvMsg,buf,numbytes);


		cout <<"MSG TYPE GIN :"<< recvMsg.type<<endl;
		if (recvMsg.type==LOGIN){
			cout <<"NEW USER LOGIN :"<<endl;

			tempUserName=(string)recvMsg.UserName;
			//tempIPAddress=(string)recvMsg.IPAddress;
			tempIPAddress=(string)fromUser;
			if (tempIPAddress == IP127){
				tempIPAddress.assign(getLocalIP());
				//cout <<"********"<<endl;
			}
			// cout << "recvMsg->type: "<< recvMsg.type <<endl;
			// cout << "recvMsg->value: "<< recvMsg.value <<endl;
			cout << "recvMsg->UserName: "<< tempUserName<<endl;
			cout << "recvMsg->IPAddress: "<< tempIPAddress<<endl;					
			ADDUser2MAP(tempUserName,tempIPAddress);
			UPDateSig=true; 
		}

		if (recvMsg.type==SINGOUT){
			cout <<"USER LOGOUT :"<<endl;
			tempUserName=(string)recvMsg.UserName;
			cout << "lOGOUT->UserName: "<< tempUserName<<endl;
			DELUser2MAP(tempUserName);
			UPDateSig=true; 
		}

		if (recvMsg.type==STARTCONNECT){
			cout <<"USER STARTCONNECT :"<<endl;
			string initalker;
			string totalker;
			initalker=(string)recvMsg.UserName;
			totalker=(string)recvMsg.IPAddress;
			int tempReqABA=recvMsg.CommPort;
			cout << initalker<< "===>" <<totalker<<endl;
			
			if (tempReqABA==AUTOMODE){ //auto mode need more action
				//first get initalker's ABA==>FromABA and totalker's ABA==>ToABA
				int FromABA=ABAMap[initalker];
				int ToABA=ABAMap[totalker];
				int tempReqABA;
				//debug
				cout << "NOWMax(totalker): "<<NOWMax(totalker)<<endl;
				cout << "NOWMax(initalker): "<<NOWMax(initalker)<<endl;

				if (NOWMax(totalker)< LOWMODE && NOWMax(initalker)< LOWMODE){
					cout <<"AUTO negotation is not allowed"<<endl;
					Message tempmsg;
					tempmsg.type=ACKDECLINE;
					string tempDetail="LOW NOT ENU";
					strcpy(tempmsg.UserName,tempDetail.c_str());
					sendMsg(AllUserMap[initalker],S2CPORT,tempmsg);
				}

				else{
					if (min(FromABA,ToABA)>=HIGHMODE){
						//cout <<"??????????case 1"<<endl;
						tempReqABA=HIGHMODE;
					}
					else if (min(FromABA,ToABA)>=LOWMODE){
						cout <<"??????????case 2"<<endl;
						tempReqABA=LOWMODE;
					}
					else if(NOWMax(totalker)>=LOWMODE && ABAMap[initalker]>=LOWMODE){
						cout <<"??????????case 3"<<endl;
						DOWNOptimizeUser(totalker,LOWMODE-ABAMap[totalker]);
						tempReqABA=LOWMODE;

					}
					else if(ABAMap[totalker]>=LOWMODE && NOWMax(initalker)>=LOWMODE){
						cout <<"??????????case 4"<<endl;
						DOWNOptimizeUser(initalker,LOWMODE-ABAMap[initalker]);
						tempReqABA=LOWMODE;

					}
					else if(NOWMax(totalker)>=LOWMODE && NOWMax(initalker)>=LOWMODE){
						cout <<"??????????case 5"<<endl;
						DOWNOptimizeUser(totalker,LOWMODE-ABAMap[initalker]);
						DOWNOptimizeUser(initalker,LOWMODE-ABAMap[totalker]);
						tempReqABA=LOWMODE;
					}

					//mark the RouteAUTOHLMap this path can be upgrade in the future
					RouteAUTOHLMap[initalker][totalker]=1;
					RouteAUTOHLMap[totalker][initalker]=1;

					ABAMap[initalker]-=tempReqABA;
					ABAMap[totalker] -=tempReqABA;
					RouteStateMap[initalker][totalker]=tempReqABA;
					RouteStateMap[totalker][initalker]=tempReqABA;


					Message tempmsg;
					tempmsg.type=STARTCONNECT;
					tempmsg.CommPort=tempReqABA;
					//tell the first user
					strcpy(tempmsg.UserName,totalker.c_str());
					strcpy(tempmsg.IPAddress,AllUserMap[totalker].c_str());
					sendMsg(AllUserMap[initalker],S2CPORT,tempmsg);

					strcpy(tempmsg.UserName,initalker.c_str());
					strcpy(tempmsg.IPAddress,AllUserMap[initalker].c_str());
					sendMsg(AllUserMap[totalker],S2CPORT,tempmsg);
				}
			}

			//either high or low mode
			else if(tempReqABA== HIGHMODE || tempReqABA== LOWMODE){
				// self bindwidth not required !!! this also can be done be them self, before sending this msg
				if (ABAMap[totalker]>=tempReqABA && ABAMap[initalker]>=tempReqABA ){	//permit tempReqABA> NOWMax
					//first mark this path can not auto Upgrade
					RouteAUTOHLMap[initalker][totalker]=-1;
					RouteAUTOHLMap[totalker][initalker]=-1;

					ABAMap[initalker]-=tempReqABA;
					ABAMap[totalker] -=tempReqABA;
					RouteStateMap[initalker][totalker]=tempReqABA;
					RouteStateMap[totalker][initalker]=tempReqABA;
					//server tell the user to build connection between two user
					Message tempmsg;
					tempmsg.type=STARTCONNECT;
					tempmsg.CommPort=tempReqABA;
					//tell the first user
					strcpy(tempmsg.UserName,totalker.c_str());
					strcpy(tempmsg.IPAddress,AllUserMap[totalker].c_str());
					sendMsg(AllUserMap[initalker],S2CPORT,tempmsg);

					strcpy(tempmsg.UserName,initalker.c_str());
					strcpy(tempmsg.IPAddress,AllUserMap[initalker].c_str());
					sendMsg(AllUserMap[totalker],S2CPORT,tempmsg);
				}

				else if (NOWMax(totalker)>=tempReqABA && ABAMap[initalker]>=tempReqABA){	//permit but need change as auto init
					DOWNOptimizeUser(totalker,tempReqABA-ABAMap[totalker]);
					Message tempmsg;
					tempmsg.type=STARTCONNECT;
					tempmsg.CommPort=tempReqABA;
					//tell the first user
					strcpy(tempmsg.UserName,totalker.c_str());
					strcpy(tempmsg.IPAddress,AllUserMap[totalker].c_str());
					sendMsg(AllUserMap[initalker],S2CPORT,tempmsg);

					strcpy(tempmsg.UserName,initalker.c_str());
					strcpy(tempmsg.IPAddress,AllUserMap[initalker].c_str());
					sendMsg(AllUserMap[totalker],S2CPORT,tempmsg);
				}

				else if (ABAMap[totalker]>=tempReqABA && NOWMax(initalker)>=tempReqABA){	//permit but need change as auto init
					DOWNOptimizeUser(initalker,tempReqABA-ABAMap[initalker]);
					Message tempmsg;
					tempmsg.type=STARTCONNECT;
					tempmsg.CommPort=tempReqABA;
					//tell the first user
					strcpy(tempmsg.UserName,totalker.c_str());
					strcpy(tempmsg.IPAddress,AllUserMap[totalker].c_str());
					sendMsg(AllUserMap[initalker],S2CPORT,tempmsg);

					strcpy(tempmsg.UserName,initalker.c_str());
					strcpy(tempmsg.IPAddress,AllUserMap[initalker].c_str());
					sendMsg(AllUserMap[totalker],S2CPORT,tempmsg);
				}

				else if (NOWMax(totalker)>=tempReqABA && NOWMax(initalker)>=tempReqABA){	//permit but need change as auto init
					DOWNOptimizeUser(initalker,tempReqABA-ABAMap[initalker]);
					DOWNOptimizeUser(totalker,tempReqABA-ABAMap[totalker]);
					Message tempmsg;
					tempmsg.type=STARTCONNECT;
					tempmsg.CommPort=tempReqABA;
					//tell the first user
					strcpy(tempmsg.UserName,totalker.c_str());
					strcpy(tempmsg.IPAddress,AllUserMap[totalker].c_str());
					sendMsg(AllUserMap[initalker],S2CPORT,tempmsg);

					strcpy(tempmsg.UserName,initalker.c_str());
					strcpy(tempmsg.IPAddress,AllUserMap[initalker].c_str());
					sendMsg(AllUserMap[totalker],S2CPORT,tempmsg);
				}

				else if(NOWMax(initalker)< tempReqABA && NOWMax(totalker) < tempReqABA){
					// self bindwidth not required !!! this also can be done be them self, before sending this msg
					if (NOWMax(initalker)< tempReqABA){

					}

					//the destination user's bindwidth is not allowed
					else if (NOWMax(totalker)< tempReqABA){//
						cout <<"the destination user's bindwidth is not allowed"<<endl;
						Message tempmsg;
						tempmsg.type=ACKDECLINE;
						string tempDetail="ABA DENEY";
						strcpy(tempmsg.UserName,tempDetail.c_str());
						sendMsg(AllUserMap[initalker],S2CPORT,tempmsg);
					}
				}
			}

			else if(tempReqABA== MUTEMODE){
				if (ABAMap[totalker]>=tempReqABA && ABAMap[initalker]>=tempReqABA ){	//permit tempReqABA> NOWMax
					//first mark this path can not auto Upgrade
					RouteAUTOHLMap[initalker][totalker]=-1;
					RouteAUTOHLMap[totalker][initalker]=-1;

					ABAMap[initalker]-=tempReqABA;
					ABAMap[totalker] -=tempReqABA;
					RouteStateMap[initalker][totalker]=tempReqABA;
					RouteStateMap[totalker][initalker]=tempReqABA;
					//server tell the user to build connection between two user
					Message tempmsg;
					tempmsg.type=STARTCONNECT;
					tempmsg.CommPort=tempReqABA;
					//tell the first user
					strcpy(tempmsg.UserName,totalker.c_str());
					strcpy(tempmsg.IPAddress,AllUserMap[totalker].c_str());
					sendMsg(AllUserMap[initalker],S2CPORT,tempmsg);

					strcpy(tempmsg.UserName,initalker.c_str());
					strcpy(tempmsg.IPAddress,AllUserMap[initalker].c_str());
					sendMsg(AllUserMap[totalker],S2CPORT,tempmsg);
				}
			}
		}

		if (recvMsg.type==STOPCONNECT){
			cout <<"USER DISCONNECT TO OTHER USER :"<<endl;
			string initalker;
			string totalker;
			initalker=(string)recvMsg.UserName;
			totalker=(string)recvMsg.IPAddress;
			cout <<"1 "<<initalker<<endl;
			cout <<"2 "<<totalker <<endl;
			//first update the ABAMap & RouteStateMap in server side
			int ABArest=RouteStateMap[initalker][totalker];
			ABAMap[initalker]+=ABArest;
			ABAMap[totalker]+=ABArest;
			RouteStateMap[initalker].erase(totalker);
			if (RouteStateMap[initalker].size()==0)
				RouteStateMap.erase(initalker);
			RouteStateMap[totalker].erase(initalker);
			if (RouteStateMap[totalker].size()==0)
				RouteStateMap.erase(totalker);

			RouteAUTOHLMap[initalker].erase(totalker);
			if (RouteAUTOHLMap[initalker].size()==0)
				RouteAUTOHLMap.erase(initalker);
			RouteAUTOHLMap[totalker].erase(initalker);
			if (RouteAUTOHLMap[totalker].size()==0)
				RouteAUTOHLMap.erase(totalker);

			OptimizeUser(initalker,HIGHMODE);
			OptimizeUser(totalker,HIGHMODE);

			Message tempmsg;
			tempmsg.type=STOPCONNECT;
			strcpy(tempmsg.UserName,totalker.c_str());
			strcpy(tempmsg.IPAddress,initalker.c_str());
			sendMsg(AllUserMap[totalker],S2CPORT,tempmsg);
		}


		//showABAMapInfo();
		//ShowNameIPMapInfo();
		//ShowIPPORTMAPInfo();
		ShowRouteStateMapInfo();
		ShowRouteTypeMapInfo();

		close(new_fd);  // parent doesn't need this
	}
}

void *ControlCenter(void *arg){
	//cout <<"ithread 2@@@@@@@@"<<endl;
	//UPDateSig=false;
	cout <<"UPDateSig"<<UPDateSig<<endl;
	while(1){
		//cout <<UPDateSig<<endl;
		if (UPDateSig){
			//cout <<"into here!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"<<endl;
			// show content:
			// cout << "!!send tempmsg.UserName"<<tempmsg.UserName<<endl;
			// cout << "!!send tempmsg.IPAddress"<<tempmsg.IPAddress<<endl;
			// cout <<"stayhere"<<endl;

			//first send update clean command
	    	for (auto it=AllUserMap.begin(); it!=AllUserMap.end(); ++it){
	    		Message tempmsg;
	    		tempmsg.type=CLEANLIST;
	    		sendMsg(it->second,S2CPORT,tempmsg);
	    	}

	    	for (auto it=AllUserMap.begin(); it!=AllUserMap.end(); ++it){
	    		for (auto itin=AllUserMap.begin(); itin!=AllUserMap.end(); ++itin){
					Message tempmsg;
					tempmsg.type=UPDATELIST;
					strcpy(tempmsg.UserName,itin->first.c_str());
					strcpy(tempmsg.IPAddress,itin->second.c_str());
					//tempmsg.CommPort=AllUserIPPortMap[it->second]
					sendMsg(it->second,S2CPORT,tempmsg);
	    		}
	    	}

			for (auto itt = AllUserIPPortMap.begin(); itt != AllUserIPPortMap.end(); ++itt){
				for (auto ittin = itt->second.begin(); ittin != itt->second.end(); ++ittin){
					Message tempmsgP;
					tempmsgP.type=UPDATEPORTINFO;
					strcpy(tempmsgP.UserName,IPUNmap[ittin->first].c_str());
					strcpy(tempmsgP.IPAddress,ittin->first.c_str());//destination IP
					tempmsgP.CommPort=ittin->second;
					sendMsg(itt->first,S2CPORT,tempmsgP);
				}
			}

			/*//
		  	for (auto it=AllUserMap.begin(); it!=AllUserMap.end(); ++it){
		    	//std::cout << it->first << " => " << it->second << '\n';
		    	Message tempmsg;
		    	tempmsg.type=UPDATELIST;
		    	strcpy(tempmsg.UserName,it->first.c_str());
		    	strcpy(tempmsg.IPAddress,it->second.c_str());
		    	//tempmsg.CommPort=AllUserIPPortMap[it->second]
		    	sendMsg(it->second,S2CPORT,tempmsg);

				for (auto itt = AllUserIPPortMap[it->second].begin(); itt != AllUserIPPortMap[it->second].end(); ++itt){
					Message tempmsgP;
					tempmsgP.type=UPDATEPORTINFO;
					strcpy(tempmsgP.UserName,it->second.c_str());
					strcpy(tempmsgP.IPAddress,itt->first.c_str());//destination IP
					tempmsgP.CommPort=itt->second;
					sendMsg(it->second,S2CPORT,tempmsgP);
				}
		    }
		    //*/
		    
			//cout <<"over"<<endl;
		    UPDateSig=false;
		}
	}
}

int main(void)
{
	pthread_t t1,t2;
	pthread_create(&t1,NULL,ListenMsg,NULL);
	pthread_create(&t2,NULL,ControlCenter,NULL);

	pthread_join(t1,NULL);
	pthread_join(t2,NULL);
}

