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
#include <unordered_map>
#include <unordered_set>
#include <utility>      // std::pair, std::make_pair
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <thread>         // std::this_thread::sleep_for
#include <chrono> 
#include <time.h>       /* time */
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/interfaces/xoverlay.h>
#include <gst/pbutils/pbutils.h>

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

const int MAXIMUM_BW = 10000;
const int MAXBUFLEN = 100;
const int BACKLOG = 10 ; 
const int MAXDATASIZE = 256 ;// max number of bytes we can get at once 
const int ACTIVE_MODE = 0;
const int PASSIVE_MODE = 1;
const int RES_hogh = 0;
const int RES_LOW = 1;

char status_buf[100];

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

  GstCaps *videocaps; 
  GstCaps *audiocaps; 

  GMainLoop *main_loop;  /* GLib's Main Loop */

  gint mode;
  gint res_mode;
  gboolean connected;
  gboolean mode_selected;

  GtkEntry* ip_text, * port_text, *ba_text;
  GtkWidget *video_window; /* The drawing area where the video will be shown */
  GtkWidget* check_button, *check_button2;
  GtkWidget * text1, *text2, *text3, *status_text;
  GtkTextBuffer *buffer;

  GstState state;
  gint sourceid;
  gint startingUdpport;
}RecvDataStream ;

typedef struct _SentDataStream
{
  GstElement *audio_queue_1, *video_queue_1;
  GstElement *video_encoder, *audio_encoder;
  GstElement *videorate_controller, *audiorate_controller;
  GstElement *videoPayloader, *audioPayloader;
  GstElement *audio_queue_2, *video_queue_2;

  GstElement *serverRTPBIN;
  GstElement *udpsink_rtp0, *udpsink_rtp1, *udpsink_rtcp0, *udpsink_rtcp1 ;
  GstElement *udpsrc_rtcp0, *udpsrc_rtcp1 ;

  GstCaps *video_caps;
  GstCaps *audio_caps;
  gint mode;
  gint res_mode;
  gint FPS;
  gchar* clientip;

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

SentData* dataSource = new SentData;
queue<pthread_t> waitingThreads;

void parseMsg(string str, vector<string>& tokens)
{
    split(tokens, str, is_any_of(" ")); 
}

gboolean buildRecvPipeline(RecvDataStream* data, const string ip, const int startPort)
{
  /*Pads for requesting*/
  GstPadTemplate *recv_rtp_sink_temp, *recv_rtcp_sink_temp, *send_rtcp_src_temp;
  GstPad *recv_rtp_sink0, *recv_rtp_sink1;
  GstPad *recv_rtcp_sink0, *recv_rtcp_sink1; 
  GstPad *send_rtcp_src0, *send_rtcp_src1;

  /*static pads*/
  GstPad *udpsrc_rtp0_src, *udpsrc_rtp1_src, *udpsrc_rtcp0_src, *udpsrc_rtcp1_src;
  GstPad *udpsink_rtcp0_sink, *udpsink_rtcp1_sink ;
 
  GstBus *bus = gst_bus_new();
 
  data->ip_text = (GtkEntry*)gtk_entry_new ();
  data->port_text = (GtkEntry*)gtk_entry_new ();
  GstMessage *msg;
  GstStateChangeReturn ret;

  /* Create pipeline and attach a callback to it's
   * message bus */
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

  data->clientRTPBIN = gst_element_factory_make("gstrtpbin","clientRTPBIN");

  data->udpsrc_rtp0 = gst_element_factory_make("udpsrc", "udpsrc_rtp0");
  data->udpsrc_rtp1 = gst_element_factory_make("udpsrc", "udpsrc_rtp1");

  data->udpsrc_rtcp0 = gst_element_factory_make("udpsrc", "udpsrc_rtcp0");
  data->udpsrc_rtcp1 = gst_element_factory_make("udpsrc", "udpsrc_rtcp1");
  
  data->udpsink_rtcp0 = gst_element_factory_make("udpsink", "udpsink_rtcp0");
  data->udpsink_rtcp1 = gst_element_factory_make("udpsink", "udpsink_rtcp1");

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
  g_object_set(data->udpsrc_rtp0, "caps", data->videocaps, "port", startPort, NULL);
  g_object_set(data->udpsrc_rtcp0, "port", startPort+1, NULL);
  g_object_set(data->udpsink_rtcp0, "host", (gchar*)ip.c_str(), "port", startPort+2, "async", FALSE, "sync", FALSE, NULL);
  g_object_set(data->udpsrc_rtp1,  "caps", data->audiocaps, "port", startPort+3,  NULL);
  g_object_set(data->udpsrc_rtcp1, "port", startPort+4, NULL);
  g_object_set(data->udpsink_rtcp1, "host", (gchar*)ip.c_str(), "port", startPort+5, "async", FALSE, "sync", FALSE,  NULL);

  gst_bin_add_many(GST_BIN(data->clientPipeline), data->audio_decoder, data->audio_queue, data->video_decoder ,data->video_queue,
       data->videoDepayloader, data->audioDepayloader, data->clientRTPBIN,
       data->udpsrc_rtp0, data->udpsrc_rtp1, data->udpsink_rtcp0, data->udpsink_rtcp1,
       data->udpsrc_rtcp0, data->udpsrc_rtcp1, data->video_sink, data->audio_sink, NULL);

  if(!gst_element_link_many(data->video_queue, data->videoDepayloader, data->video_decoder, data->video_sink, NULL))
  {
    g_printerr("videoqueue, depayloader, decoder, and sink cannot be linked!\n");
    gst_object_unref (data->clientPipeline);
    return FALSE;
  }
  
  if(!gst_element_link_many(data->audio_queue, data->audioDepayloader, data->audio_decoder, data->audio_sink, NULL))
  {
    g_printerr("audio queue and audio decoder cannot be linked!.\n");
    gst_object_unref (data->clientPipeline);
    return FALSE;
  }

  recv_rtp_sink_temp = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS(data->clientRTPBIN), "recv_rtp_sink_%d");
  recv_rtp_sink0 = gst_element_request_pad (data->clientRTPBIN, recv_rtp_sink_temp, NULL, NULL);
  g_print ("Obtained request pad %s for recv_rtp_sink !.\n", gst_pad_get_name (recv_rtp_sink0));
  udpsrc_rtp0_src = gst_element_get_static_pad(data->udpsrc_rtp0, "src");
  recv_rtp_sink1 = gst_element_request_pad (data->clientRTPBIN, recv_rtp_sink_temp, NULL, NULL);
  g_print ("Obtained request pad %s for recv_rtp_sink !.\n", gst_pad_get_name (recv_rtp_sink1));
  udpsrc_rtp1_src = gst_element_get_static_pad(data->udpsrc_rtp1, "src");


  recv_rtcp_sink_temp = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(data->clientRTPBIN), "recv_rtcp_sink_%d");
  recv_rtcp_sink0 = gst_element_request_pad(data->clientRTPBIN, recv_rtcp_sink_temp ,NULL, NULL);
  g_print ("Obtained request pad %s for recv_rtcp_sink !.\n", gst_pad_get_name (recv_rtcp_sink0));
  udpsrc_rtcp0_src = gst_element_get_static_pad(data->udpsrc_rtcp0, "src");
  recv_rtcp_sink1 = gst_element_request_pad(data->clientRTPBIN, recv_rtcp_sink_temp ,NULL, NULL);
  g_print ("Obtained request pad %s for recv_rtcp_sink !.\n", gst_pad_get_name (recv_rtcp_sink1));
  udpsrc_rtcp1_src = gst_element_get_static_pad(data->udpsrc_rtcp1, "src");


  send_rtcp_src_temp = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS(data->clientRTPBIN), "send_rtcp_src_%d");
  send_rtcp_src0 = gst_element_request_pad(data->clientRTPBIN, send_rtcp_src_temp, NULL, NULL);
  g_print ("Obtained request pad %s for send_rtcp_src !.\n", gst_pad_get_name (send_rtcp_src0));
  udpsink_rtcp0_sink = gst_element_get_static_pad(data->udpsink_rtcp0, "sink");
  send_rtcp_src1 = gst_element_request_pad(data->clientRTPBIN, send_rtcp_src_temp, NULL, NULL);
  g_print ("Obtained request pad %s for send_rtcp_src !.\n", gst_pad_get_name (send_rtcp_src1));
  udpsink_rtcp1_sink = gst_element_get_static_pad(data->udpsink_rtcp1, "sink");


  if(gst_pad_link (udpsrc_rtp0_src, recv_rtp_sink0 )!= GST_PAD_LINK_OK ||
     gst_pad_link (udpsrc_rtp1_src, recv_rtp_sink1 )!= GST_PAD_LINK_OK ||
     gst_pad_link (udpsrc_rtcp0_src, recv_rtcp_sink0 )!= GST_PAD_LINK_OK ||
     gst_pad_link (udpsrc_rtcp1_src, recv_rtcp_sink1 )!= GST_PAD_LINK_OK ||
     gst_pad_link (send_rtcp_src0, udpsink_rtcp0_sink )!= GST_PAD_LINK_OK ||
     gst_pad_link (send_rtcp_src1, udpsink_rtcp1_sink )!= GST_PAD_LINK_OK  )
  {
    g_printerr ("Some requested pads cannot be linked with static pads!\n");
    gst_object_unref (data->clientPipeline);
    return FALSE;
  }

  gst_object_unref (udpsrc_rtp0_src);
  gst_object_unref (udpsrc_rtp1_src);
  gst_object_unref (udpsrc_rtcp0_src);
  gst_object_unref (udpsrc_rtcp1_src);
  gst_object_unref (udpsink_rtcp0_sink);
  gst_object_unref (udpsink_rtcp1_sink);
 
  // g_signal_connect (data->clientRTPBIN, "pad-added", G_CALLBACK (rtpbin_pad_added_handler), data);

	//where ui goes(create ui before set playing);
	
  // bus = gst_element_get_bus (data->clientPipeline);
  // gst_bus_add_signal_watch (bus);
  // g_signal_connect (G_OBJECT (bus), "message::error", (GCallback)error_cb, data);
  // g_signal_connect (G_OBJECT (bus), "message::eos", (GCallback)eos_cb, data);
  // g_signal_connect (G_OBJECT (bus), "message::state-changed", (GCallback)state_changed_cb, data);
  // gst_object_unref (bus);

   ret=gst_element_set_state(data->clientPipeline, GST_STATE_PLAYING );
   if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (data->clientPipeline);
    return FALSE;
  }
  /* Free resources */
  gst_element_release_request_pad (data->clientRTPBIN, recv_rtp_sink0);
  gst_element_release_request_pad (data->clientRTPBIN, recv_rtp_sink1);
  gst_element_release_request_pad (data->clientRTPBIN, recv_rtcp_sink0);
  gst_element_release_request_pad (data->clientRTPBIN, recv_rtcp_sink1);
  gst_element_release_request_pad (data->clientRTPBIN, send_rtcp_src0);
  gst_element_release_request_pad (data->clientRTPBIN, send_rtcp_src1);
  gst_object_unref (recv_rtp_sink0);
  gst_object_unref (recv_rtp_sink1);
  gst_object_unref (send_rtcp_src0);
  gst_object_unref (send_rtcp_src1);
  gst_object_unref (recv_rtcp_sink0);
  gst_object_unref (recv_rtcp_sink1);

  gst_object_unref (bus);
  gst_element_set_state (data->clientPipeline, GST_STATE_NULL);
  gst_object_unref (data->clientPipeline);
  return TRUE;
}

void buildSendPipeline(SentDataStream* data, const string ip, const int startPort)
{
  return ;
}

void removeRecvPipeline(RecvDataStream* data, const string ip)
{
  return ;
}

void removeSendPipeline(SentDataStream* data, const string ip)
{
  return ;
}

void *incomingData(void *arg)
{
  char* s_ip = (char*)arg;
  string ip(s_ip);
  RecvDataStream* data = (*dataSource->idata)[ip] ;
  int startPort = (*dataSource->partnersInfo)[ip].second;
  buildRecvPipeline(data, ip, startPort);
  return NULL;
}

void *outcomingData(void *arg)
{
  char* s_ip = (char*)arg;
  string ip(s_ip);
  SentDataStream* data = (*dataSource->odata)[ip];
  int startPort = (*dataSource->partnersInfo)[ip].second;
  buildSendPipeline(data, ip, startPort);
  return NULL;
}

void startNewSession(const string& ip, const string& name, const string& startPort)
{
  pair<string, int> p(name, atoi(startPort.c_str()));
  RecvDataStream* idata = new RecvDataStream;
  SentDataStream* odata = new SentDataStream;
  (*dataSource->partnersInfo)[ip] = p;
  (*dataSource->idata)[ip] = idata;
  (*dataSource->odata)[ip] = odata;
  pthread_t r,s;
  pthread_create(&r, NULL, incomingData, (void*)(ip.c_str()) );  
  pthread_create(&s, NULL, outcomingData, (void*)(ip.c_str()) );
  waitingThreads.push(r);
  waitingThreads.push(s);

}

gboolean initUserPipeline(SentData* data)
{
  data->serverPipeline = gst_pipeline_new("server");

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
  return TRUE;
}

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

typedef struct _VCDATA{
	int chat_id;
	GtkWidget *chatting, *high_mode, *medium_mode, *low_mode, *auto_mode;

}VCDATA;
GtkTextBuffer * buffer;
typedef struct _GuiData {

    GtkTextBuffer *buffer;
    GtkEntry * user_name, *server_ip;
	GtkWidget *main_box, *cur_window;
	GtkWidget *menu;
	GtkWidget *chat_b, *quit_b;
	GtkWidget *table=NULL;
	GtkWidget * open_contact  ;
	bool chat_open=	FALSE;
	GtkWidget * status_text;
	GtkWidget * chat_window;
	GtkWidget * hbox;

} GuiData;
typedef struct _videoData {
	char * contact;
	GtkWidget * mode_but; 
}videoData;

GuiData * myGui;
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

//	printf("enter create mode menu\n");
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

void create_chat_window(videoData * vdata )
{
	GtkWidget *display_box, *video_window, *controls, *stop_but,*mute_but,*switch_mode;
	if (myGui->chat_open == FALSE)
	{
		myGui->hbox= gtk_hbox_new(TRUE, 1);
		myGui->chat_window=gtk_window_new (GTK_WINDOW_TOPLEVEL);
		gtk_window_set_title (GTK_WINDOW (myGui->chat_window), vdata->contact);
		gtk_window_set_default_size (GTK_WINDOW (myGui->chat_window), 640, 480);
	//	g_signal_connect (G_OBJECT (chat_window), "delete-event", G_CALLBACK (delete_event_cb), NULL);
	}
		video_window = gtk_drawing_area_new ();
  		gtk_widget_set_double_buffered (video_window, FALSE);
				
		
		stop_but=gtk_button_new_from_stock (GTK_STOCK_MEDIA_STOP);
		mute_but=gtk_button_new_with_label("MUTE");
		controls = gtk_hbox_new (FALSE, 0);
		gtk_box_pack_start (GTK_BOX (controls), stop_but, FALSE, FALSE, 2);
		g_signal_connect (G_OBJECT (stop_but), "clicked", G_CALLBACK (video_stop), (gpointer) g_strdup (vdata->contact)) ;
		gtk_box_pack_start (GTK_BOX (controls), mute_but, FALSE, FALSE, 2);
		g_signal_connect (G_OBJECT (mute_but), "clicked", G_CALLBACK (video_mute), (gpointer) g_strdup (vdata->contact)) ;

		switch_mode=gtk_button_new_with_label("switch mode");
		vdata->mode_but=switch_mode;
		g_signal_connect_swapped (switch_mode, "clicked",G_CALLBACK (create_mode_menu), vdata);
		gtk_widget_show(switch_mode);
	//g_signal_connect_swapped (chat_button, "event",G_CALLBACK (find_chaters), data);
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

void begin_chat(GtkButton *button,  GuiData * data)
{
	printf("connecting contact : %s\n", (char *) data);
	videoData* vdata= new videoData;
	char* contact= g_strdup ( (char*)data);
	vdata->contact=contact;
	//printf("contact = %s\n", contact);
	create_chat_window(vdata);			
}

void menuitem_response( gchar* buf)
{
	//printf("chatting\n");
	printf("chatter is %s\n", buf);
	//table=gtktable;
	//gtk_layout_move (myGui->main_box,)
	if(myGui->table != NULL)
		gtk_widget_hide(myGui->table);
	//gtk_widget_hide(myGui->table);
	myGui->table=gtk_table_new(4,2,TRUE);

	GtkWidget *friend_name, *chatting, *high_mode, *medium_mode, *low_mode, *auto_mode;
	myGui->open_contact =gtk_label_new( buf );
	chatting=gtk_button_new_with_label ("start chat");
	g_signal_connect (G_OBJECT (chatting), "clicked", G_CALLBACK (begin_chat), (gpointer) g_strdup (buf)) ;
	high_mode=gtk_button_new_with_label ("high mode");
	
	medium_mode=gtk_button_new_with_label ("medium mode");
	low_mode=gtk_button_new_with_label ("low mode");
	auto_mode=gtk_button_new_with_label ("auto mode");
		
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

void create_menu(GuiData * data)
{
	data->menu = gtk_menu_new ();
	GtkWidget *menu_items;
	int i;
	for (i = 0; i < 3; i++)
        {
            /* Copy the names to the buf. */
            sprintf (status_buf, "Test-undermenu - %d", i);

            /* Create a new menu-item with a name... */
            menu_items = gtk_menu_item_new_with_label (status_buf);

            /* ...and add it to the menu. */
            gtk_menu_shell_append (GTK_MENU_SHELL (data->menu), menu_items);

			g_signal_connect_swapped (menu_items, "activate",G_CALLBACK (menuitem_response),  (gpointer) g_strdup (status_buf));

	    /* Do something interesting when the menuitem is selected */
		   // g_signal_connect_swapped (menu_items, "activate", G_CALLBACK (menuitem_response),  (gpointer) g_strdup (buf));

            /* Show the widget */
            gtk_widget_show (menu_items);
        }
	
	
}
//to be merge
static void
set_position(GtkMenu *menu, gint *x, gint *y, gboolean *push_in, gpointer user_data)
{

	printf("set position\n");
	gint tx,ty;
 	GtkWidget *button = (GtkWidget *)  ( ((GuiData*) user_data)  ->chat_b );

 	gdk_window_get_origin(button->window, x, y);
	//printf("%d, %d\n", tx,ty);
  *x += button->allocation.x;
  *y += (button->allocation.y + (button->allocation.height));

}
//to be merge
static gboolean find_chaters( GuiData *data,
                              GdkEvent *event )
{
	
	create_menu(data );
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
	printf("log out!\n");
}
// to be merge
void join_cb(GtkButton *button,  GuiData * data)
{

	//gtk_widget_hide_all(data->control0); 
	//gtk_container_remove( )
	const char * tmp=gtk_entry_get_text(data->server_ip); 
	printf("server ip= %s\n", tmp);	
    gtk_container_remove (GTK_CONTAINER (data->cur_window), data->main_box);

    data->main_box =gtk_layout_new(NULL,NULL); //gtk_vbox_new (FALSE, 0);
	GtkWidget *chat_button;
	chat_button=gtk_button_new_with_label ("chat");
	data->chat_b=chat_button;
	gtk_widget_set_usize(GTK_WIDGET(chat_button),180,30);
	data->quit_b=gtk_button_new_with_label ("LOG OUT");
//	create_menu(data);
	g_signal_connect_swapped (chat_button, "event",G_CALLBACK (find_chaters), data);
	g_signal_connect_swapped (data->quit_b, "clicked",G_CALLBACK (logout), NULL);
//	g_signal_connect (G_OBJECT (join_button), "clicked", G_CALLBACK (join_cb), data);

   // gtk_box_pack_start (GTK_BOX (data->main_box), chat_button, FALSE, FALSE, 2);

    gtk_layout_put(GTK_LAYOUT(data->main_box), data->chat_b,0,0);
    gtk_layout_put(GTK_LAYOUT(data->main_box), data->quit_b, 550, 600);
	data->status_text=gtk_text_view_new();	
	//(data->status_ext) -> set_border_window_size(TextWindowType.TOP, 300);
//	gtk_text_view_set_border_window_size( (GtkTextView*) (data->status_text),GTK_TEXT_WINDOW_BOTTOM,200);
//	gtk_text_view_set_border_window_size( (GtkTextView*) (data->status_text),GTK_TEXT_WINDOW_RIGHT,200);
	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->status_text));
	sprintf(status_buf,"MM Status Log:\n");
	gtk_text_buffer_set_text (buffer, status_buf, -1);

    gtk_layout_put(GTK_LAYOUT(data->main_box), data->status_text, 250, 300);
    gtk_container_add (GTK_CONTAINER (data->cur_window), data->main_box);
	
    set_background(data->main_box);
	//create_menu(data);
	gtk_widget_show_all(data-> cur_window);

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

//		gtk_layout_put(GTK_LAYOUT(layout), label, 0, 0); 

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
    GtkWidget *control1, *layout, *usr_name_label,*ip_label;
	
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
    ip_label=gtk_label_new("Server IP:");
    data->user_name=(GtkEntry*)gtk_entry_new ();
    data->server_ip=(GtkEntry*)gtk_entry_new ();
   // gtk_label_set_text((GtkLabel*)label,"welcome");   
   // gtk_box_pack_start (GTK_BOX (control0), label, FALSE, FALSE, 2);
    gtk_box_pack_start (GTK_BOX (control1), usr_name_label, FALSE, FALSE, 2);
    gtk_box_pack_start (GTK_BOX (control1), (GtkWidget*)(data->user_name), FALSE, FALSE, 2);
    gtk_box_pack_start (GTK_BOX (control1), ip_label, FALSE, FALSE, 2);
    gtk_box_pack_start (GTK_BOX (control1), (GtkWidget*)(data->server_ip), FALSE, FALSE, 2);
    gtk_box_pack_start (GTK_BOX (control1), join_button, FALSE, FALSE, 2);

    gtk_layout_put(GTK_LAYOUT(layout), control1, 50, 150);
	
	//////////////status log//////////
	data->status_text=gtk_text_view_new();	
	//(data->status_ext) -> set_border_window_size(TextWindowType.TOP, 300);
//	gtk_text_view_set_border_window_size( (GtkTextView*) (data->status_text),GTK_TEXT_WINDOW_BOTTOM,200);
//	gtk_text_view_set_border_window_size( (GtkTextView*) (data->status_text),GTK_TEXT_WINDOW_RIGHT,200);
	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->status_text));
	sprintf(status_buf,"MM Status Log:\n");
	gtk_text_buffer_set_text (buffer, status_buf, -1);
    gtk_layout_put(GTK_LAYOUT(layout), data->status_text, 250, 250);

    gtk_container_add (GTK_CONTAINER (window), layout);
	data->cur_window=window;

    gtk_window_set_default_size (GTK_WINDOW (window), 640, 640);
    gtk_widget_show_all(window);

}

void * worker1 (void *inp)
{
	printf("worker1\n");
}


void * worker2 (void *inp)
{
	printf("worker2\n");
}

int main(int argc, char *argv[]) 
{
  gtk_init (&argc, &argv);
  gst_init(&argc, &argv);
  pthread_t tr1,tr2;
  pthread_create(&tr1, NULL, worker1, NULL);
  pthread_create(&tr2, NULL, worker2, NULL);

  dataSource = new SentData;
  printf("User enters mp3 chatroom!\n");
  GuiData gdata;
  myGui=&gdata;
  create_welcome(&gdata);
 
  if(!initUserPipeline(dataSource))
  {
    fprintf(stderr, "Error occurs in initialing user's initial pipeline!\n");
    exit(1);
  }
  gtk_main();	
  delete dataSource;
	printf("User leaves mp3 chatroom!\n");
}



