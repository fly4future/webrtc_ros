# webrtc_ros Signaling Protocol Specification

This document outlines the signaling protocol used by the `webrtc_ros` package to
communicate between clients.

While WebRTC provides the required infrastructure to stream video, audio, and
data in real time, it requires a signaling channel to coordinate the connection.
This channel handles the exchange of Session Description Protocol (SDP) offers
and answers, as well as Interactive Connectivity Establishment (ICE) candidates, all
of which are essential for WebRTC to establish a connection between clients. The
`webrtc_ros` package implements a custom, lightweight signaling protocol that gives
clients a simple and flexible way to exchange this setup information.

The protocol relies on exchanging JSON objects over WebSockets. To keep parsing
straightforward, each JSON object is transmitted in its own WebSocket message.
Every message includes a `type` field to identify its purpose.

1. **The `webrtc_ros` signaling server**

   This package provides a signaling server that clients can connect to using WebSockets. The server listens for incoming connections and routes messages between clients. Clients can connect to the signaling server at `ws://<server_address>:<port>/<id>`, where `<server_address>` is the address of the machine running the signaling server, `<port>` is the port it's listening on (default is 8000), and `<id>` is a unique identifier for the client (e.g., `uav124`).

   To run the signaling server, use the following command:

   ```bash
   python3 scripts/signaling_server.py 0.0.0.0:8000
   ```

   > [!NOTE]
   > You should have the `websockets` Python package installed to run the signaling server. You might want to set up a virtual environment and install the package there to avoid conflicts with other Python packages on your system.

   When a client connects to the signaling server, it can send messages to other clients by specifying their unique identifiers. The signaling server will route messages based on these identifiers, allowing clients to exchange the necessary information to establish WebRTC connections.

   ```json
   {
     "type": <message_type>,
     "to": <recipient_id>,
     ... other fields depending on message type ...
   }
   ```

2. **The `webrtc_ros` signaling protocol**

   Message Types:
   - **ice_candidate** - A message that contains an ICE candidate
   - **offer** - A message that contains a WebRTC SDP offer
   - **answer** - A message that contains a WebRTC SDP answer
   - **request** - A message that requests an action from the remote client
   1. **ICE Candidate Message**
      This message is used to exchange ICE candidates between clients. ICE candidates are network information that WebRTC uses to establish peer-to-peer connections. Each candidate includes details about the network interface and port that can be used for communication.

      ```jsonc
      {
        "type": "ice_candidate",
        "sdp_mid": <string>,      // The media stream identifier (e.g., "video")
        "sdp_mline_index": <int>, // The index of the media description in the SDP
        "candidate": <string>     // The ICE candidate string
      }
      ```

   2. **SDP Offer and Answer Message**
      These messages are used to exchange SDP offers and answers between clients. The offer describes the media capabilities and streams of the sender, while the answer responds with the capabilities and streams of the receiver.

      ```jsonc
      {
        "type": "offer" | "answer",
        "sdp": <string> // The SDP string describing the media session
      }
      ```

   3. **Request Message**
      This message is used to request a list of streams from the remote client. The remote client should respond with an "offer" message containing an SDP offer that describes the available streams.

      ```jsonc
      {
        "type": "request",
        "id": <string> // The remote client's unique identifier (e.g., "uav124")
        "streams": <array of strings> // Optional: A list of stream names to request (e.g., ["camera1", "camera2"]), if not provided, the remote client should include all available streams in the offer.
      }
      ```
