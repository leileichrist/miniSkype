#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <fstream>
#include <string.h>
#include <string>
#include <queue>
#include <vector>
#include <ifaddrs.h>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>      // std::pair, std::make_pair
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <thread>         // std::this_thread::sleep_for
#include <pthread.h>
#include <chrono> 
#include <time.h>       /* time */
#include <signal.h>
// #include <X11/Xlib.h>
#include <gst/video/videooverlay.h>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

#include <gtk/gtk.h>
#include <gst/gst.h>

#include <gdk/gdk.h>
#if defined (GDK_WINDOWING_X11)
#include <gdk/gdkx.h>
#elif defined (GDK_WINDOWING_WIN32)
#include <gdk/gdkwin32.h>
#elif defined (GDK_WINDOWING_QUARTZ)
#include <gdk/gdkquartz.h>
#endif

using namespace std;
using namespace boost::algorithm;

const char* const DEFAULT_PORT = "3490";
const char* const VIDEO_CAPS = "application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)JPEG";
const char* const AUDIO_CAPS = "application/x-rtp, media=(string)audio, clock-rate=(int)8000, encoding-name=(string)AMR, encoding-params=(string)1, octet-align=(string)1, payload=(int)96";
const string DEFAULT_IP = "localhost" ;

const int MAXIMUM_BW = 10000;
const int MAXBUFLEN = 100;
const int BACKLOG = 10 ; 
const int BUFSIZE = 1024 ;
const int MAXDATASIZE = 256 ;// max number of bytes we can get at once 
const int ACTIVE_MODE = 0;

const int RES_HIGH = 0;
const int RES_MEDIAN = 1;
const int RES_PASSIVE = 2;

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
#define C2SPORT "3690"  

//command list
//string fromIP
//Operation: 1) start connection 2) stop connection (3)Pause connection 3)Mode change
//Details: 1)ActiveHD 2)ActiveLOW 3) ActiveAUTO 4)Passave

/*struct representing a message*/
struct Message
{
  string fromIp;
  string content;
  Message(const string& ip, const string& str): fromIp(ip), content(str){}   
};
typedef struct _videoData {
  char * contact;
  string fromIp;
  GtkWidget * mode_but; 
}videoData;

typedef struct _RecvDataStream
{
  GstElement *clientPipeline;
  GstElement *audio_queue, *video_queue;
  GstElement *video_decoder, *audio_decoder;
  GstElement *video_sink, *audio_sink;
  GstElement *videoDepayloader, *audioDepayloader;
  GstElement *clientRTPBIN;
  GstElement *udpsrc_rtp0, *udpsrc_rtp1, *udpsrc_rtcp0, *udpsrc_rtcp1 ;
  GstElement *udpsink_rtcp0, *udpsink_rtcp1 ;
  GstElement *fakeaudiosink;

  videoData* vdata;


  GstCaps *videocaps; 
  GstCaps *audiocaps; 

  GMainLoop *main_loop;  /* GLib's Main Loop */

  gint mode;
  gboolean connected;
  gboolean mode_selected;
  string fromIp;
  string fromname;

}RecvDataStream ;

typedef struct _SentDataStream
{
  GstElement *audio_queue_1, *video_queue_1;
  GstElement *video_encoder, *audio_encoder;
  GstElement *videorate_controller, *audiorate_controller;
  GstElement *videoPayloader, *audioPayloader;
  GstElement *audio_queue_2, *video_queue_2;
  GstElement *video_scale;

  GstElement *serverRTPBIN;
  GstElement *udpsink_rtp0, *udpsink_rtp1, *udpsink_rtcp0, *udpsink_rtcp1 ;
  GstElement *udpsrc_rtcp0, *udpsrc_rtcp1 ;
  GstElement *fakeaudiosink;

  GstPad *tee_audio_pad, *tee_video_pad;

  GstCaps *video_caps;
  GstCaps *audio_caps;
  gint mode;
  gint tomode;
 
  gint res_mode;
  gint FPS;
  string toIp;
  string toname ;

  gboolean audio_removed;
  gboolean video_removed;

}SentDataStream;

typedef struct _SentData
{
  GstElement *serverPipeline;
  GstElement *camera_video_src;
  GstElement *camera_audio_src;
  GstElement *video_tee, *audio_tee;
  
  unordered_map<string, pair<string, int> >* partnersInfo;  /*< ip, <username, startingUdpPort> >*/
  unordered_map<string, RecvDataStream* > * idata;  /*< ip, recvDataStream* >*/
  unordered_map<string, SentDataStream* > * odata;  /*< ip, sentDataStream* >*/

  GMainLoop *main_loop;  /* GLib's Main Loop */

}SentData;

struct MessageS{
  int type;
  char UserName[20];
  char IPAddress[20];
  int CommPort;
};

typedef struct _GuiData {
  GtkTextBuffer *buffer;
  GtkEntry * user_name, *server_ip;
  GtkWidget *main_box, *cur_window;
  GtkWidget *menu;
  GtkWidget *chat_b, *quit_b;
  GtkWidget *table=NULL;
  GtkWidget * open_contact  ;
  bool chat_open= FALSE;
  GtkWidget * status_text;
  GtkWidget * chat_window;
  GtkWidget * hbox;

} GuiData;
GuiData * myGui;

SentData* dataSource ;
vector<pthread_t> waitingThreads;
pthread_mutex_t vLock;
pthread_mutex_t sendPipelineLock;
pthread_mutex_t databranchlock;
pthread_cond_t readyToDelete;

char* username;

map<string,string> AllUserMap;
map<string, int> UserIPPortMap;
string ServerIP;
string ServerPORT;
string LocalIP;
string SelfUserName;
char status_buf[100];
GtkTextBuffer * buffer;


/*function prototypes*/
void removeRecvPipeline(RecvDataStream* data);
static void rtpbin_pad_added_handler_send(GstElement *src, GstPad *new_pad, SentDataStream *data);
static void rtpbin_pad_added_handler_recv(GstElement *src, GstPad *new_pad, RecvDataStream *data);
void getSystemName(void);
void testPipelineSend(const string& ip, const int& port);
void testPipelineRecv(const string& ip, const int& port);
static GstPadProbeReturn remove_audio_cb (GstPad * tee_audio_pad, GstPadProbeInfo * info,  gpointer userData) ;
static GstPadProbeReturn remove_video_cb (GstPad * tee_video_pad, GstPadProbeInfo * info,  gpointer userData) ;
static GstPadProbeReturn change_mode_cb(GstPad * video_queue_pad, GstPadProbeInfo * info,  gpointer userdata) ;
static void realize_cb (GtkWidget *widget, RecvDataStream* data ) ;
void create_chat_window(RecvDataStream * data ) ;


/*gui functions*/
string getLocalIP();
void *ListenMsgClient(void *arg);
int sendMsg2Server(string IP, string PORT, MessageS sendMsg);
void ShowLocalMapInfo(void);
void ShowIPPortMapInfo(void);
void ShowDataSourcePartnersInfo(void);
void set_background( GtkWidget *window );
void create_menu(GtkWidget * * menu );
static gboolean find_chaters( GuiData *data, GdkEvent *event );
void join_cb(GtkButton *button,  GuiData * data);
void create_welcome( GuiData * data);
static void set_position(GtkMenu *menu, gint *x, gint *y, gboolean *push_in, gpointer user_data);
void insert_welcome(GtkWidget * layout);
void menuitem_response( gchar* buf);


/*Message: [ip] t1 t2 t3 will become a vector of string : {t1, t2, t3}*/
void parseMsg(Message* msg, vector<string>& tokens)
{
    split(tokens, msg->content, is_any_of(" ")); 
}

void sigchld_handler(int s)
{
  while(waitpid(-1, NULL, WNOHANG) > 0);
}

void *get_in_addr(struct sockaddr *sa)
{
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in*)sa)->sin_addr);
  }
  return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

static void* process_message(void* arg)
{
  return NULL;
}

int send_message(const char* toIp, const char* toPort , const char* buf)
{
  int sockfd, numbytes;  
  struct addrinfo hints, *servinfo, *p;
  int rv;
  char s[INET6_ADDRSTRLEN];
  char* msg=NULL;
  asprintf(&msg, "%s", buf);

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if ((rv = getaddrinfo(toIp, toPort, &hints, &servinfo)) != 0) {
    fprintf(stderr, "<send_message> getaddrinfo: %s\n", gai_strerror(rv));
    return 1;
  }
  // loop through all the results and connect to the first we can
  for(p = servinfo; p != NULL; p = p->ai_next) 
  {
    if ((sockfd = socket(p->ai_family, p->ai_socktype,p->ai_protocol)) == -1)
    {
      fprintf(stderr, "%s: cannot establish socket!", username );
      continue;
    }
    if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) 
    {
      close(sockfd);
      fprintf(stderr, "%s: cannot connect to socket", username);
      continue;
    }
    break;
  }
  if (p == NULL) {
    fprintf(stderr, "%s cannot connect to %s \n", username, toIp);
    return 2;
  }
  freeaddrinfo(servinfo); // all done with this structure
  
  if (send(sockfd, msg, MAXDATASIZE, 0) == -1)
        perror("send");
  
  close(sockfd);
  return 0;
}

static void* listeningForMsg(void* arg)
{
  int sockfd, new_fd, numbytes;  // listen on sock_fd, new connection on new_fd
  char buf[MAXDATASIZE];
  struct addrinfo hints, *servinfo, *p;
  struct sockaddr_storage their_addr; // connector's address information
  socklen_t sin_size;
  struct sigaction sa;
  int yes=1;
  char s[INET6_ADDRSTRLEN];
  int rv;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET; // set to AF_INET to force IPv4
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE; // use my IP

  if ((rv = getaddrinfo(NULL, DEFAULT_PORT, &hints, &servinfo)) != 0) {
    fprintf(stderr, "<listeningForMsg> getaddrinfo: %s\n", gai_strerror(rv));
    return NULL;
  }

  // loop through all the results and bind to the first we can
  for(p = servinfo; p != NULL; p = p->ai_next) 
  {
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
    return NULL;
  }

  freeaddrinfo(servinfo); // all done with this structure

  if (listen(sockfd, BACKLOG) == -1) {
    perror("listen");
    exit(1);
  }

  //do we have to reap all the dead processes here?
  sa.sa_handler = sigchld_handler; // reap all dead processes
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if (sigaction(SIGCHLD, &sa, NULL) == -1) {
    perror("sigaction");
    exit(1);
  }

  printf("%s: waiting for connections...\n", username);
  while(1) 
  { 
    // main accept() loop
    char* mesg_stored=NULL;
    sin_size = sizeof their_addr;
    new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
    if (new_fd == -1) 
    {
      perror("accept");
      continue;
    }

    char* fromUser = (char*)inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
    if ((numbytes = recv(new_fd, buf, MAXDATASIZE-1, 0)) == -1) 
    {
        perror("recv");
        exit(1);
    }
    string ip(fromUser);
    string str(buf);
    Message* message = new Message(ip, str);
    /*spawn a thread for processing this message*/
    pthread_t t;
    pthread_create(&t, NULL, process_message, (void*)(message) );
 
    //recycle this thread  
    pthread_mutex_lock(&vLock);
    waitingThreads.push_back(t);  
    pthread_mutex_unlock(&vLock);

    close(new_fd);  
  }
  return NULL;
}

gboolean buildRecvPipeline(RecvDataStream* data, const int startPort)
{
  /*Pads for requesting*/
  GstPadTemplate *recv_rtp_sink_temp, *recv_rtcp_sink_temp, *send_rtcp_src_temp;
  GstPad *recv_rtp_sink0, *recv_rtp_sink1;
  GstPad *recv_rtcp_sink0, *recv_rtcp_sink1; 
  GstPad *send_rtcp_src0, *send_rtcp_src1;

  gint port_recv_rtp_sink0 = startPort;
  gint port_recv_rtp_sink1 = startPort+1;
  gint port_recv_rtcp_sink0 = startPort+2;
  gint port_recv_rtcp_sink1 = startPort+3;
  gint port_send_rtcp_src0 = startPort+4;
  gint port_send_rtcp_src1 = startPort+5;

  /*static pads*/
  GstPad *udpsrc_rtp0_src, *udpsrc_rtp1_src, *udpsrc_rtcp0_src, *udpsrc_rtcp1_src;
  GstPad *udpsink_rtcp0_sink, *udpsink_rtcp1_sink ;
 
  GstBus *bus = gst_bus_new();
  GstMessage *msg;
  GstStateChangeReturn ret;

  /* Create pipeline */
  data->clientPipeline = gst_pipeline_new("client");
  
  /* Create elements */
  data->video_decoder = gst_element_factory_make("jpegdec", "video_decoder");
  data->audio_decoder = gst_element_factory_make("amrnbdec", "audio_decoder");    

  data->audio_queue = gst_element_factory_make("queue", "audio_queue");
  data->video_queue = gst_element_factory_make("queue", "video_queue");

  data->videoDepayloader = gst_element_factory_make("rtpjpegdepay","videoDepayloader");
  data->audioDepayloader = gst_element_factory_make("rtpamrdepay","audioDepayloader");

  data->video_sink = gst_element_factory_make("xvimagesink","video_sink");
  data->audio_sink = gst_element_factory_make("autoaudiosink","audio_sink");

  data->clientRTPBIN = gst_element_factory_make("rtpbin","clientRTPBIN");

  data->udpsrc_rtp0 = gst_element_factory_make("udpsrc", "udpsrc_rtp0");
  data->udpsrc_rtp1 = gst_element_factory_make("udpsrc", "udpsrc_rtp1");

  data->udpsrc_rtcp0 = gst_element_factory_make("udpsrc", "udpsrc_rtcp0");
  data->udpsrc_rtcp1 = gst_element_factory_make("udpsrc", "udpsrc_rtcp1");
  
  data->udpsink_rtcp0 = gst_element_factory_make("udpsink", "udpsink_rtcp0");
  data->udpsink_rtcp1 = gst_element_factory_make("udpsink", "udpsink_rtcp1");

  data->fakeaudiosink = gst_element_factory_make("fakesink", "fakeaudiosink");

  /* Check that elements are correctly initialized */
  if(!(data->clientPipeline && data->audio_decoder && data->audio_queue && data->video_decoder && data->video_queue && 
       data->videoDepayloader && data->audioDepayloader && data->clientRTPBIN && data->udpsrc_rtp0 && data->udpsrc_rtp1 &&
       data->udpsink_rtcp0 && data->udpsink_rtcp1 && data->udpsrc_rtcp0 && data->udpsrc_rtcp1 && data->video_sink && data->audio_sink ))
  {
    g_critical("Couldn't create pipeline elements");
    return FALSE;
  }

  data->videocaps = gst_caps_from_string(VIDEO_CAPS);
  data->audiocaps = gst_caps_from_string(AUDIO_CAPS);

  printf("<receiver>: receiving rtp packets at ports %d and %d, and rtcp packets at ports %d and %d \n",
          port_recv_rtp_sink0, port_recv_rtp_sink1, port_recv_rtcp_sink0, port_recv_rtcp_sink1 );
  printf("<receiver>: sending rtcp packets to user %s 's ports %d and %d \n", 
          (gchar*)data->fromIp.c_str(), port_send_rtcp_src0, port_send_rtcp_src1 );

  g_object_set(data->udpsrc_rtp0, "caps", data->videocaps, "port", port_recv_rtp_sink0, NULL);
  g_object_set(data->udpsrc_rtp1,  "caps", data->audiocaps, "port", port_recv_rtp_sink1,  NULL);
  g_object_set(data->udpsrc_rtcp0, "port", port_recv_rtcp_sink0, NULL);
  g_object_set(data->udpsrc_rtcp1, "port", port_recv_rtcp_sink1, NULL);
  g_object_set(data->udpsink_rtcp0, "host", (gchar*)data->fromIp.c_str(), "port", port_send_rtcp_src0, "async", FALSE, "sync", FALSE, NULL);
  g_object_set(data->udpsink_rtcp1, "host", (gchar*)data->fromIp.c_str(), "port", port_send_rtcp_src1, "async", FALSE, "sync", FALSE,  NULL);

  if(data->mode != RES_PASSIVE)
    gst_bin_add_many(GST_BIN(data->clientPipeline), data->audio_decoder, data->audio_queue, data->video_decoder, data->video_queue,
         data->videoDepayloader, data->audioDepayloader, data->clientRTPBIN,
         data->udpsrc_rtp0, data->udpsrc_rtp1, data->udpsink_rtcp0, data->udpsink_rtcp1,
         data->udpsrc_rtcp0, data->udpsrc_rtcp1, data->video_sink, data->audio_sink, NULL);
  else
  {
    gst_bin_add_many(GST_BIN(data->clientPipeline), data->video_decoder, data->video_queue,
         data->videoDepayloader, data->clientRTPBIN, data->udpsrc_rtp0, data->udpsink_rtcp0, 
         data->udpsrc_rtcp0, data->video_sink, NULL);
  }

  if(!gst_element_link_many(data->video_queue, data->videoDepayloader, data->video_decoder, data->video_sink, NULL))
  {
    g_printerr("videoqueue, depayloader, decoder, and sink cannot be linked!\n");
    gst_object_unref (data->clientPipeline);
    return FALSE;
  }
  if(data->mode !=RES_PASSIVE && !gst_element_link_many(data->audio_queue, data->audioDepayloader, data->audio_decoder, data->audio_sink, NULL))
  {
    g_printerr("audio queue and audio decoder cannot be linked!.\n");
    gst_object_unref (data->clientPipeline);
    return FALSE;
  }
  
  recv_rtp_sink_temp = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS(data->clientRTPBIN), "recv_rtp_sink_%u");
  recv_rtp_sink0 = gst_element_request_pad (data->clientRTPBIN, recv_rtp_sink_temp, NULL, NULL);
  g_print ("<receiver>: Obtained request pad %s for recv_rtp_sink !.\n", gst_pad_get_name (recv_rtp_sink0));
  udpsrc_rtp0_src = gst_element_get_static_pad(data->udpsrc_rtp0, "src");
  if(data->mode != RES_PASSIVE)
  {
    recv_rtp_sink1 = gst_element_request_pad (data->clientRTPBIN, recv_rtp_sink_temp, NULL, NULL);
    g_print ("<receiver>: Obtained request pad %s for recv_rtp_sink !.\n", gst_pad_get_name (recv_rtp_sink1));
    udpsrc_rtp1_src = gst_element_get_static_pad(data->udpsrc_rtp1, "src");
  } 

  recv_rtcp_sink_temp = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(data->clientRTPBIN), "recv_rtcp_sink_%u");
  recv_rtcp_sink0 = gst_element_request_pad(data->clientRTPBIN, recv_rtcp_sink_temp ,NULL, NULL);
  g_print ("<receiver>: Obtained request pad %s for recv_rtcp_sink !.\n", gst_pad_get_name (recv_rtcp_sink0));
  udpsrc_rtcp0_src = gst_element_get_static_pad(data->udpsrc_rtcp0, "src");
  if(data->mode != RES_PASSIVE)
  {
    recv_rtcp_sink1 = gst_element_request_pad(data->clientRTPBIN, recv_rtcp_sink_temp ,NULL, NULL);
    g_print ("<receiver>: Obtained request pad %s for recv_rtcp_sink !.\n", gst_pad_get_name (recv_rtcp_sink1));
    udpsrc_rtcp1_src = gst_element_get_static_pad(data->udpsrc_rtcp1, "src");
  }

  send_rtcp_src_temp = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS(data->clientRTPBIN), "send_rtcp_src_%u");
  send_rtcp_src0 = gst_element_request_pad(data->clientRTPBIN, send_rtcp_src_temp, NULL, NULL);
  g_print ("<receiver>: Obtained request pad %s for send_rtcp_src !.\n", gst_pad_get_name (send_rtcp_src0));
  udpsink_rtcp0_sink = gst_element_get_static_pad(data->udpsink_rtcp0, "sink");
  if(data->mode != RES_PASSIVE)
  {
    send_rtcp_src1 = gst_element_request_pad(data->clientRTPBIN, send_rtcp_src_temp, NULL, NULL);
    g_print ("<receiver>: Obtained request pad %s for send_rtcp_src !.\n", gst_pad_get_name (send_rtcp_src1));
    udpsink_rtcp1_sink = gst_element_get_static_pad(data->udpsink_rtcp1, "sink");
  }
 
  if(gst_pad_link (udpsrc_rtp0_src, recv_rtp_sink0 )!= GST_PAD_LINK_OK ||
     gst_pad_link (udpsrc_rtcp0_src, recv_rtcp_sink0 )!= GST_PAD_LINK_OK ||
     gst_pad_link (send_rtcp_src0, udpsink_rtcp0_sink )!= GST_PAD_LINK_OK )
  {
    g_printerr ("Some requested pads cannot be linked with static pads!\n");
    gst_object_unref (data->clientPipeline);
    return FALSE;
  }

  if( data->mode != RES_PASSIVE && (gst_pad_link (udpsrc_rtp1_src, recv_rtp_sink1 )!= GST_PAD_LINK_OK ||
                                     gst_pad_link (udpsrc_rtcp1_src, recv_rtcp_sink1 )!= GST_PAD_LINK_OK ||
                                     gst_pad_link (send_rtcp_src1, udpsink_rtcp1_sink )!= GST_PAD_LINK_OK   )
    )
  {
    g_printerr ("Some requested pads cannot be linked with static pads!\n");
    gst_object_unref (data->clientPipeline);
    return FALSE;
  }

  gst_object_unref (udpsrc_rtp0_src);
  gst_object_unref (udpsrc_rtcp0_src);
  gst_object_unref (udpsink_rtcp0_sink);
  if(data->mode != RES_PASSIVE)
  {
    gst_object_unref (udpsrc_rtp1_src);
    gst_object_unref (udpsrc_rtcp1_src);
    gst_object_unref (udpsink_rtcp1_sink);
  }  

  g_signal_connect (data->clientRTPBIN, "pad-added", G_CALLBACK (rtpbin_pad_added_handler_recv), data);

  // bus = gst_element_get_bus (data->clientPipeline);
  // gst_bus_add_signal_watch (bus);
  // g_signal_connect (G_OBJECT (bus), "message::error", (GCallback)error_cb, data);
  // g_signal_connect (G_OBJECT (bus), "message::eos", (GCallback)eos_cb, data);
  // g_signal_connect (G_OBJECT (bus), "message::state-changed", (GCallback)state_changed_cb, data);
  // gst_object_unref (bus);

   data->vdata = new videoData;
   data->vdata->fromIp = data->fromIp;
   asprintf(&data->vdata->contact, "%s", data->fromname.c_str());
   
   // create_chat_window(data);

   ret=gst_element_set_state(data->clientPipeline, GST_STATE_PLAYING );
   if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("<receiver>: Unable to set the pipeline to the playing state.\n");
    gst_object_unref (data->clientPipeline);
    return FALSE;
  }

   // gtk_main();

/************************PLEASE COMMENT THIS AFTER YOU CREATE THE GUI MAIN LOOP**************************/
 GMainLoop *main_loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (main_loop);
/************************PLEASE COMMENT THIS AFTER YOU CREATE THE GUI MAIN LOOP**************************/
  
  /* Free resources */
  gst_element_release_request_pad (data->clientRTPBIN, recv_rtp_sink0);
  gst_element_release_request_pad (data->clientRTPBIN, recv_rtcp_sink0);
  gst_element_release_request_pad (data->clientRTPBIN, send_rtcp_src0);
  gst_object_unref (recv_rtp_sink0);
  gst_object_unref (send_rtcp_src0);
  gst_object_unref (recv_rtcp_sink0);

  if(data->mode != RES_PASSIVE)
  {
    gst_element_release_request_pad (data->clientRTPBIN, recv_rtp_sink1);
    gst_element_release_request_pad (data->clientRTPBIN, recv_rtcp_sink1);
    gst_element_release_request_pad (data->clientRTPBIN, send_rtcp_src1);
    gst_object_unref (recv_rtp_sink1);
    gst_object_unref (send_rtcp_src1);
    gst_object_unref (recv_rtcp_sink1);
  }

  gst_object_unref (bus);
  gst_element_set_state (data->clientPipeline, GST_STATE_NULL);
  gst_object_unref (data->clientPipeline);
  removeRecvPipeline(data);
  return TRUE;
}

gboolean addElementToParent(SentDataStream* data)
{
  
  if(data->mode != RES_PASSIVE)  
  {
     gst_bin_add_many(GST_BIN(dataSource->serverPipeline), data->video_encoder, data->audio_encoder, 
                        data->audiorate_controller, data->videorate_controller, data->video_queue_1, data->audio_queue_1,
                        data->videoPayloader, data->audioPayloader, data->audio_queue_2 , data->video_queue_2,
                        data->serverRTPBIN , data->udpsink_rtp0 , data->udpsink_rtp1 , data->udpsink_rtcp0 , data->udpsink_rtcp1 ,
                        data->udpsrc_rtcp0 , data->udpsrc_rtcp1, NULL);

     if(!(gst_element_sync_state_with_parent (data->video_encoder) && gst_element_sync_state_with_parent (data->audio_encoder) &&
         gst_element_sync_state_with_parent (data->audiorate_controller) && gst_element_sync_state_with_parent (data->videorate_controller) &&
         gst_element_sync_state_with_parent (data->video_queue_1) && gst_element_sync_state_with_parent (data->audio_queue_1) &&
         gst_element_sync_state_with_parent (data->video_queue_2) && gst_element_sync_state_with_parent (data->audio_queue_2) &&
         gst_element_sync_state_with_parent (data->videoPayloader) && gst_element_sync_state_with_parent (data->audioPayloader) &&
         gst_element_sync_state_with_parent (data->serverRTPBIN) && gst_element_sync_state_with_parent (data->udpsink_rtp0) &&
         gst_element_sync_state_with_parent (data->udpsink_rtp1) && gst_element_sync_state_with_parent (data->udpsink_rtcp0) &&
         gst_element_sync_state_with_parent (data->udpsink_rtcp1) && gst_element_sync_state_with_parent (data->udpsrc_rtcp0) &&
         gst_element_sync_state_with_parent (data->udpsrc_rtcp1) )
      )
      {
        printf("Some elements' state cannot be synced to the parent state!\n");
        return FALSE;
      }
  }
  else  /*only adds video*/
  {
     gst_bin_add_many(GST_BIN(dataSource->serverPipeline), data->video_encoder, data->videorate_controller, data->video_queue_1,
                        data->videoPayloader, data->video_queue_2, data->serverRTPBIN , data->udpsink_rtp0 ,  data->udpsink_rtcp0 , 
                        data->udpsrc_rtcp0 , data->fakeaudiosink, NULL);

     if(!(gst_element_sync_state_with_parent (data->video_encoder) && gst_element_sync_state_with_parent (data->videorate_controller) &&
          gst_element_sync_state_with_parent (data->video_queue_1) && gst_element_sync_state_with_parent (data->video_queue_2) && 
          gst_element_sync_state_with_parent (data->videoPayloader) && gst_element_sync_state_with_parent (data->serverRTPBIN) && 
          gst_element_sync_state_with_parent (data->udpsink_rtp0) && gst_element_sync_state_with_parent (data->udpsink_rtcp0) &&  
          gst_element_sync_state_with_parent (data->udpsrc_rtcp0) && gst_element_sync_state_with_parent (data->fakeaudiosink ) )
      )
      {
        printf("<passive mode>: Some elements' state cannot be synced to the parent state!\n");
        return FALSE;
      }
  } 
  printf("synced successful!\n");
  return TRUE;
}

void buildSendPipeline(SentDataStream* data, const int startPort)
{
  gint res_width, res_height;

  gint port_send_rtp_src0 = startPort ;
  gint port_send_rtp_src1 = startPort+1;
  gint port_send_rtcp_src0 = startPort+2;
  gint port_send_rtcp_src1 = startPort+3;
  gint port_recv_rtcp_sink0 = startPort+4;
  gint port_recv_rtcp_sink1 = startPort+5;

  /*Pads for requesting*/
  GstPadTemplate *send_rtp_sink_temp, *send_rtcp_src_temp, *recv_rtcp_sink_temp;
  GstPadTemplate *video_tee_src_pad_template;
  GstPadTemplate *audio_tee_src_pad_template;
  GstPad *send_rtp_sink0, *send_rtp_sink1;
  GstPad *send_rtcp_src0, *send_rtcp_src1;
  GstPad *recv_rtcp_sink0, *recv_rtcp_sink1; 

  /*static pads*/
  GstPad *video_queue2_srcPad, *audio_queue2_srcPad;
  GstPad *udpsink_rtcp0_sink, *udpsink_rtcp1_sink ;
  GstPad *udpsrc_rtcp0_src, *udpsrc_rtcp1_src ;

  GstPad *videoqueue_sinkpad, *audioqueue_sinkpad, *audio_fakesink_sinkpad;

  GstBus *bus;
  GstStateChangeReturn ret;

  if(data->mode==RES_HIGH)
  {
    res_width = 640;
    res_height = 480;
  }
  else if(data->mode == RES_MEDIAN)
  {
    res_width = 320;
    res_height = 240;
  }
  else{
    res_width = 320;
    res_height = 240; 
    printf("streaming in passive mode\n");
  }

  string name_VE = to_string(startPort) + "video_encoder" ;
  string name_AE = to_string(startPort) + "audio_encoder"; 
  string name_AQ1 = to_string(startPort) + "audio_queue_1";
  string name_VQ1 = to_string(startPort) + "video_queue_1";
  string name_AQ2 = to_string(startPort) + "audio_queue_2";
  string name_VQ2 = to_string(startPort) + "video_queue_2" ;
  string name_VC = to_string(startPort) + "videorate_controller" ;
  string name_AC = to_string(startPort) + "audiorate_controller" ;
  string name_VP = to_string(startPort) + "videoPayloader" ;
  string name_AP = to_string(startPort) + "audioPayloader" ;
  string name_SR = to_string(startPort) + "serverRTPBIN" ;

  string name_udpsink_rtp0 = to_string(startPort) + "udpsink_rtp0" ;
  string name_udpsink_rtp1 = to_string(startPort) + "udpsink_rtp1" ;
  string name_udpsink_rtcp0 = to_string(startPort) + "udpsink_rtcp0" ;
  string name_udpsink_rtcp1 = to_string(startPort) + "udpsink_rtcp1" ;
  string name_udpsrc_rtcp0 = to_string(startPort) + "udpsrc_rtcp0" ;
  string name_udpsrc_rtcp1 = to_string(startPort) + "udpsrc_rtcp1" ;
  string name_fakeaudiosink = to_string(startPort) + "fakeaudiosink" ;


  data->video_encoder = gst_element_factory_make("jpegenc", name_VE.c_str());
  data->audio_encoder = gst_element_factory_make("amrnbenc", name_AE.c_str());    

  data->audio_queue_1 = gst_element_factory_make("queue", name_AQ1.c_str());
  data->video_queue_1 = gst_element_factory_make("queue", name_VQ1.c_str());

  data->audio_queue_2 = gst_element_factory_make("queue", name_AQ2.c_str());
  data->video_queue_2 = gst_element_factory_make("queue", name_VQ2.c_str());

  data->videorate_controller = gst_element_factory_make("videorate", name_VC.c_str() );
  data->audiorate_controller = gst_element_factory_make("audiorate", name_AC.c_str() );
  // data->video_scale = gst_element_factory_make("videoscale", name_VS.c_str() );

  data->videoPayloader = gst_element_factory_make("rtpjpegpay", name_VP.c_str());
  data->audioPayloader = gst_element_factory_make("rtpamrpay", name_AP.c_str());

  data->serverRTPBIN = gst_element_factory_make("rtpbin", name_SR.c_str());

  data->udpsink_rtp0 = gst_element_factory_make("udpsink",  name_udpsink_rtp0.c_str());
  data->udpsink_rtp1 = gst_element_factory_make("udpsink",  name_udpsink_rtp1.c_str());

  data->udpsink_rtcp0 = gst_element_factory_make("udpsink", name_udpsink_rtcp0.c_str());
  data->udpsink_rtcp1 = gst_element_factory_make("udpsink", name_udpsink_rtcp1.c_str());
  
  data->udpsrc_rtcp0 = gst_element_factory_make("udpsrc", name_udpsrc_rtcp0.c_str());
  data->udpsrc_rtcp1 = gst_element_factory_make("udpsrc", name_udpsrc_rtcp1.c_str());

  data->fakeaudiosink = gst_element_factory_make("fakesink", name_fakeaudiosink.c_str()); 

  /* Check that elements are correctly initialized */
  if(!(data->audio_encoder && data->video_encoder && data->audio_queue_1 && data->video_queue_1 && 
       data->audio_queue_2 && data->video_queue_2 && data->videorate_controller && data->audiorate_controller &&
       data->videoPayloader && data->audioPayloader && data->serverRTPBIN && data->udpsink_rtp0 &&  data->udpsink_rtp1 && 
       data->udpsink_rtcp0 && data->udpsink_rtcp1 && data->udpsrc_rtcp0 && data->udpsrc_rtcp1 && data->fakeaudiosink ))
  {
    g_critical("Couldn't create pipeline elements");
    return ;
  }

  printf("<sender>: sending rtp packets to ports %d and %d, and rtcp packets to ports %d and %d, to user %s \n",
           port_send_rtp_src0, port_send_rtp_src1, port_send_rtcp_src0, port_send_rtcp_src1, (gchar*)data->toIp.c_str() );
  printf("<sender>: receiving rtcp packets at ports %d and %d \n", port_recv_rtcp_sink0, port_recv_rtcp_sink1);

   g_object_set(data->udpsink_rtp0, "host", (gchar*)data->toIp.c_str(), "port", port_send_rtp_src0 ,/*"async", FALSE, "sync",TRUE ,*/NULL);
   g_object_set(data->udpsink_rtp1, "host", (gchar*)data->toIp.c_str(), "port", port_send_rtp_src1 , NULL);
   g_object_set(data->udpsink_rtcp0, "host",(gchar*)data->toIp.c_str(), "port", port_send_rtcp_src0, "async", FALSE, "sync", FALSE, NULL);
   g_object_set(data->udpsink_rtcp1, "host",(gchar*)data->toIp.c_str(), "port", port_send_rtcp_src1, "async", FALSE, "sync", FALSE, NULL);
   g_object_set(data->udpsrc_rtcp0 , "port", port_recv_rtcp_sink0 , NULL);
   g_object_set(data->udpsrc_rtcp1 , "port", port_recv_rtcp_sink1 , NULL);
   g_object_set(data->videorate_controller , "drop-only", TRUE, "max-rate", data->FPS, NULL);

   data->video_caps = gst_caps_new_simple("video/x-raw",
          "framerate", GST_TYPE_FRACTION, 30, 1,
          "width", G_TYPE_INT, res_width,
          "height", G_TYPE_INT, res_height,
          NULL);

   data->audio_caps = gst_caps_new_simple("audio/x-raw",
        "rate", G_TYPE_INT, 8000,
        NULL);

    addElementToParent(data);

    if(!gst_element_link_filtered(data->video_queue_1, data->videorate_controller, data->video_caps))
    {
      printf("<to port %d> video_queue_1 and videorate cannot be linked!\n", startPort);
      return ;
    }
    if(!gst_element_link_many(data->videorate_controller, data->video_encoder, 
                                data->videoPayloader, data->video_queue_2, NULL))
    {
      g_printerr("videorate, video_encoder, and video payloader cannot be linked!.\n");
      return;
    }
    if(data->mode != RES_PASSIVE && !gst_element_link_filtered(data->audio_queue_1, data->audiorate_controller, data->audio_caps))
    {
      g_printerr("audio_queue_1 and audiorate cannot be linked!.\n");
      return ;
    }
    if(data->mode != RES_PASSIVE && !gst_element_link_many(data->audiorate_controller, data->audio_encoder,
                                data->audioPayloader, data->audio_queue_2, NULL))
    {
      g_printerr("audiorate, audio_encoder, and audioPayloader cannot be linked!.\n");
      return ;
    }

    g_signal_connect (data->serverRTPBIN, "pad-added", G_CALLBACK (rtpbin_pad_added_handler_send), data);

    send_rtp_sink_temp = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS(data->serverRTPBIN), "send_rtp_sink_%u");
    send_rtp_sink0 = gst_element_request_pad (data->serverRTPBIN, send_rtp_sink_temp, NULL, NULL);
    g_print ("<sender>: Obtained request pad %s for send_rtp_sink !.\n", gst_pad_get_name (send_rtp_sink0));
    video_queue2_srcPad = gst_element_get_static_pad(data->video_queue_2, "src");
    if(data->mode != RES_PASSIVE)
    { 
      send_rtp_sink1 = gst_element_request_pad (data->serverRTPBIN, send_rtp_sink_temp, NULL, NULL);
      g_print ("<sender>: Obtained request pad %s for send_rtp_sink !.\n", gst_pad_get_name (send_rtp_sink1));
      audio_queue2_srcPad = gst_element_get_static_pad(data->audio_queue_2, "src");
    } 
    
    send_rtcp_src_temp = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS(data->serverRTPBIN), "send_rtcp_src_%u");
    send_rtcp_src0 = gst_element_request_pad(data->serverRTPBIN, send_rtcp_src_temp, NULL, NULL);
    g_print ("<sender>: Obtained request pad %s for send_rtcp_src !.\n", gst_pad_get_name (send_rtcp_src0));
    udpsink_rtcp0_sink = gst_element_get_static_pad(data->udpsink_rtcp0, "sink");
    if(data->mode != RES_PASSIVE)
    { send_rtcp_src1 = gst_element_request_pad(data->serverRTPBIN, send_rtcp_src_temp, NULL, NULL);
      g_print ("<sender>: Obtained request pad %s for send_rtcp_src !.\n", gst_pad_get_name (send_rtcp_src1));
      udpsink_rtcp1_sink = gst_element_get_static_pad(data->udpsink_rtcp1, "sink");
    } 

    recv_rtcp_sink_temp = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(data->serverRTPBIN), "recv_rtcp_sink_%u");
    recv_rtcp_sink0 = gst_element_request_pad(data->serverRTPBIN, recv_rtcp_sink_temp ,NULL, NULL);
    g_print ("<sender>: Obtained request pad %s for recv_rtcp_sink !.\n", gst_pad_get_name (recv_rtcp_sink0));
    udpsrc_rtcp0_src = gst_element_get_static_pad(data->udpsrc_rtcp0, "src");
    if(data->mode != RES_PASSIVE)
    {
      recv_rtcp_sink1 = gst_element_request_pad(data->serverRTPBIN, recv_rtcp_sink_temp ,NULL, NULL);
      g_print ("<sender>: Obtained request pad %s for recv_rtcp_sink !.\n", gst_pad_get_name (recv_rtcp_sink1));
      udpsrc_rtcp1_src = gst_element_get_static_pad(data->udpsrc_rtcp1, "src");
    }

    if( gst_pad_link (video_queue2_srcPad, send_rtp_sink0) != GST_PAD_LINK_OK ||
        gst_pad_link (send_rtcp_src0, udpsink_rtcp0_sink )!= GST_PAD_LINK_OK ||
        gst_pad_link (udpsrc_rtcp0_src, recv_rtcp_sink0 )!= GST_PAD_LINK_OK )
      {
        g_printerr ("<sender>: Some requested pads cannot be linked with static pads!\n");
        return;
      }

    if ( data->mode != RES_PASSIVE && (gst_pad_link (audio_queue2_srcPad, send_rtp_sink1) != GST_PAD_LINK_OK ||
           gst_pad_link (send_rtcp_src1, udpsink_rtcp1_sink )!= GST_PAD_LINK_OK ||
           gst_pad_link (udpsrc_rtcp1_src, recv_rtcp_sink1 )!= GST_PAD_LINK_OK ) )
    {
        g_printerr ("<sender>: In active mode, Some audio requested pads cannot be linked with static pads!\n");
        return ;
    }
    gst_object_unref (video_queue2_srcPad);
    gst_object_unref (udpsink_rtcp0_sink);
    gst_object_unref (udpsrc_rtcp0_src);
    if(data->mode != RES_PASSIVE)
    {
      gst_object_unref (audio_queue2_srcPad);
      gst_object_unref (udpsink_rtcp1_sink);
      gst_object_unref (udpsrc_rtcp1_src);
    }
    
    /* Manually link the data src Tees, which have "Request" pads */
    video_tee_src_pad_template = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (dataSource->video_tee), "src_%u");
    data->tee_video_pad = gst_element_request_pad (dataSource->video_tee, video_tee_src_pad_template, NULL, NULL);
    g_print ("<sender-videotee>: Obtained request pad %s for video branch.\n", gst_pad_get_name (data->tee_video_pad));
    videoqueue_sinkpad = gst_element_get_static_pad (data->video_queue_1, "sink");

    audio_tee_src_pad_template = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (dataSource->audio_tee), "src_%u");
    data->tee_audio_pad = gst_element_request_pad (dataSource->audio_tee, audio_tee_src_pad_template, NULL, NULL);
    g_print ("<sender-audiotee>: Obtained request pad %s for audio branch.\n", gst_pad_get_name (data->tee_audio_pad));
    if(data->mode != RES_PASSIVE)
    {
      audioqueue_sinkpad = gst_element_get_static_pad (data->audio_queue_1, "sink");
    } 
    else //have to link with the fakesink!
    {
      audio_fakesink_sinkpad = gst_element_get_static_pad (data->fakeaudiosink, "sink");
      printf("obtain audio fakesink pad\n");
    }
      
    if ( gst_pad_link (data->tee_video_pad, videoqueue_sinkpad) != GST_PAD_LINK_OK ||
         (data->mode !=RES_PASSIVE && gst_pad_link (data->tee_audio_pad, audioqueue_sinkpad) != GST_PAD_LINK_OK )
       ) 
    {
      g_printerr ("<sender-tee>: Tee could not be linked.\n");
      return ;
    }
    gst_object_unref (videoqueue_sinkpad);
    
    if(data->mode != RES_PASSIVE)
      gst_object_unref (audioqueue_sinkpad);
    else //passive mode 
    {
      if(gst_pad_link (data->tee_audio_pad, audio_fakesink_sinkpad) != GST_PAD_LINK_OK)
      {
        g_printerr ("<sender-tee>: Tee could not be linked with the fakesink.\n");
        return ;   
      }
      gst_object_unref (audio_fakesink_sinkpad);
    }

    ret=gst_element_set_state(dataSource->serverPipeline, GST_STATE_PLAYING);

    if(!g_main_loop_is_running(dataSource->main_loop))
    {
      printf("<sender-tee>: set the first sending loop to run!\n");
       g_main_loop_run(dataSource->main_loop);
    }
    else
    {
        printf("data source is aleady running!\n");
    }
    
}

void removeRecvPipeline(RecvDataStream* data)
{
  printf("removing recv pipeline\n");
  if(!data || !data->clientPipeline) return ;
  gst_element_set_state (data->clientPipeline, GST_STATE_NULL);
  gst_object_unref (data->clientPipeline);
  delete data;
  data = NULL;
}

void removeSendPipeline(SentDataStream* data)
{
  printf("removing sender pipeline branch \n");
  if(data->mode!=RES_PASSIVE && data->tee_audio_pad)
  {
    gst_pad_add_probe (data->tee_audio_pad, GST_PAD_PROBE_TYPE_BLOCK, remove_audio_cb, data, NULL);
  }
  if(data->tee_video_pad)
  {
    gst_pad_add_probe (data->tee_video_pad, GST_PAD_PROBE_TYPE_BLOCK, remove_video_cb, data, NULL);
  }
  return ;
}

void *incomingData(void *arg)
{
  RecvDataStream* data = (RecvDataStream*)arg ;
  int startPort = (*dataSource->partnersInfo)[data->fromIp].second;
  cout <<"receiving from ip " << data->fromIp << "  at port "<< startPort <<endl;

  buildRecvPipeline(data, startPort);
  return NULL;
}

void *outcomingData(void *arg)
{
  SentDataStream* data = (SentDataStream*)arg ;
  int startPort = (*dataSource->partnersInfo)[data->toIp].second;
  cout <<"connecting to ip " << data->toIp<< "  at port "<< startPort <<endl;
  buildSendPipeline(data, startPort);
  return NULL;
}

/*this function is called when the user wants to video chat with another user;
  it spawns two thread: one for receiving data from the other side and one for 
  sending data to the other side.
  */
void startNewSession(const string& ip, int mode)
{
  cout<<"connecting to " <<ip << "port "<< (*dataSource->partnersInfo)[ip].second <<endl;
  RecvDataStream* idata = new RecvDataStream;
  SentDataStream* odata = new SentDataStream;
  idata->fromIp = ip;
  idata->mode = mode;
  idata->fromname = (*dataSource->partnersInfo)[ip].first;
  idata->clientPipeline = NULL;
 
  odata->toIp = ip;
  odata->toname = (*dataSource->partnersInfo)[ip].first;
  odata->mode = mode;
  odata->FPS = 10 ; 
  odata->tee_audio_pad = NULL;
  odata->tee_video_pad = NULL;
  if(odata->mode != RES_PASSIVE)
    odata->audio_removed = FALSE;
  else
    odata->audio_removed = TRUE;
  odata->video_removed = FALSE ; 


  pthread_mutex_lock(&databranchlock); 
  (*dataSource->idata)[ip] = idata;
  (*dataSource->odata)[ip] = odata;
  pthread_mutex_unlock(&databranchlock);
 
  pthread_t r,s;
  pthread_create(&r, NULL, incomingData, (void*)(idata) );  
  pthread_create(&s, NULL, outcomingData, (void*)(odata) );
      
  pthread_mutex_lock(&vLock);
  waitingThreads.push_back(r);
  waitingThreads.push_back(s);
  pthread_mutex_unlock(&vLock);
}

void tearDownSession(const string& ip)
{
  SentDataStream* odata = NULL;
  if(dataSource->idata->find(ip) == dataSource->idata->end())
  {
    fprintf(stderr, "<tearDownSession> incoming data does not exist!\n");
  }
  else
  {
    removeRecvPipeline((*dataSource->idata)[ip]);
    pthread_mutex_lock(&databranchlock); 
    dataSource->idata->erase(ip);
    pthread_mutex_unlock(&databranchlock);
  }
  if(dataSource->odata->find(ip) == dataSource->odata->end())
  {
    fprintf(stderr, "<tearDownSession> outcoming data does not exist!\n");
    return;
  }
  else
  {
    odata = (*dataSource->odata)[ip] ;
    removeSendPipeline(odata) ;
  }

  pthread_mutex_lock(&sendPipelineLock);
  while(!(odata->audio_removed && odata->video_removed ))
  {
    pthread_cond_wait(&readyToDelete, &sendPipelineLock);
  }
  pthread_mutex_unlock(&sendPipelineLock);
    
  pthread_mutex_lock(&databranchlock); 
  dataSource->odata->erase(ip);
  pthread_mutex_unlock(&databranchlock); 
  
  printf("disconnect to user %s\n", ip.c_str());

  if(dataSource->odata->empty())
  {
      printf("No users left, quit the loop!\n");
      g_main_loop_quit(dataSource->main_loop);    
  }
}

/*add initial pipeline elements, including two sources(A/V) and two tees*/
gboolean initUserPipeline(SentData* data)
{
  data->serverPipeline = gst_pipeline_new("serverPipeline");

  /* Camera video stream comes from a Video4Linux driver */
  data->camera_video_src = gst_element_factory_make("v4l2src", "camera_video_src");
  /*audio source*/
  data->camera_audio_src = gst_element_factory_make("alsasrc", "camera_audio_src");
  data->video_tee = gst_element_factory_make("tee", "video_tee");
  data->audio_tee = gst_element_factory_make("tee", "audio_tee");

  data->partnersInfo = new unordered_map<string, pair<string, int> > ;
  data->idata = new unordered_map<string, RecvDataStream* >;
  data->odata = new unordered_map<string, SentDataStream* >;



  /* Check that elements are correctly initialized */
  if(!(data->serverPipeline && data->camera_video_src && data->camera_audio_src && 
       data->video_tee && data->audio_tee && data->partnersInfo && data->idata && data->odata ))
  {
    g_critical("Couldn't create initial server pipeline elements");
    return FALSE;
  }
  gst_bin_add_many(GST_BIN(data->serverPipeline), data->camera_video_src, data->camera_audio_src, 
                           data->video_tee, data->audio_tee, NULL);

  /*linking inital elements*/
  if(!gst_element_link_many(data->camera_video_src, data->video_tee, NULL))
  {
    g_printerr("video source and tee cannot be linked!\n");
    gst_object_unref (data->serverPipeline);
    return FALSE;
  }
  if(!gst_element_link_many(data->camera_audio_src, data->audio_tee, NULL))
  {
    g_printerr("audio source and tee cannot be linked!\n");
    gst_object_unref (data->serverPipeline);
    return FALSE;
  }
  data->main_loop = g_main_loop_new (NULL, FALSE);
  return TRUE;
}

  // std::chrono::milliseconds dura( 2000 );
  // std::this_thread::sleep_for( dura );
  

void getSystemName(void)
{
  struct utsname systemName ;
  bzero((struct utsname*)(&systemName), sizeof(struct utsname));

  if(uname(&systemName)==0)
  {
    asprintf(&username, "%s", systemName.nodename);
    // printf("%s\n", systemName.sysname );
    // printf("%s\n", systemName.nodename);    
    // printf("%s\n", systemName.release );    
    // printf("%s\n", systemName.version );    
    // printf("%s\n", systemName.machine );    
  }
}

 
static void rtpbin_pad_added_handler_recv(GstElement *src, GstPad *new_pad, RecvDataStream *data)
{
      GstPad *video_queue_sinkPad = gst_element_get_static_pad (data->video_queue, "sink");
      GstPad *audio_queue_sinkPad ;
      if(data->mode != RES_PASSIVE)
        audio_queue_sinkPad = gst_element_get_static_pad (data->audio_queue, "sink");

      GstPadLinkReturn ret;

      g_print ("<receiver>: Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));
      if ( ( data->mode == RES_PASSIVE && gst_pad_is_linked(video_queue_sinkPad) ) || 
           ( gst_pad_is_linked(video_queue_sinkPad) && gst_pad_is_linked(audio_queue_sinkPad) )
         ) 
      {
        g_print ("<receiver>  We are already linked. Ignoring.\n");
        goto exit;
      }

       if(strstr(GST_PAD_NAME (new_pad), "recv_rtp_src"))
       {
          if(strstr(GST_PAD_NAME(new_pad), "recv_rtp_src_0"))
           {
                ret = gst_pad_link (new_pad, video_queue_sinkPad);
                if (GST_PAD_LINK_FAILED (ret))
                {
                    g_print ("<receiver-rtpbin>: New pad is '%s' but link failed with error: %d \n", 
                            GST_PAD_NAME (new_pad), ret);
                }
                else
                {
                    g_print ("<receiver-video> Link succeeded (pad '%s') \n ", GST_PAD_NAME (new_pad) );
                }
           }
           else if(strstr(GST_PAD_NAME(new_pad), "recv_rtp_src_1"))
           {
                ret = gst_pad_link (new_pad, audio_queue_sinkPad);
                if (GST_PAD_LINK_FAILED (ret))
                {
                    g_print ("<receiver-rtpbin>: New pad is '%s' but link failed with error: %d \n", 
                              GST_PAD_NAME (new_pad), ret);
                }
                else
                {
                    g_print ("<receiver-audio> Link succeeded (pad '%s'). \n", GST_PAD_NAME (new_pad) );
                }
           }
      }

      exit: 
      /* Unreference the sink pad */
      gst_object_unref (video_queue_sinkPad);
      if(data->mode != RES_PASSIVE)
        gst_object_unref (audio_queue_sinkPad);
}

   
static void rtpbin_pad_added_handler_send(GstElement *src, GstPad *new_pad, SentDataStream *data)
{
      GstPad *udpsink_rtp0_sink = gst_element_get_static_pad (data->udpsink_rtp0, "sink");
      GstPad *udpsink_rtp1_sink = NULL;
      if(data->mode != RES_PASSIVE)
        udpsink_rtp1_sink = gst_element_get_static_pad (data->udpsink_rtp1, "sink");

      GstPadLinkReturn ret;

      if ( ( data->mode == RES_PASSIVE && gst_pad_is_linked(udpsink_rtp0_sink) ) ||
           ( gst_pad_is_linked(udpsink_rtp0_sink) && gst_pad_is_linked(udpsink_rtp1_sink) ) 
         ) 
      {
        g_print ("<sender>: We are already linked. Ignoring.\n");
        goto exit;
      }
      
      if(strstr(GST_PAD_NAME (new_pad), "send_rtp_src"))
      {
          g_print ("<sender-rtpbin>: Received new pad '%s' from '%s' \n ", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));
          if(strstr(GST_PAD_NAME(new_pad), "0"))
          {
                ret = gst_pad_link (new_pad, udpsink_rtp0_sink);
                if (GST_PAD_LINK_FAILED (ret))
                {
                    g_print ("<sender-rtpbin>: New pad is '%s' but link failed.\n", GST_PAD_NAME (new_pad));
                }
                else
                {
                    g_print ("<sender-rtpbin>: Link succeeded (pad '%s')\n", GST_PAD_NAME (new_pad) );
                    // print_caps (new_pad_caps, "      ");
                }
           }    
            if(strstr(GST_PAD_NAME(new_pad), "1"))
            {
                 ret = gst_pad_link (new_pad, udpsink_rtp1_sink);
                 if (GST_PAD_LINK_FAILED (ret)) 
                 {
                     g_print ("<sender-rtpbin>: New pad is '%s' but link failed.\n", GST_PAD_NAME (new_pad));
                 } 
                 else 
                 {
                     g_print ("<sender-rtpbin>: Link succeeded (pad '%s')\n", GST_PAD_NAME (new_pad));
                 }
            }      
      }

      exit:
      /* Unreference the sink pad */
      gst_object_unref (udpsink_rtp0_sink);
      if(data->mode != RES_PASSIVE)
        gst_object_unref (udpsink_rtp1_sink);
}


/*entry point for a new user
  usage: ./user <to_ip_address>*/
int main(int argc, char *argv[]) 
{ 
  gtk_init (&argc, &argv);
  gst_init(&argc, &argv);
  // XInitThreads();
  pthread_mutex_init(&vLock, NULL);
  pthread_mutex_init(&sendPipelineLock, NULL);
  pthread_mutex_init(&databranchlock , NULL);
  pthread_cond_init(&readyToDelete, NULL);   

  dataSource = new SentData;
  dataSource->partnersInfo= new unordered_map<string, pair<string, int> >;
  getSystemName();

  if(!initUserPipeline(dataSource))
  {
    fprintf(stderr, "Error occurs in initializing user's initial pipeline!\n");
    exit(1);
  }
  printf("%s enters mp3 chatroom!\n", username);

  GuiData gdata;
  myGui=&gdata;

  create_welcome(&gdata);
  pthread_t t1;
  pthread_create(&t1,NULL,ListenMsgClient,NULL);  

  gtk_main();

  printf("GTK gui quits\n");
  delete dataSource;

  pthread_mutex_destroy(&vLock);
  pthread_mutex_destroy(&sendPipelineLock);
  pthread_mutex_destroy(&databranchlock);
  pthread_cond_destroy(&readyToDelete);    

  printf("User leaves mp3 chatroom!\n");
  pthread_join(t1,NULL);
  return 0;
}

void testPipelineRecv(const string& ip, const int& port)
{
  RecvDataStream* data = new RecvDataStream ;
  data->mode = ACTIVE_MODE;
  data->fromIp = ip;
  buildRecvPipeline(data, port);
  delete data;
}
void testPipelineSend(const string& ip, const int& port)
{
  SentDataStream* data = new SentDataStream;
  data->mode = ACTIVE_MODE;
  data->toIp =  ip;
  buildSendPipeline(data, port);
  delete data;
}

gboolean change_mode(string DestIP, int tomode)
{

    tearDownSession(DestIP);
    startNewSession(DestIP, tomode);
    
    return TRUE;

    if( dataSource->odata->find(DestIP) == dataSource->odata->end() )
    {
      printf("cannot find the user you're connecting to !\n");
      return FALSE;
    }        
    if( (*dataSource->odata)[DestIP]->mode == tomode )
    {
      printf("you're already in the same mode you want to change to !\n");
      return FALSE;
    }        
    (*dataSource->odata)[DestIP]->mode = tomode;
    printf("%s\n", DestIP.c_str());
    GstPad* video_queue_pad = gst_element_get_static_pad( (*dataSource->odata)[DestIP]->video_queue_1 , "src");


    gst_pad_add_probe (video_queue_pad, GST_PAD_PROBE_TYPE_BLOCK, change_mode_cb, (*dataSource->odata)[DestIP], NULL);
    return TRUE;
}

static GstPadProbeReturn change_mode_cb(GstPad * video_queue_pad, GstPadProbeInfo * info,  gpointer userdata)
{
    GstPad *sinkpad;
    GstPad *videorate_sinkpad;
    GstPad *videorate_srcpad;
    GstPad *video_encoder_sink_pad;
    GstPad *video_queue_src_pad;

    SentDataStream* data = (SentDataStream*)userdata ;
    gint res_width, res_height;

    if(data->mode == RES_HIGH)
    {
      res_width = 640;
      res_height = 480;
    }
    else
    {
      res_width = 320;
      res_height = 240;
    }

    video_queue_src_pad = gst_element_get_static_pad(data->video_queue_1,  "src"); 
    videorate_sinkpad = gst_element_get_static_pad(data->videorate_controller,  "sink");
    videorate_srcpad = gst_element_get_static_pad(data->videorate_controller,  "src");
    video_encoder_sink_pad = gst_element_get_static_pad(data->video_encoder,  "sink");

    gst_pad_unlink(video_queue_src_pad, videorate_sinkpad);
    gst_pad_unlink(videorate_srcpad, video_encoder_sink_pad);

    data->video_caps = gst_caps_new_simple("video/x-raw",
          "framerate", GST_TYPE_FRACTION, 30, 1,
          "width", G_TYPE_INT, res_width,
          "height", G_TYPE_INT, res_height,
          NULL);

    printf("ready to relink : new resolution %d X %d \n", res_width, res_height );
    gboolean ret;
    gst_pad_remove_probe (video_queue_pad, GST_PAD_PROBE_INFO_ID (info));

    gst_element_sync_state_with_parent (data->videorate_controller) ;

    if(!gst_element_link_filtered (data->video_queue_1, data->videorate_controller, data->video_caps) )
    {
      printf("<in changemode_cb> video_queue_1 and videorate cannot be linked!\n");
    }

    if(gst_pad_link (videorate_srcpad, video_encoder_sink_pad) != GST_PAD_LINK_OK) {
      g_printerr ("Failed to link videorate and encoder\n");
      gst_object_unref (videorate_srcpad);
    }
    // if(!(ret = gst_element_link_filtered(data->video_queue_1, data->videorate_controller, data->video_caps) ) )
    // {
    //   // printf("%d \n", ret );
    //   printf("<in changemode_cb> video_queue_1 and videorate cannot be linked!\n");
    // }


    return GST_PAD_PROBE_REMOVE;
} 



static GstPadProbeReturn remove_audio_cb (GstPad * tee_audio_pad, GstPadProbeInfo * info,  gpointer userdata) 
{
    GstPad *sinkpad;
    GstPad *queue_audio_pad;
    SentDataStream* data = (SentDataStream*)userdata ;

    printf("removing audio branch!\n");
    sinkpad = gst_element_get_static_pad(data->audio_queue_1,  "sink");
    gst_pad_unlink(tee_audio_pad, sinkpad);
    gst_element_unlink_many (data->audio_queue_1, data->audiorate_controller, data->audio_encoder,
                            data->audioPayloader, data->audio_queue_2, NULL);
    
    gst_bin_remove_many (GST_BIN (dataSource->serverPipeline), data->audio_queue_1, data->audiorate_controller, data->audio_encoder,
                            data->audioPayloader, data->audio_queue_2, NULL);

    gst_object_unref(data->audio_queue_1);
    gst_object_unref(data->audiorate_controller);
    gst_object_unref(data->audio_encoder);
    gst_object_unref(data->audioPayloader);
    gst_object_unref(data->audio_queue_2);
    gst_object_unref (sinkpad); 

    data->audio_queue_1 = NULL;
    data->audiorate_controller = NULL;
    data->audio_encoder = NULL;
    data->audioPayloader = NULL;
    data->audio_queue_2 = NULL;
    
    pthread_mutex_lock(&sendPipelineLock);
    if(data->serverRTPBIN !=NULL)
    {
        gst_bin_remove_many(GST_BIN (dataSource->serverPipeline), data->serverRTPBIN, data->udpsink_rtp0, 
        data->udpsink_rtp1, data->udpsink_rtcp0,  data->udpsink_rtcp1, data->udpsrc_rtcp0, data->udpsrc_rtcp1, NULL);

        gst_object_unref(data->serverRTPBIN);
        gst_object_unref(data->udpsink_rtp0);
        gst_object_unref(data->udpsink_rtp1);
        gst_object_unref(data->udpsink_rtcp0);
        gst_object_unref(data->udpsink_rtcp1);
        gst_object_unref(data->udpsrc_rtcp0);
        gst_object_unref(data->udpsrc_rtcp1);

        data->serverRTPBIN = NULL;
        data->udpsink_rtp0 = NULL;
        data->udpsink_rtp1 = NULL;
        data->udpsink_rtcp0 = NULL;
        data->udpsink_rtcp1 = NULL;
        data->udpsrc_rtcp0 = NULL;
        data->udpsrc_rtcp1 = NULL;
      
    }
    data->audio_removed = TRUE;
    pthread_cond_signal(&readyToDelete);
    pthread_mutex_unlock(&sendPipelineLock);
    gst_pad_remove_probe (tee_audio_pad, GST_PAD_PROBE_INFO_ID (info));
    
    // gst_element_release_request_pad (dataSource->audio_tee, tee_audio_pad);
    // gst_object_unref (tee_audio_pad);
   
    printf("<remove_audio_cb>: audio branch removed\n");
    return GST_PAD_PROBE_DROP;
}

static GstPadProbeReturn remove_video_cb (GstPad * tee_video_pad, GstPadProbeInfo * info,  gpointer userData) 
{
    GstPad *sinkpad;
    GstPad *queue_video_pad;
    SentDataStream* data = (SentDataStream*)userData ;

    printf("removing video branch!\n");
    sinkpad = gst_element_get_static_pad(data->video_queue_1,  "sink");
    gst_pad_unlink(tee_video_pad, sinkpad);
    gst_element_unlink_many (data->video_queue_1, data->videorate_controller, data->video_encoder,
                            data->videoPayloader, data->video_queue_2, NULL);
    gst_bin_remove_many (GST_BIN (dataSource->serverPipeline), data->video_queue_1, data->videorate_controller, data->video_encoder,
                            data->videoPayloader, data->video_queue_2, NULL);

    gst_object_unref(data->video_queue_1);
    gst_object_unref(data->videorate_controller);
    gst_object_unref(data->video_encoder);
    gst_object_unref(data->videoPayloader);
    gst_object_unref(data->video_queue_2);
    gst_object_unref(sinkpad);

    data->video_queue_1 = NULL;
    data->videorate_controller = NULL;
    data->video_encoder = NULL;
    data->videoPayloader = NULL;
    data->video_queue_2 = NULL;
    
    pthread_mutex_lock(&sendPipelineLock);
    if(data->serverRTPBIN !=NULL)
    {
        gst_bin_remove_many(GST_BIN (dataSource->serverPipeline), data->serverRTPBIN, data->udpsink_rtp0, 
        data->udpsink_rtp1, data->udpsink_rtcp0,  data->udpsink_rtcp1, data->udpsrc_rtcp0, data->udpsrc_rtcp1, NULL);

        gst_object_unref(data->serverRTPBIN);
        gst_object_unref(data->udpsink_rtp0);
        gst_object_unref(data->udpsink_rtp1);
        gst_object_unref(data->udpsink_rtcp0);
        gst_object_unref(data->udpsink_rtcp1);
        gst_object_unref(data->udpsrc_rtcp0);
        gst_object_unref(data->udpsrc_rtcp1);

        data->serverRTPBIN = NULL;
        data->udpsink_rtp0 = NULL;
        data->udpsink_rtp1 = NULL;
        data->udpsink_rtcp0 = NULL;
        data->udpsink_rtcp1 = NULL;
        data->udpsrc_rtcp0 = NULL;
        data->udpsrc_rtcp1 = NULL;
    }
    data->video_removed = TRUE;
    pthread_cond_signal(&readyToDelete);
    pthread_mutex_unlock(&sendPipelineLock);

    gst_pad_remove_probe (tee_video_pad, GST_PAD_PROBE_INFO_ID (info));

    // gst_element_release_request_pad (dataSource->video_tee, tee_video_pad);
    // gst_object_unref (tee_video_pad);
   
    printf("<remove_video_cb>: video branch removed\n");
    return GST_PAD_PROBE_DROP;
}

//MP3_add server server code 
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

void *ListenMsgClient(void *arg){
  cout <<"ithread listen @@@@@@@@"<<endl;
  MessageS recvMsg;
  MessageS ErrorMsg;
  ErrorMsg.type=-1;

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

  if ((rv = getaddrinfo(NULL, S2CPORT, &hints, &servinfo)) != 0) 
  {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    exit(1);
  }

  // loop through all the results and bind to the first we can
  for(p = servinfo; p != NULL; p = p->ai_next) {
    if ((sockfd = socket(p->ai_family, p->ai_socktype,
        p->ai_protocol)) == -1) 
    {
      perror("server: socket");
      continue;
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
        sizeof(int)) == -1) 
    {
      perror("setsockopt");
      exit(1);
    }

    if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) 
    {
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

  if (listen(sockfd, BACKLOG) == -1) 
  {
    perror("listen");
    exit(1);
  }

  sa.sa_handler = sigchld_handler; // reap all dead processes
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if (sigaction(SIGCHLD, &sa, NULL) == -1) 
  {
    perror("sigaction");
    exit(1);
  }

  printf("client: waiting for connections...\n");

  while(1) 
  {  // main accept() loop
    sin_size = sizeof their_addr;
    new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
    if (new_fd == -1) {
      perror("accept");
      continue;
    }

    inet_ntop(their_addr.ss_family,
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

    if (recvMsg.type==CLEANLIST){
      AllUserMap.clear();
      UserIPPortMap.clear();
      //cout <<"11$$$$$$$$$$$$$$$$$$$$"<<endl;
      //if (dataSource->partnersInfo !=NULL)
      (*dataSource->partnersInfo).clear();
      //cout <<"22$$$$$$$$$$$$$$$$$$$$"<<endl;
    }

    if (recvMsg.type==UPDATELIST){
      // cout <<"[recv]user: "<<recvMsg.UserName;
      // cout <<" [recv]IP: "<<recvMsg.IPAddress<<endl;
      string tempUserName=recvMsg.UserName;
      string tempIPAddress=recvMsg.IPAddress;
      AllUserMap[tempUserName]=tempIPAddress;

    }

    if (recvMsg.type==UPDATEPORTINFO){
      string tempIPAddress=recvMsg.IPAddress;
      string tempUserName=recvMsg.UserName;
      int tempPort=recvMsg.CommPort;
      UserIPPortMap[tempIPAddress]=recvMsg.CommPort;

     
      cout <<tempUserName<<endl;
      cout <<tempIPAddress<<endl;
      cout <<tempPort<<endl;
      pair<string, int> tempPair (tempUserName,tempPort);
      cout <<"1$$$$$$$$$$$$$$$$$$$$"<<endl;
      cout <<"size:  "<<(*dataSource->partnersInfo).size()<<endl;
      (*dataSource->partnersInfo)[tempIPAddress]=tempPair;
      cout <<"2$$$$$$$$$$$$$$$$$$$$"<<endl;

      
    }

    if (recvMsg.type==STARTCONNECT){
        //recvMsg.IPAddress is the destionation Ip address
        //recvMsg.UserName is the destion username;
        string CoNodeName= (string)recvMsg.UserName;
        string DestIP = (string)recvMsg.IPAddress;
        if( dataSource->idata->find(DestIP) != dataSource->idata->end() ||
            dataSource->odata->find(DestIP) != dataSource->odata->end()   )
        {
          printf("You are already linked with user %s \n", DestIP.c_str() );
          close(new_fd);  // parent doesn't need this
          continue;
        }
        int DestStartPort= UserIPPortMap[DestIP];
        int tempBD=recvMsg.CommPort;
        cout << "the bandwidth returned by server is "<<tempBD<<endl;
     
        if(tempBD == HIGHMODE)
        {
          startNewSession(DestIP, RES_HIGH);
          printf("chatting in high mode!\n");          
        }
        else if (tempBD == LOWMODE)
        {
          printf("chatting in low mode!\n");
          startNewSession(DestIP, RES_MEDIAN);
        }
        else if(tempBD == MUTEMODE)
        {
          printf("Communicating in passive mode!\n");
          startNewSession(DestIP, RES_PASSIVE);          
        }
        else
          startNewSession(DestIP, RES_MEDIAN);

        // std::chrono::milliseconds dura( 10000 );
        // std::this_thread::sleep_for( dura );
        // change_mode(DestIP, RES_HIGH);


    }

    if (recvMsg.type==MODESWITCH){
        //recvMsg.IPAddress is the destionation Ip address
        //recvMsg.UserName is the destion username;
        string SwNodeName= (string)recvMsg.UserName;
        string DestIP = (string)recvMsg.IPAddress;
        int DestStartPort= UserIPPortMap[DestIP];
        int tempBD=recvMsg.CommPort;
        int tomode;

        if(tempBD == HIGHMODE)
          tomode = RES_HIGH;
        else if (tempBD == LOWMODE)
          tomode = RES_MEDIAN;
        else
        {
          printf("modechange fails!\n");
          close(new_fd);  
          continue;
        }

        change_mode(DestIP, tomode);

    }

    if (recvMsg.type==STOPCONNECT){
        //recvMsg.IPAddress is the destionation Ip address
        //recvMsg.UserName is the destion username;
        string tempselfIP= (string)recvMsg.UserName;
        string DestName = (string)recvMsg.IPAddress;
        cout <<"RECV disconnect selfIP (should =selfIP): "<< tempselfIP <<endl;
        cout <<"RECV disconnect to IP: "<< DestName <<endl;

        string DisTarget=AllUserMap[DestName];
        //DestIP is the destination IP, DestStartPort is startport, ModeBA is switch to Bindwidth
        tearDownSession(DisTarget);
        cout <<"@@@HU@@@ call start pipeline switch function here@@@"<<endl;
    }




    close(new_fd);  // parent doesn't need this
    ShowDataSourcePartnersInfo();
    ShowLocalMapInfo();
    ShowIPPortMapInfo();
  }
}

int sendMsg2Server(string IP, string PORT, MessageS sendMsg){
  cout <<"IP"<<IP.c_str()<<endl;
  cout <<"PORT"<<PORT.c_str()<<endl;
  int sockfd, numbytes;  
  char buf[MAXDATASIZE];
  struct addrinfo hints, *servinfo, *p;
  int rv;
  char s[INET6_ADDRSTRLEN];

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  // char tempIPchar[20];
  // char tempPortChar[20];
  // memcpy(tempIPchar,IP.c_str(),20);
  // memcpy(tempPortChar,PORT.c_str(),20);

  if ((rv = getaddrinfo(IP.c_str(), PORT.c_str(), &hints, &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return 1;
  }

  fprintf(stderr, "sendMsg2Servergetaddrinfo: %s\n", gai_strerror(rv));

  // loop through all the results and connect to the first we can
  for(p = servinfo; p != NULL; p = p->ai_next) {
    if ((sockfd = socket(p->ai_family, p->ai_socktype,
        p->ai_protocol)) == -1) {
      perror("client:sendMsg2Server socket");
      continue;
    }

    if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sockfd);
      perror("client:sendMsg2Server connect");
      continue;
    }

    break;
  }

  if (p == NULL) {
    fprintf(stderr, "client:sendMsg2Server failed to connect\n");
    return 2;
  }

  inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
      s, sizeof s);
  printf("client:sendMsg2Server connecting to %s\n", s);

  freeaddrinfo(servinfo); // all done with this structure


  cout << "SEND IP: "<< IP.c_str() <<endl;
  cout << "SEND PORT: "<< PORT.c_str() <<endl;
  cout << "SEND sendMsg.type: "<< sendMsg.type <<endl;
  cout << "SEND sendMsg.UserName: "<< sendMsg.UserName <<endl;
  cout << "SEND sendMsg.IPAddress: "<< sendMsg.IPAddress <<endl;

  if (send(sockfd, (char *) &sendMsg, sizeof(sendMsg), 0) == -1)
    perror("sendsendMsg2Server");


  cout <<":**************Send Finish******"<<endl;

  close(sockfd);
  return 0;
}

void ShowLocalMapInfo(void){
  cout <<"=========== local map begin ============"<<endl;
    for (auto it=AllUserMap.begin(); it!=AllUserMap.end(); ++it)
      cout << "[" << it->first << "]: [" <<  it->second << "]"<<endl;
    cout <<"=========== local map end =============="<<endl;
}

void ShowDataSourcePartnersInfo(void){
  cout <<"@@@@@@@@@@@@@@@@@@ DataSource PartnersInfo @@@@@@@@@@@@@@@@"<<endl;
    for (auto it=(*dataSource->partnersInfo).begin(); it!=(*dataSource->partnersInfo).end(); ++it)
        cout << "IP[" << it->first << "]==>[" <<  it->second.first << ": "<< it->second.second<<"]"<<endl;
    cout <<"@@@@@@@@@@@@@@@@@@ DataSource PartnersInfo @@@@@@@@@@@@@@@@"<<endl;
}

void ShowIPPortMapInfo(void){
    cout <<"+++++++++++++++local IP port begin +++++++++++++"<<endl;
    for (auto it=UserIPPortMap.begin(); it!=UserIPPortMap.end(); ++it)
        cout << "[" << it->first << "]: [" <<  it->second << "]"<<endl;
    cout <<"+++++++++++++++local IP port end +++++++++++++++"<<endl;
}

///below is GUI part for MP3
void set_background( GtkWidget *window )
{
    GtkStyle *style;
    GdkPixbuf *pixbuf;
    GdkPixmap *background;
    GError *error = NULL;

    pixbuf = gdk_pixbuf_new_from_file ("img.jpg",&error);
    if (error != NULL) {
        if (error->domain == GDK_PIXBUF_ERROR) {
            g_print ("Pixbuf Related Error:\n");
        }
        if (error->domain == G_FILE_ERROR) {
            g_print ("File Error: Check file permissions and state:\n");
        }

        g_printerr ("%s\n", error[0].message);
        exit(1);
    }
    gdk_pixbuf_render_pixmap_and_mask (pixbuf, &background, NULL, 0);
    style = gtk_style_new ();
    style->bg_pixmap[0] = background;


    gtk_widget_set_style (GTK_WIDGET(window), GTK_STYLE(style));
}
// void menuitem_response(void * inp)
// {
//   //click user name callback function
//   printf("chatting\n");
// }

void stop_chating(GtkButton *button,  GuiData * data)
{
  printf("!!!!!!!!!!!!!!stop_chating :%s\n", (char *)data);
  string TalktoName((char *)data);
  MessageS tempmsg;
  tempmsg.type=STOPCONNECT;

  cout << "'disconnect selfName:"<<SelfUserName<<endl;
  cout << "'disconnect TalktoName:"<<TalktoName<<endl;
  strcpy(tempmsg.UserName, SelfUserName.c_str());
  strcpy(tempmsg.IPAddress,TalktoName.c_str());
  //!!!!!!!!!!!!!!!!!!!!need select mode to select to select the right mode
  //tempmsg.CommPort store the desired Bindwidth
  //tempmsg.CommPort=HIGHMODE;
  sendMsg2Server(ServerIP, C2SPORT,tempmsg);
  
  string  DestIP= AllUserMap[TalktoName];
  //@@@ HU @@@ call self dissconnect function
  tearDownSession(DestIP);
}


void hm_cb(GtkButton *button,  GuiData * data)
{
  printf("high mode\n");
  string TalktoName((char *)data);
  MessageS tempmsg;
  tempmsg.type=STARTCONNECT;

  //give talker's Username and talkto's Username to this structure
  //and the CommPort store the connect type
  strcpy(tempmsg.UserName, SelfUserName.c_str());
  strcpy(tempmsg.IPAddress,TalktoName.c_str());
  //!!!!!!!!!!!!!!!!!!!!need select mode to select to select the right mode
  //tempmsg.CommPort store the desired Bindwidth
  tempmsg.CommPort=HIGHMODE;
  sendMsg2Server(ServerIP, C2SPORT,tempmsg);
}

void mm_cb(GtkButton *button,  GuiData * data)
{
  printf("auto mode, user :%s\n", (char *)data);
  string TalktoName((char *)data);
  MessageS tempmsg;
  tempmsg.type=STARTCONNECT;

  strcpy(tempmsg.UserName, SelfUserName.c_str());
  strcpy(tempmsg.IPAddress,TalktoName.c_str());
  tempmsg.CommPort=MUTEMODE;
  sendMsg2Server(ServerIP, C2SPORT,tempmsg);
}




void lm_cb(GtkButton *button,  GuiData * data)
{
  printf("low mode, user :%s\n", (char *)data);
  string TalktoName((char *)data);
  MessageS tempmsg;
  tempmsg.type=STARTCONNECT;

  //give talker's Username and talkto's Username to this structure
  //and the CommPort store the connect type
  strcpy(tempmsg.UserName, SelfUserName.c_str());
  strcpy(tempmsg.IPAddress,TalktoName.c_str());
  //!!!!!!!!!!!!!!!!!!!!need select mode to select to select the right mode
  //tempmsg.CommPort store the desired Bindwidth
  tempmsg.CommPort=LOWMODE;
  sendMsg2Server(ServerIP, C2SPORT,tempmsg);

}


void am_cb(GtkButton *button,  GuiData * data)
{
  printf("auto mode, user :%s\n", (char *)data);
  string TalktoName((char *)data);
  MessageS tempmsg;
  tempmsg.type=STARTCONNECT;

  strcpy(tempmsg.UserName, SelfUserName.c_str());
  strcpy(tempmsg.IPAddress,TalktoName.c_str());
  tempmsg.CommPort=AUTOMODE;
  sendMsg2Server(ServerIP, C2SPORT,tempmsg);

}


void menuitem_response( gchar* buf)
{
    //below is start chating connection, 
    //the user send to server which other user name this user want to connect
    //tempmsg.UserName ==> self name;
    //tempmsg.IPAddress ==> want to talked userName
    // printf("chatting@@@@@@@@@@@@@@@@@\n");
    // printf("chat to %s\n", buf);
    // string TalktoName(buf);
    // MessageS tempmsg;
    // tempmsg.type=STARTCONNECT;

    // //give talker's Username and talkto's Username to this structure
    // //and the CommPort store the connect type
    // strcpy(tempmsg.UserName, SelfUserName.c_str());
    // strcpy(tempmsg.IPAddress,TalktoName.c_str());
    // //!!!!!!!!!!!!!!!!!!!!need select mode to select to select the right mode
    // //tempmsg.CommPort store the desired Bindwidth
    // tempmsg.CommPort=LOWMODE;
    // sendMsg2Server(ServerIP, C2SPORT,tempmsg);

  //////////////////////////////////////////
    if(myGui->table != NULL)
    gtk_widget_hide(myGui->table);
  //gtk_widget_hide(myGui->table);
  myGui->table=gtk_table_new(4,2,TRUE);

  GtkWidget *friend_name, *chatting, *high_mode, *medium_mode, *low_mode, *auto_mode;
  myGui->open_contact =gtk_label_new( buf );
  chatting=gtk_button_new_with_label ("stop chat");
  g_signal_connect (G_OBJECT (chatting), "clicked", G_CALLBACK (stop_chating), (gpointer) g_strdup (buf)) ;
  high_mode=gtk_button_new_with_label ("high mode");
   g_signal_connect (G_OBJECT (high_mode), "clicked", G_CALLBACK (hm_cb), (gpointer) g_strdup (buf)) ;
  medium_mode=gtk_button_new_with_label ("mute mode");
  g_signal_connect (G_OBJECT (medium_mode), "clicked", G_CALLBACK (mm_cb), (gpointer) g_strdup (buf)) ;
  low_mode=gtk_button_new_with_label ("low mode");
  g_signal_connect (G_OBJECT (low_mode), "clicked", G_CALLBACK (lm_cb), (gpointer) g_strdup (buf)) ;
  auto_mode=gtk_button_new_with_label ("auto mode");
  g_signal_connect (G_OBJECT (auto_mode), "clicked", G_CALLBACK (am_cb), (gpointer) g_strdup (buf)) ;
    
  myGui->open_contact=gtk_label_new(buf);
  gtk_table_attach_defaults (GTK_TABLE(myGui->table), myGui->open_contact , 0, 2, 0, 1);
  gtk_table_attach_defaults (GTK_TABLE(myGui->table), high_mode, 0, 1, 1, 2);
  gtk_table_attach_defaults (GTK_TABLE(myGui->table), medium_mode, 1, 2, 1, 2);
  gtk_table_attach_defaults (GTK_TABLE(myGui->table), low_mode, 0, 1, 2, 3);
  gtk_table_attach_defaults (GTK_TABLE(myGui->table), auto_mode, 1, 2, 2, 3);
  gtk_table_attach_defaults (GTK_TABLE(myGui->table), chatting, 0, 2, 3, 4);
  
            gtk_widget_show_all (myGui->table);
           // gtk_widget_show (auto_mode);
  
    gtk_layout_put(GTK_LAYOUT(myGui->main_box), myGui->table,140,140);
}


void menuitem_response_disconnect( gchar* buf)
{
    //below is disconnect chating connection, 
    //the user send to server which other user name this user want to disconnect
    //tempmsg.UserName ==> self name;
    //tempmsg.IPAddress ==> want to disconnected userName
    printf("stop chating @@@@@@@@@@@@@@@@@\n");
    printf("stop chat to %s\n", buf);
    string stopTalktoName(buf);
    MessageS tempmsg;
    tempmsg.type=STOPCONNECT;
    //give talker's Username and talkto's Username to this structure
    strcpy(tempmsg.UserName, SelfUserName.c_str());
    strcpy(tempmsg.IPAddress,stopTalktoName.c_str());
    sendMsg2Server(ServerIP, C2SPORT,tempmsg);
}



void create_menu(GtkWidget * * menu )
{
  *(menu) = gtk_menu_new ();
  GtkWidget *menu_items;
  int i;
  //for (i = 0; i < 3; i++)
    for (auto it=AllUserMap.begin(); it!=AllUserMap.end(); ++it)
      //cout << "[" << it->first << "]: [" <<  it->second << "]"<<endl;
        {
            /* Copy the names to the buf. */
            sprintf (status_buf, "Test-undermenu - %d", i);

            /* Create a new menu-item with a name... */
            menu_items = gtk_menu_item_new_with_label ( it->first.c_str());

            /* ...and add it to the menu. */
            gtk_menu_shell_append (GTK_MENU_SHELL (*menu), menu_items);
            g_signal_connect_swapped (menu_items, "activate",G_CALLBACK (menuitem_response),  (gpointer) g_strdup(it->first.c_str()) );
            //g_signal_connect_swapped(menu_items, "activate", G_CALLBACK(menuitem_response), NULL);
      /* Do something interesting when the menuitem is selected */
       // g_signal_connect_swapped (menu_items, "activate", G_CALLBACK (menuitem_response),  (gpointer) g_strdup (buf));

            /* Show the widget */
            gtk_widget_show (menu_items);
        }
}
//to be merge
static gboolean find_chaters( GuiData *data, GdkEvent *event )
{
  
  create_menu(& (data->menu) );
    if (event->type == GDK_BUTTON_PRESS) {
    printf("find chaters!\n");
        GdkEventButton *bevent = (GdkEventButton *) event; 
        gtk_menu_popup (GTK_MENU (data->menu), NULL, NULL, (GtkMenuPositionFunc) set_position, data,
                        bevent->button, bevent->time);
        /* Tell calling code that we have handled this event; the buck
         * stops here. */
        return TRUE;
    }

    /* Tell calling code that we have not handled this event; pass it on. */
    return FALSE;
}
void logout()
{
  printf("logout!\n");
  MessageS tempmsg;
  tempmsg.type=SINGOUT;
  strcpy(tempmsg.UserName, SelfUserName.c_str());
  sendMsg2Server(ServerIP, C2SPORT,tempmsg);
}


void join_cb(GtkButton *button,  GuiData * data)
{

  //gtk_widget_hide_all(data->control0); 
  //gtk_container_remove( )
  gchar*s_ip;
  s_ip=(gchar*)gtk_entry_get_text(data->server_ip);
  ServerIP.assign(s_ip);
  cout <<"ServerIP: "<<ServerIP<<endl;

  gchar *username=NULL;
  username=(gchar*)gtk_entry_get_text(data->user_name);
  SelfUserName.assign(username);

  string DefaultIP="localhost";
  string PORT=C2SPORT;
  MessageS tempmsg;
  tempmsg.type=LOGIN;
  //cout <<"input username:";
  strcpy(tempmsg.UserName, username);
  string tempUserNameStr=tempmsg.UserName;
  cout <<"username is:"<<SelfUserName<<endl;

  // const string stringIP=getLocalIP();
  // LocalIP=stringIP;
  // strcpy(tempmsg.IPAddress,stringIP.c_str());
  // cout<<"IPAddress is:"<<tempmsg.IPAddress<<endl;
  // cout <<"=================="<<endl;
  // AllUserMap[tempUserNameStr]=stringIP;

  //sendMsg2Server(DefaultIP, PORT,tempmsg);
  sendMsg2Server(ServerIP, C2SPORT,tempmsg);
  
  gtk_container_remove (GTK_CONTAINER (data->cur_window), data->main_box);

  data->main_box = gtk_layout_new(NULL,NULL);;
  GtkWidget *chat_button;
  chat_button=gtk_button_new_with_label ("chat");
  data->chat_b=chat_button;
  gtk_widget_set_usize(GTK_WIDGET(chat_button),180,30);
  data->quit_b=gtk_button_new_with_label("quit");
  //  create_menu(data);
  g_signal_connect_swapped (chat_button, "event",G_CALLBACK (find_chaters), data);
  g_signal_connect (data->quit_b, "clicked",G_CALLBACK (logout), NULL);
  //  g_signal_connect (G_OBJECT (join_button), "clicked", G_CALLBACK (join_cb), data);

    //gtk_box_pack_start (GTK_BOX (data->main_box), chat_button, FALSE, FALSE, 2);
    //gtk_box_pack_start (GTK_BOX (data->main_box), data->quit_b, FALSE, FALSE, 2);
      gtk_layout_put(GTK_LAYOUT(data->main_box), data->chat_b,0,0);
    gtk_layout_put(GTK_LAYOUT(data->main_box), data->quit_b, 550, 600);
    gtk_container_add (GTK_CONTAINER (data->cur_window), data->main_box);
  set_background(data->main_box);
  //create_menu(data);
  gtk_widget_show_all(data-> cur_window);
}
//to be merge
void create_welcome( GuiData * data)
{
    GtkWidget *window,*main_box ;

    GtkStyle *style;
    GdkPixbuf *pixbuf;
    GdkPixmap *background;
    char* title = NULL;
    asprintf(&title, "Skype");
    window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    g_signal_connect (window, "delete-event", G_CALLBACK (gtk_main_quit), NULL);
    gtk_window_set_title((GtkWindow * )window, title);
    main_box = gtk_vbox_new (FALSE, 0);
    //data->main_box=main_box;
    set_background(window);

    GtkWidget * welcometext;
    GtkWidget *control1, *layout, *usr_name_label , *ip_label;
    GtkWidget *join_button;

    control1 = gtk_hbox_new (FALSE, 0);
    layout = gtk_layout_new(NULL,NULL);//gtk_hbox_new (FALSE, 0);
    data->main_box=layout;
    set_background(layout);
    insert_welcome(layout);

    //data->control0=control0;
    //data->control1=control1;
    join_button=gtk_button_new_with_label ("join");

    g_signal_connect (G_OBJECT (join_button), "clicked", G_CALLBACK (join_cb), data);
    usr_name_label=gtk_label_new("USER NAME");
    ip_label=gtk_label_new("SERVER IP:");
    data->user_name=(GtkEntry*)gtk_entry_new ();
    data->server_ip=(GtkEntry*)gtk_entry_new();
    // gtk_label_set_text((GtkLabel*)label,"welcome");   
    // gtk_box_pack_start (GTK_BOX (control0), label, FALSE, FALSE, 2);
    gtk_box_pack_start (GTK_BOX (control1), usr_name_label, FALSE, FALSE, 2);
    gtk_box_pack_start (GTK_BOX (control1), (GtkWidget*)(data->user_name), FALSE, FALSE, 2);
    gtk_box_pack_start (GTK_BOX (control1), ip_label, FALSE, FALSE, 2);
    gtk_box_pack_start (GTK_BOX (control1), (GtkWidget*)(data->server_ip), FALSE, FALSE, 2);
    gtk_box_pack_start (GTK_BOX (control1), join_button, FALSE, FALSE, 2);

    //   gtk_box_pack_start (GTK_BOX (main_box), control0, FALSE, FALSE, 2);
    gtk_layout_put(GTK_LAYOUT(layout), control1, 50, 150);
    //gtk_box_pack_start (GTK_BOX (main_box), control1, FALSE, FALSE, 2);

    gtk_container_add (GTK_CONTAINER (window), layout);
    data->cur_window=window;

    gtk_window_set_default_size (GTK_WINDOW (window), 640, 640);
    gtk_widget_show_all(window);
}

static void set_position(GtkMenu *menu, gint *x, gint *y, gboolean *push_in, gpointer user_data)
{
  printf("set position\n");
  gint tx,ty;
  GtkWidget *button = (GtkWidget *)  ( ((GuiData*) user_data)  ->chat_b );

  gdk_window_get_origin(button->window, x, y);
  //printf("%d, %d\n", tx,ty);
  *x += button->allocation.x;
  *y += (button->allocation.y + (button->allocation.height));
}

// to be merge
void insert_welcome(GtkWidget * layout)
{
    GError    *gerror;
    GdkPixbuf *old_pix,*new_pix;
    int        x_old,x_new,y_old,y_new;
    GtkWidget * image, *label, *button; 
    gerror = NULL;
    // load the image
    old_pix = gdk_pixbuf_new_from_file("welcome.jpg",&gerror);
    // get the size of the image 
     y_old = gdk_pixbuf_get_height (old_pix);
     x_old = gdk_pixbuf_get_width (old_pix);
    // Keep aspect ratio
      if (x_old > y_old) {
       x_new = 400;
       y_new = (int)((float)y_old)/((float)x_old)*200.0;
      } else {
       x_new = (int)((float)x_old)/((float)y_old)*400.0;
       y_new = 200;
      }
      cout<< "x old=" << x_old << "y old=" << y_old <<"\n";
      cout<< "x new=" << x_new << "y new=" << y_new <<"\n";
    // create new pictures
    new_pix = gdk_pixbuf_scale_simple(old_pix,
                      x_new,y_new,
                      GDK_INTERP_BILINEAR);

    image = gtk_image_new_from_pixbuf(new_pix); 

    label=gtk_label_new("WELCOME");
    button = gtk_button_new_with_label("Button 1");
    gtk_layout_put(GTK_LAYOUT(layout), image, 100, 0);
   // gtk_layout_put(GTK_LAYOUT(layout), button, 300, 300);

    //    gtk_layout_put(GTK_LAYOUT(layout), label, 0, 0); 
}


/*************************************Creating client GUI, realize_cb->hold the video****************************************/
static void realize_cb (GtkWidget *widget, RecvDataStream *data) 
{
  GdkWindow *window = gtk_widget_get_window (widget);
  guintptr window_handle;
  if (!gdk_window_ensure_native (window))
    g_error ("Couldn't create native window needed for GstXOverlay!");
  
    /* Retrieve window handler from GDK */
  #if defined (GDK_WINDOWING_WIN32)
    window_handle = (guintptr)GDK_WINDOW_HWND (window);
  #elif defined (GDK_WINDOWING_QUARTZ)
    window_handle = gdk_quartz_window_get_nsview (window);
  #elif defined (GDK_WINDOWING_X11)
    window_handle = GDK_WINDOW_XID (window);
  #endif
  /* Pass it to pipeline, which implements XOverlay and will forward it to the video sink */
  printf("hi1\n");
  gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (data->video_sink), window_handle);
  printf("hi2\n");
}

static void print_status(const char* inp)
{
  printf("%s\n",inp);
  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (myGui->status_text));
  gtk_text_buffer_insert_at_cursor(buffer,inp,strlen(inp));
}

static void
set_position2( GtkMenu *menu /*videoData* vdata*/ , gint *x, gint *y, gboolean *push_in, gpointer user_data)
{

  printf("set position\n");
  gint tx,ty;
  GtkWidget *button =  ((videoData *) user_data) -> mode_but ; //( ((GuiData*) user_data)  ->chat_b );

  gdk_window_get_origin(button->window, x, y);
  //printf("%d, %d\n", tx,ty);
  *x += button->allocation.x;
  *y += (button->allocation.y + (button->allocation.height));

}
void switch_to_high( videoData* vdata)
{
  printf("switch to high mode, with contact %s\n", vdata->contact);
  sprintf(status_buf,"switch to high mode, with contact %s\n", vdata->contact);
  print_status(status_buf);
}

void switch_to_low( videoData* vdata)
{
  printf("switch to low mode, with contact %s\n", vdata->contact);
  sprintf(status_buf,"switch to low mode, with contact %s\n", vdata->contact);

  print_status(status_buf);
}
void create_mode_menu(videoData* vdata, GdkEvent *event )
{

//  printf("enter create mode menu\n");
  //  if (event->type == GDK_BUTTON_PRESS) {
  printf("enter create mode menu\n");
  GtkWidget * menu_item1, *menu_item2, *root_menu, *menu, *menu_bar;
  menu=gtk_menu_new();
    menu_item1 = gtk_menu_item_new_with_label ("high mode");
    g_signal_connect_swapped (menu_item1, "activate",G_CALLBACK (switch_to_high),  (gpointer) vdata);
    menu_item2 = gtk_menu_item_new_with_label ("low mode");
    g_signal_connect_swapped (menu_item2, "activate",G_CALLBACK (switch_to_low),  (gpointer) vdata);
    gtk_menu_append (GTK_MENU (menu), menu_item1);
    gtk_menu_append (GTK_MENU (menu), menu_item2);
    gtk_widget_show_all(menu);

        GdkEventButton *bevent = (GdkEventButton *) event; 
        gtk_menu_popup (GTK_MENU (menu), NULL, NULL, (GtkMenuPositionFunc) set_position2, vdata,
                        bevent->button, bevent->time);
}

void video_stop(GtkButton *button,  char * contact)
{
  printf("video stop with contact %s\n", contact);
  sprintf(status_buf,"video stop with contact %s\n", contact);
  print_status(status_buf);
}
void video_mute(GtkButton *button,  char * contact)
{

  printf("video mute with contact %s\n", contact);
  sprintf(status_buf,"video mute with contact %s\n", contact);
  print_status(status_buf);
}


void create_chat_window( RecvDataStream* data )
{
  GtkWidget *display_box, *video_window, *controls, *stop_but,*mute_but,*switch_mode;
  if (myGui->chat_open == FALSE)
  {
    myGui->hbox= gtk_hbox_new(TRUE, 1);
    myGui->chat_window=gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title (GTK_WINDOW (myGui->chat_window), data->vdata->contact);
    gtk_window_set_default_size (GTK_WINDOW (myGui->chat_window), 640, 480);
    }
    video_window = gtk_drawing_area_new ();
    gtk_widget_set_double_buffered (video_window, FALSE);
    // g_signal_connect (video_window, "realize", G_CALLBACK (realize_cb), data);
        
    
    stop_but=gtk_button_new_from_stock (GTK_STOCK_MEDIA_STOP);
    mute_but=gtk_button_new_with_label("MUTE");
    controls = gtk_hbox_new (FALSE, 0);
    gtk_box_pack_start (GTK_BOX (controls), stop_but, FALSE, FALSE, 2);
    g_signal_connect (G_OBJECT (stop_but), "clicked", G_CALLBACK (video_stop), (gpointer) g_strdup (data->vdata->contact)) ;
    gtk_box_pack_start (GTK_BOX (controls), mute_but, FALSE, FALSE, 2);
    g_signal_connect (G_OBJECT (mute_but), "clicked", G_CALLBACK (video_mute), (gpointer) g_strdup (data->vdata->contact)) ;

    switch_mode=gtk_button_new_with_label("switch mode");
    data->vdata->mode_but=switch_mode;
    g_signal_connect_swapped (switch_mode, "clicked",G_CALLBACK (create_mode_menu), data->vdata);
    gtk_widget_show(switch_mode);
  // g_signal_connect_swapped (chat_button, "event",G_CALLBACK (find_chaters), data);
          /* Tell calling code that we have handled this event; the buck
         * stops here. */
/*    
    root_menu=gtk_menu_item_new_with_label ("Root Menu");
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (root_menu), menu);
    menu_bar=gtk_menu_bar_new();
    gtk_menu_bar_append (GTK_MENU_BAR (menu_bar), root_menu);
*/
    gtk_box_pack_start (GTK_BOX (controls), switch_mode, FALSE, FALSE, 2);

    display_box = gtk_vbox_new (FALSE, 0);
    gtk_box_pack_start (GTK_BOX (display_box), video_window, FALSE, FALSE, 2);
    gtk_box_pack_start (GTK_BOX (display_box), controls, FALSE, FALSE, 2);
    
    gtk_box_pack_start (GTK_BOX (myGui->hbox), display_box, FALSE, FALSE, 2);
    printf("chat window created\n");
    gtk_container_add (GTK_CONTAINER (myGui->chat_window), myGui->hbox);
    gtk_widget_show_all(myGui->chat_window);

/*
  else
  {
    printf("chat window already created\n");
  }*/
}
/*************************************Creating client GUI****************************************/











