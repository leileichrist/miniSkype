<h2>miniSkype-a live-conference P2P streaming system</h2>

<h3>Introduction</h3>
<p>In this final MP, we will use all the components built in previous MPs to build an online
conference P2P streaming system. Our Live-conference system enable multiple people
video chatting/conference at the same time. This video streaming is interest oriented,
double direction and bandwidth adaptation. Each user in our system is interest oriented,
as it only have video chatting with the user they are interested. The video chatting is
double direction, as the user sending and receiving video streaming simultaneously. The
most important character of our system is that it’s bandwidth adaptation, it can switch
between different video modes based on the networking environment.</p>


<h3>Design and algorithm</h3>
<p>Each user in our Live-conference system composed of three parts, the GUI thread, the
control communication thread (with coordinator), and video/audio streaming thread (with
other user). As there is no GUI requirement for the coordinator, the coordinator only has
control communication thread. Similar to the concept of P2P system, for each user itself is
a video streaming server to the other connected user, it’s also one client to the other user.
When one user has a video streaming request, it first send its request to the coordinator,
information include the destination node, the bandwidth information of itself and its desired
video streaming mode (include high quality, low quality, or mute mode).
Then the coordinator make decision according to the bandwidth situation of current
network and the network situation of the desired user. The decision include decline the
request, permit with the desired mode, upgrade to a higher mode, or decrease to a lower
mode. Then the coordinator send messages to the corresponding user, to tell them to build
the dynamic pipeline and start video streaming.
The GUI part is in charge of the user interface and controls the communication thread to
let client and server communicate with each other. These communication include
reservation, QoS negotiation, and video operation command, such as, Play, Stop, mode
switch...</p>

<h3>Challenge</h3>
<p>Gstreamer has one big weakness it that it cannot efficient apply dynamic pipeline element
modification. In this MP, we did great work to make our pipeline dynamic and change over
the situation changes. For example, one user has one camera and one camera’s
corresponding source element, if user1 start video streaming with user2, in user1’s pipeline,
it only need one branch of the tee element before the RTPBIN element. After a while, the
user3 join the video chatting, now in user1’s pipeline the tee element need another branch
to send the video streaming to user3. So we come out one efficient way to dynamic change
the pipeline element user1.</p>